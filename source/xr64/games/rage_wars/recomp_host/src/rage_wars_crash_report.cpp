#define NOMINMAX
#define WIN32_LEAN_AND_MEAN
#include <Windows.h>
#include <commctrl.h>
#include <bcrypt.h>
#include <winver.h>
#include <cwchar>
#include <vector>
#include <array>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <exception>
#include <string>
#include "rage_wars_crash_report.hpp"
#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "Version.lib")
#pragma comment(lib, "comctl32.lib")

namespace xr64::rage_wars::crash {
namespace {
std::filesystem::path report_directory;
std::string executable_hash = "unavailable";
bool display_window = true;
std::atomic_flag reporting = ATOMIC_FLAG_INIT;
std::string executable_version = "unavailable";
#if !defined(XR64_PUBLIC_RELEASE)
struct Lookup { std::int32_t target = 0; const char* file = nullptr; int line = 0; };
thread_local Lookup last_lookup;

struct CallFrame {
    std::uint64_t token = 0;
    std::int32_t target = 0;
    std::uint32_t caller_pc = UINT32_MAX, caller_rom = UINT32_MAX, caller_section = UINT32_MAX;
    std::uint32_t caller_function_rom = UINT32_MAX, caller_section_rom = UINT32_MAX;
    const char* file = nullptr;
    int line = 0;
    const char* phase = "unavailable";
    CallIdentity identity{};
    std::array<std::uint64_t, 6> registers{};
};
struct CallJournal {
    std::array<CallFrame, 64> frames{};
    std::uint32_t depth = 0, overflow_depth = 0;
    std::uint64_t sequence = 0;
    bool observed = false;
    CallFrame last_completed{};
};
thread_local CallJournal call_journal;
#endif


std::string hex(std::uint64_t value, int width = 8) {
    char text[32]{};
    std::snprintf(text, sizeof(text), "0x%0*llX", width, static_cast<unsigned long long>(value));
    return text;
}
#if !defined(XR64_PUBLIC_RELEASE)
std::string logical_file(const char* file) {
    if (!file) return "unavailable";
    std::string path(file);
    for (char& c : path) if (c == '\\') c = '/';
    for (const char* prefix : {"staging/", "xr64/", "toolchain/"}) {
        const auto at = path.find(prefix);
        if (at != std::string::npos) return path.substr(at);
    }
    const auto slash = path.find_last_of('/');
    return slash == std::string::npos ? path : path.substr(slash + 1);
}
#endif
std::string hash_executable() {
    std::array<wchar_t, 32768> path{};
    const auto n = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!n || n >= path.size()) return "unavailable";
    HANDLE file = CreateFileW(path.data(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_DELETE,
                             nullptr, OPEN_EXISTING, FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return "unavailable";
    BCRYPT_ALG_HANDLE algorithm = nullptr;
    BCRYPT_HASH_HANDLE hash = nullptr;
    bool ok = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0;
    if (ok) ok = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    std::array<unsigned char, 65536> chunk{};
    DWORD count = 0;
    while (ok) {
        if (!ReadFile(file, chunk.data(), static_cast<DWORD>(chunk.size()), &count, nullptr)) { ok = false; break; }
        if (!count) break;
        ok = BCryptHashData(hash, chunk.data(), count, 0) >= 0;
    }
    std::array<unsigned char, 32> digest{};
    if (ok) ok = BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    if (!ok) return "unavailable";
    std::string output;
    for (unsigned char byte : digest) {
        char text[3]{};
        std::snprintf(text, sizeof(text), "%02X", static_cast<unsigned>(byte));
        output += text;
    }
    return output;
}
std::string executable_product_version() {
    std::array<wchar_t, 32768> path{};
    const auto path_length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!path_length || path_length >= path.size()) return "unavailable";
    DWORD ignored = 0;
    const DWORD size = GetFileVersionInfoSizeW(path.data(), &ignored);
    if (!size) return "unavailable";
    std::vector<unsigned char> data(size);
    if (!GetFileVersionInfoW(path.data(), 0, size, data.data())) return "unavailable";

    struct Translation { WORD language; WORD code_page; };
    Translation* translations = nullptr;
    UINT translation_bytes = 0;
    if (VerQueryValueW(data.data(), L"\\VarFileInfo\\Translation",
            reinterpret_cast<void**>(&translations), &translation_bytes) &&
            translations && translation_bytes >= sizeof(Translation)) {
        const UINT count = translation_bytes / sizeof(Translation);
        for (UINT i = 0; i < count; ++i) {
            wchar_t query[96]{};
            std::swprintf(query, 96, L"\\StringFileInfo\\%04x%04x\\ProductVersion",
                translations[i].language, translations[i].code_page);
            wchar_t* value = nullptr;
            UINT value_chars = 0;
            if (!VerQueryValueW(data.data(), query, reinterpret_cast<void**>(&value), &value_chars) ||
                    !value || !value_chars || !value[0]) continue;
            const int bytes = WideCharToMultiByte(CP_UTF8, 0, value, -1, nullptr, 0, nullptr, nullptr);
            if (bytes <= 1) continue;
            std::string result(static_cast<std::size_t>(bytes), '\0');
            if (!WideCharToMultiByte(CP_UTF8, 0, value, -1, result.data(), bytes, nullptr, nullptr)) continue;
            result.resize(static_cast<std::size_t>(bytes - 1));
            return result;
        }
    }

    VS_FIXEDFILEINFO* fixed = nullptr;
    UINT fixed_bytes = 0;
    if (VerQueryValueW(data.data(), L"\\",
            reinterpret_cast<void**>(&fixed), &fixed_bytes) &&
            fixed && fixed_bytes >= sizeof(VS_FIXEDFILEINFO)) {
        char version[64]{};
        std::snprintf(version, sizeof(version), "%u.%u.%u.%u",
            HIWORD(fixed->dwProductVersionMS), LOWORD(fixed->dwProductVersionMS),
            HIWORD(fixed->dwProductVersionLS), LOWORD(fixed->dwProductVersionLS));
        return version;
    }
    return "unavailable";
}

std::wstring widen(const std::string& text) {
    int n = MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0);
    std::wstring result(n, L'\0');
    MultiByteToWideChar(CP_UTF8, 0, text.data(), static_cast<int>(text.size()), result.data(), n);
    return result;
}
bool save_text(const std::filesystem::path& path, const std::string& text) {
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                             CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD written = 0;
    bool ok = WriteFile(file, text.data(), static_cast<DWORD>(text.size()), &written, nullptr) != 0
              && written == text.size() && FlushFileBuffers(file) != 0;
    CloseHandle(file);
    return ok;
}
bool copy_report(HWND owner, const std::wstring& text) {
    HGLOBAL data = GlobalAlloc(GMEM_MOVEABLE, (text.size() + 1) * sizeof(wchar_t));
    if (!data) return false;
    void* memory = GlobalLock(data);
    if (!memory) { GlobalFree(data); return false; }
    std::memcpy(memory, text.c_str(), (text.size() + 1) * sizeof(wchar_t));
    GlobalUnlock(data);
    if (!OpenClipboard(owner)) { GlobalFree(data); return false; }
    bool ok = EmptyClipboard() && SetClipboardData(CF_UNICODETEXT, data);
    CloseClipboard();
    if (!ok) GlobalFree(data);
    return ok;
}
HRESULT CALLBACK dialog_callback(HWND window, UINT message, WPARAM button, LPARAM, LONG_PTR data) {
    if (message == TDN_CREATED) { SetForegroundWindow(window); return S_OK; }
    if (message == TDN_BUTTON_CLICKED && button == 101) {
        const bool ok = copy_report(window, *reinterpret_cast<const std::wstring*>(data));
        SendMessageW(window, TDM_SET_ELEMENT_TEXT, TDE_FOOTER,
                     reinterpret_cast<LPARAM>(ok ? L"Report copied. Paste it into the bug report." : L"Clipboard unavailable. Try Copy Report again, or use the saved file."));
        return S_FALSE; // Keep the report available after copying.
    }
    return S_OK;
}
void show_report(const std::string& text, const std::wstring& status) {
    const auto wide = widen(text);
    const TASKDIALOG_BUTTON buttons[] = {{101, L"Copy Report"}, {102, L"Close Game"}};
    TASKDIALOGCONFIG config{};
    config.cbSize = sizeof(config);
    config.dwFlags = TDF_ALLOW_DIALOG_CANCELLATION | TDF_EXPANDED_BY_DEFAULT | TDF_SIZE_TO_CONTENT;
    config.pszWindowTitle = L"Rage Wars crash report";
    config.pszMainIcon = TD_ERROR_ICON;
    config.pszMainInstruction = L"Rage Wars stopped because of an error.";
    config.pszContent = L"Copy the report below and tell us what happened just before the crash.";
    config.pszExpandedInformation = wide.c_str();
    config.pszFooter = status.c_str();
    config.cButtons = 2;
    config.pButtons = buttons;
    config.nDefaultButton = 101;
    config.pfCallback = dialog_callback;
    config.lpCallbackData = reinterpret_cast<LONG_PTR>(&wide);
    if (FAILED(TaskDialogIndirect(&config, nullptr, nullptr, nullptr))) {
        // A missing common-controls activation context must still leave a readable report.
        MessageBoxW(nullptr, (wide + L"\r\n" + status).c_str(), L"Rage Wars crash report", MB_OK | MB_ICONERROR | MB_SETFOREGROUND);
    }
}

#if !defined(XR64_PUBLIC_RELEASE)
std::string format_call_journal() {
    if (!call_journal.observed) return "\r\nIndirect call journal: no instrumented call on this thread";
    std::string text = "\r\nActive indirect calls: " + std::to_string(call_journal.depth);
    text += "\r\nUnrecorded nested calls beyond 64 frames: " + std::to_string(call_journal.overflow_depth);
    for (std::uint32_t i = 0; i < call_journal.depth; ++i) {
        const CallFrame& frame = call_journal.frames[i];
        text += "\r\nCall[" + std::to_string(i) + "] sequence=" + std::to_string(frame.token);
        text += " phase=" + std::string(frame.phase) + " target=" + hex(static_cast<std::uint32_t>(frame.target));
        text += " caller_pc=" + hex(frame.caller_pc) + " caller_rom=" + hex(frame.caller_rom);
        text += " caller_function_rom=" + hex(frame.caller_function_rom);
        text += " caller_section_rom=" + hex(frame.caller_section_rom);
        text += " caller_declared_logical_section=" + std::to_string(frame.caller_section);
        text += " source=" + logical_file(frame.file) + ":" + std::to_string(frame.line);
        if (std::strcmp(frame.phase, "executing_resolved_callable") == 0) {
            text += "\r\n  Lookup succeeded; invocation has not returned.";
            text += " Active callable=" + std::to_string(frame.identity.active_callable ? 1 : 0);
            text += " ROM mapping matches=" + std::to_string(frame.identity.match_count);
            if (frame.identity.match_count) {
                text += "\r\n  Target section ROM=" + hex(frame.identity.section_rom);
                text += " function ROM=" + hex(frame.identity.function_rom);
                text += " original_vram=" + hex(frame.identity.original_function_vram);
                text += " logical_section=" + std::to_string(frame.identity.logical_section_index);
                text += " sorted_code_section=" + std::to_string(frame.identity.code_section_index);
                text += " loaded_section_vram=" + hex(frame.identity.loaded_section_vram);
                text += " physical_section=" + hex(frame.identity.physical_section_addr);
                text += " tlb_bound=" + std::to_string(frame.identity.tlb_bound ? 1 : 0);
                if (frame.identity.match_count > 1) text += " (multiple matching contexts; primary shown)";
            } else {
                text += "\r\n  Target ROM identity unavailable (native/manual or untracked registration).";
            }
            HMODULE module = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                reinterpret_cast<LPCWSTR>(frame.identity.native_function), &module);
            if (module) {
                text += " Native function RVA=" + hex(frame.identity.native_function - reinterpret_cast<std::uintptr_t>(module), 16);
            }
        } else {
            text += "\r\n  Lookup has not been recorded as successful.";
        }
        text += "\r\n  Invocation registers (raw): A0=" + hex(frame.registers[0], 16);
        text += " A1=" + hex(frame.registers[1], 16) + " A2=" + hex(frame.registers[2], 16);
        text += " A3=" + hex(frame.registers[3], 16) + " SP=" + hex(frame.registers[4], 16) + " RA=" + hex(frame.registers[5], 16);
    }
    if (call_journal.last_completed.token) {
        const CallFrame& frame = call_journal.last_completed;
        text += "\r\nLast completed indirect call (may precede this fault): sequence=" + std::to_string(frame.token);
        text += " phase=" + std::string(frame.phase) + " target=" + hex(static_cast<std::uint32_t>(frame.target));
        text += " caller_pc=" + hex(frame.caller_pc) + " caller_rom=" + hex(frame.caller_rom);
    }
    return text;
}
#endif

