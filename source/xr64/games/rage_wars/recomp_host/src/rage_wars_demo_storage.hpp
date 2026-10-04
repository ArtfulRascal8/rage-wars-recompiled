#pragma once

#define NOMINMAX
#include <Windows.h>

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace xr64::rage_wars::demo {

// Empty one-bank Pak; this contains filesystem metadata only, no game save.
inline std::array<std::uint8_t,32768> empty_pak() {
    std::array<std::uint8_t,32768> pak{};
    std::array<std::uint8_t,32> id{};
    id[0]=0x58;id[1]=0x52;id[2]=0x36;id[3]=0x34; // Host-generated serial.
    id[25]=1;id[26]=1;
    unsigned checksum=0,inverse=0;
    for(unsigned i=0;i<28;i+=2) {
        const unsigned word=(unsigned(id[i])<<8)|id[i+1];
        checksum+=word;inverse+=word^0xFFFFU;
    }
    id[28]=std::uint8_t(checksum>>8);id[29]=std::uint8_t(checksum);
    id[30]=std::uint8_t(inverse>>8);id[31]=std::uint8_t(inverse);
    for(unsigned offset : {0x20U,0x60U,0x80U,0xC0U})
        for(unsigned i=0;i<id.size();++i)pak[offset+i]=id[i];
    for(unsigned offset : {0x100U,0x200U}) {
        for(unsigned page=5;page<128;++page)pak[offset+page*2+1]=3;
        pak[offset+1]=std::uint8_t(123*3);
    }
    return pak;
}

enum class ControllerPakState { Missing, Valid, Invalid };

inline bool valid_controller_pak_bytes(const std::uint8_t* bytes, std::size_t size) {
    if (bytes == nullptr || size != 32768 || bytes[0x20 + 26] != 1) return false;
    const auto inode_valid = [bytes](std::size_t offset) {
        unsigned sum = 0;
        for (std::size_t i = 10; i < 256; ++i) sum += bytes[offset + i];
        return static_cast<std::uint8_t>(sum) == bytes[offset + 1];
    };
    // The native runtime accepts either inode copy and repairs the other later.
    return inode_valid(0x100) || inode_valid(0x200);
}

inline ControllerPakState inspect_controller_pak(
        const std::filesystem::path& path, std::string& error) {
    error.clear();
    std::error_code ec;
    const bool exists = std::filesystem::exists(path, ec);
    if (ec) { error = "Controller Pak path could not be inspected."; return ControllerPakState::Invalid; }
    if (!exists) return ControllerPakState::Missing;
    std::ifstream file(path, std::ios::binary | std::ios::ate);
    if (!file || file.tellg() != static_cast<std::streamoff>(32768)) {
        error = "Existing Controller Pak is not exactly 32 KiB.";
        return ControllerPakState::Invalid;
    }
    std::array<std::uint8_t,32768> bytes{};
    file.seekg(0);
    if (!file.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()))) {
        error = "Existing Controller Pak could not be read completely.";
        return ControllerPakState::Invalid;
    }
    if (!valid_controller_pak_bytes(bytes.data(), bytes.size())) {
        error = "Existing Controller Pak metadata is not valid; its contents were left untouched.";
        return ControllerPakState::Invalid;
    }
    return ControllerPakState::Valid;
}

inline bool write_pak_temp(
        const std::filesystem::path& path,
        const std::array<std::uint8_t,32768>& bytes,
        std::filesystem::path& temporary,
        std::string& error) {
    for (unsigned attempt = 0; attempt < 32; ++attempt) {
        temporary = path;
        temporary += L".tmp." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(attempt);
        HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_TEMPORARY, nullptr);
        if (file == INVALID_HANDLE_VALUE) {
            if (GetLastError() == ERROR_FILE_EXISTS || GetLastError() == ERROR_ALREADY_EXISTS) continue;
            error = "Could not create a temporary Controller Pak file.";
            return false;
        }
        DWORD written = 0;
        const bool saved = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) != 0 &&
                written == bytes.size() && FlushFileBuffers(file) != 0;
        const DWORD write_error = GetLastError();
        CloseHandle(file);
        if (!saved) {
            DeleteFileW(temporary.c_str());
            error = "Temporary Controller Pak could not be written completely (Windows error " + std::to_string(write_error) + ").";
            return false;
        }
        return true;
    }
    error = "Could not reserve a unique temporary Controller Pak filename.";
    return false;
}

inline bool install_pak_temp(
        const std::filesystem::path& temporary,
        const std::filesystem::path& path,
        bool replace,
        std::string& error) {
    const DWORD flags = MOVEFILE_WRITE_THROUGH | (replace ? MOVEFILE_REPLACE_EXISTING : 0);
    if (!MoveFileExW(temporary.c_str(), path.c_str(), flags)) {
        const DWORD code = GetLastError();
        DeleteFileW(temporary.c_str());
        error = "Could not atomically install Controller Pak (Windows error " + std::to_string(code) + ").";
        return false;
    }
    return true;
}

inline bool create_empty_controller_pak_atomic(
        const std::filesystem::path& path, std::string& error) {
    error.clear();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = "Could not create the Controller Pak folder."; return false; }
    const auto state = inspect_controller_pak(path, error);
    if (state == ControllerPakState::Valid) { error = "Controller Pak already exists; it was preserved."; return false; }
    if (state == ControllerPakState::Invalid) { if (error.empty()) error = "Invalid Controller Pak was preserved."; return false; }
    const auto bytes = empty_pak();
    std::filesystem::path temporary;
    if (!write_pak_temp(path, bytes, temporary, error)) return false;
    return install_pak_temp(temporary, path, false, error);
}

inline bool save_controller_pak_atomic(
        const std::filesystem::path& path,
        const std::array<std::uint8_t,32768>& bytes,
        std::string& error) {
    error.clear();
    if (bytes.size() != 32768) {
        error = "Refusing to save a Controller Pak with invalid size.";
        return false;
    }
    std::error_code ec;
    if (!std::filesystem::is_regular_file(path, ec) || ec || std::filesystem::file_size(path, ec) != bytes.size() || ec) {
        error = "Refusing to replace a missing or invalid-sized Controller Pak.";
        return false;
    }
    std::filesystem::path temporary;
    if (!write_pak_temp(path, bytes, temporary, error)) return false;
    return install_pak_temp(temporary, path, true, error);
}

inline bool backup_and_reset_controller_pak_atomic(
        const std::filesystem::path& path, std::filesystem::path& backup, std::string& error) {
    error.clear();
    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) { error = "Could not create the Controller Pak folder."; return false; }
    const auto state = inspect_controller_pak(path, error);
    if (state == ControllerPakState::Valid) { error = "A valid Controller Pak already exists; it was preserved."; return false; }
    if (state == ControllerPakState::Invalid) {
        for (unsigned attempt = 0; attempt < 32; ++attempt) {
            backup = path;
            backup += L".backup." + std::to_wstring(GetCurrentProcessId()) + L"." + std::to_wstring(GetTickCount64()) + L"." + std::to_wstring(attempt);
            if (CopyFileW(path.c_str(), backup.c_str(), TRUE)) break;
            const DWORD code = GetLastError();
            if (code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS) continue;
            error = "Could not preserve invalid Controller Pak before recovery (Windows error " + std::to_string(code) + ").";
            return false;
        }
        if (backup.empty() || !std::filesystem::exists(backup)) {
            error = "Could not reserve a unique backup name for the existing Controller Pak.";
            return false;
        }
        std::ifstream original(path, std::ios::binary), copied(backup, std::ios::binary);
        const std::vector<char> original_bytes((std::istreambuf_iterator<char>(original)), {});
        const std::vector<char> copied_bytes((std::istreambuf_iterator<char>(copied)), {});
        if (!original || !copied || original_bytes != copied_bytes) {
            error = "Backup verification failed; the existing Controller Pak was left in place.";
            return false;
        }
    } else if (!error.empty()) {
        return false;
    }
    const auto bytes = empty_pak();
    std::filesystem::path temporary;
    if (!write_pak_temp(path, bytes, temporary, error)) return false;
    return install_pak_temp(temporary, path, state == ControllerPakState::Invalid, error);
}

}