void emit(const std::string& detail) {
    if (reporting.test_and_set()) {
        // Preserve the first report while the first faulting thread owns the dialog.
        Sleep(INFINITE);
        return;
    }
    SYSTEMTIME time{};
    GetSystemTime(&time);
    char stamp[80]{};
    std::snprintf(stamp, sizeof(stamp), "%04u-%02u-%02uT%02u:%02u:%02u.%03uZ",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond, time.wMilliseconds);
    std::string report = "RAGE WARS RECOMPILED CRASH REPORT v3\r\n";
    report += "Version: " + executable_version + "\r\nExecutable SHA256: " + executable_hash;
    report += "\r\nUTC: " + std::string(stamp);
#if !defined(XR64_PUBLIC_RELEASE)
    report += "\r\nHost thread: " + std::to_string(GetCurrentThreadId());
#endif
    report += "\r\n" + detail;
#if !defined(XR64_PUBLIC_RELEASE)
    report += "\r\nLast indirect lookup (may precede this crash): " + hex(static_cast<std::uint32_t>(last_lookup.target));
    report += "\r\nLookup source: " + logical_file(last_lookup.file) + ":" + std::to_string(last_lookup.line);
    report += format_call_journal();
#endif
    report += "\r\n\r\nPaste this report and what you were doing into the bug report.\r\n";
    std::error_code error;
    std::filesystem::create_directories(report_directory, error);
    char filename[100]{};
    std::snprintf(filename, sizeof(filename), "crash-%04u%02u%02u-%02u%02u%02u-%03u-%lu.txt",
        time.wYear, time.wMonth, time.wDay, time.wHour, time.wMinute, time.wSecond,
        time.wMilliseconds, GetCurrentProcessId());
    const bool archived = !error && save_text(report_directory / filename, report);
    const bool latest = !error && save_text(report_directory / L"crash-latest.txt", report);
#if defined(XR64_PUBLIC_RELEASE)
    std::wstring status = archived || latest
            ? L"Report saved in your Rage Wars profile. Use Copy Report to share it."
            : L"Could not save the report. Use Copy Report before closing.";
#else
    std::wstring status = archived || latest ? L"Report saved: " + (report_directory / (latest ? L"crash-latest.txt" : widen(filename))).wstring()
                                             : L"Could not save the report. Use Copy Report before closing.";
#endif
    // Persistent product diagnostic, not a high-frequency trace.
    std::fwrite(report.data(), 1, report.size(), stderr);
    if (display_window) show_report(report, status);
}
LONG WINAPI unhandled_exception(EXCEPTION_POINTERS* exception) {
    try {
        if (exception && exception->ExceptionRecord) {
            const auto& record = *exception->ExceptionRecord;
            HMODULE module = nullptr;
            GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                              reinterpret_cast<LPCWSTR>(record.ExceptionAddress), &module);
            std::array<wchar_t, 32768> module_path{};
            if (module) GetModuleFileNameW(module, module_path.data(), static_cast<DWORD>(module_path.size()));
#if !defined(XR64_PUBLIC_RELEASE)
            const auto address = reinterpret_cast<std::uintptr_t>(record.ExceptionAddress);
#endif
            std::string detail = "Failure: unhandled native exception\r\nException code: " + hex(record.ExceptionCode);
            detail += "\r\nFault module: " + std::filesystem::path(module_path.data()).filename().string();
#if !defined(XR64_PUBLIC_RELEASE)
            detail += "\r\nFault module RVA: " + hex(address - reinterpret_cast<std::uintptr_t>(module), 16);
            if (record.ExceptionCode == EXCEPTION_ACCESS_VIOLATION && record.NumberParameters >= 2) {
                detail += "\r\nAccess operation (0=read,1=write,8=execute): " + std::to_string(record.ExceptionInformation[0]);
                detail += "\r\nAccess address: " + hex(record.ExceptionInformation[1], 16);
            }
#endif
            detail += "\r\nGuest function at fault: unknown (native exception)\r\n";
            emit(detail);
        }
    } catch (...) {} // Best effort for native faults; never recursively unwind here.
    return EXCEPTION_EXECUTE_HANDLER;
}
}
void initialize(const std::filesystem::path& directory, bool show_window) {
    report_directory = directory;
    display_window = show_window;
    executable_hash = hash_executable();
    executable_version = executable_product_version();
    SetUnhandledExceptionFilter(unhandled_exception);
    std::set_terminate([] {
        try { emit("Failure: C++ terminate\r\nGuest function at fault: unknown\r\n"); } catch (...) {}
        TerminateProcess(GetCurrentProcess(), 3);
    });
}
#if !defined(XR64_PUBLIC_RELEASE)
void note_lookup(std::int32_t target, const char* file, int line) noexcept {
    last_lookup = {target, file, line};
}

std::uint64_t begin_indirect_call(std::int32_t target, std::uint32_t caller_pc,
    std::uint32_t caller_rom, std::uint32_t caller_function_rom, std::uint32_t caller_section_rom,
    std::uint32_t caller_section,
    const char* file, int line, const recomp_context* context) noexcept {
    call_journal.observed = true;
    const std::uint64_t token = ++call_journal.sequence;
    note_lookup(target, file, line);
    if (call_journal.depth == call_journal.frames.size()) {
        ++call_journal.overflow_depth;
        return token;
    }
    CallFrame& frame = call_journal.frames[call_journal.depth++];
    frame = {};
    frame.token = token; frame.target = target;
    frame.caller_pc = caller_pc; frame.caller_rom = caller_rom; frame.caller_section = caller_section;
    frame.caller_function_rom = caller_function_rom; frame.caller_section_rom = caller_section_rom;
    frame.file = file; frame.line = line; frame.phase = "lookup_requested";
    if (context) frame.registers = {context->r4, context->r5, context->r6, context->r7, context->r29, context->r31};
    return token;
}
void resolved_indirect_call(std::uint64_t token, const CallIdentity& identity) noexcept {
    if (!call_journal.depth || call_journal.frames[call_journal.depth - 1].token != token) return;
    CallFrame& frame = call_journal.frames[call_journal.depth - 1];
    frame.identity = identity;
    frame.phase = "executing_resolved_callable";
}
void complete_indirect_call(std::uint64_t token, bool unwound) noexcept {
    if (call_journal.overflow_depth && call_journal.depth
            && token > call_journal.frames[call_journal.depth - 1].token) {
        --call_journal.overflow_depth;
        return;
    }
    if (!call_journal.depth || call_journal.frames[call_journal.depth - 1].token != token) return;
    CallFrame& frame = call_journal.frames[call_journal.depth - 1];
    frame.phase = unwound ? "unwound" : "returned";
    call_journal.last_completed = frame;
    --call_journal.depth;
}
#endif

void missing_callable(recomp_context* context, std::int32_t target) {
#if defined(XR64_PUBLIC_RELEASE)
    (void)context;
#endif
    try {
        std::string detail = "Failure: unresolved guest callable\r\nMissing guest function: " + hex(static_cast<std::uint32_t>(target));
#if !defined(XR64_PUBLIC_RELEASE)
        if (context) {
            detail += "\r\nGuest registers (raw, not a validated stack trace):";
            detail += "\r\nA0=" + hex(context->r4, 16) + " A1=" + hex(context->r5, 16);
            detail += "\r\nA2=" + hex(context->r6, 16) + " A3=" + hex(context->r7, 16);
            detail += "\r\nSP=" + hex(context->r29, 16) + " RA=" + hex(context->r31, 16);
        }
#endif
        emit(detail + "\r\n");
    } catch (...) {}
    // A fatal guest thread cannot safely run process-wide C++ teardown while
    // other guest/audio threads are alive. Reports are flushed before closing.
    TerminateProcess(GetCurrentProcess(), EXIT_FAILURE);
}
}
