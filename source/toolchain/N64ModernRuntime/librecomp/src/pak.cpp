#include "ultramodern/ultra64.h"
#include "ultramodern/ultramodern.hpp"

#include "recomp.h"
#include "helpers.hpp"

#include <array>
#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <vector>
#include <iomanip>
#include <sstream>

namespace {

constexpr s32 PFS_ERR_NOPACK = 1;
constexpr s32 PFS_ERR_INCONSISTENT = 3;
constexpr s32 PFS_ERR_CONTRFAIL = 4;
constexpr s32 PFS_ERR_INVALID = 5;
constexpr s32 PFS_ERR_BAD_DATA = 6;
constexpr s32 PFS_DATA_FULL = 7;
constexpr s32 PFS_DIR_FULL = 8;
constexpr s32 PFS_ERR_EXIST = 9;
constexpr s32 PFS_ERR_DEVICE = 11;
constexpr int PFS_INITIALIZED = 1;
constexpr u8 PFS_READ = 0;
constexpr u8 PFS_WRITE = 1;
constexpr std::size_t kPakSize = 32 * 1024;
constexpr std::size_t kBlockSize = 32;
constexpr std::size_t kPageSize = 256;
constexpr std::size_t kInodeOffset = 0x100;
constexpr std::size_t kMirrorInodeOffset = 0x200;
constexpr std::size_t kDirectoryOffset = 0x300;
constexpr int kDirectoryEntries = 16;
constexpr int kFirstDataPage = 5;
constexpr int kPageCount = 128;
constexpr u16 kPageEnd = 1;
constexpr u16 kPageFree = 3;
constexpr u8 kDirectoryOccupied = 2;

#if defined(XR64_RAGE_WARS_CLEAN_RUN)
struct Rw017ApiScope {
    Rw017ApiScope(const char*, recomp_context*) {}
};
#else
std::atomic<unsigned long long> g_rw017_pfs_sequence{0};

bool rw017_trace_enabled() {
    const char* value = std::getenv("XR64_RW017_TRACE");
    return value != nullptr && value[0] == '1';
}

struct Rw017ApiScope {
    const char* operation;
    recomp_context* ctx;
    unsigned long long sequence;

    Rw017ApiScope(const char* operation_, recomp_context* ctx_)
        : operation(operation_), ctx(ctx_), sequence(++g_rw017_pfs_sequence) {
        ultramodern::runtime_trace(
                "stage=pfs-entry operation=" + std::string(operation) +
                " sequence=" + std::to_string(sequence) +
                " thread=0x" + std::to_string(static_cast<unsigned>(ultramodern::this_thread())) +
                " ra=0x" + std::to_string(static_cast<unsigned>(ctx->r31)));
        if (!rw017_trace_enabled()) return;
        std::fprintf(stderr,
                "RW017_GUEST_PFS sequence=%llu phase=entry source=guest_or_host_selftest operation=%s ra=0x%08X\n",
                sequence, operation, static_cast<unsigned>(ctx->r31));
        std::fflush(stderr);
    }

    ~Rw017ApiScope() {
        ultramodern::runtime_trace(
                "stage=pfs-return operation=" + std::string(operation) +
                " sequence=" + std::to_string(sequence) +
                " thread=0x" + std::to_string(static_cast<unsigned>(ultramodern::this_thread())) +
                " result=" + std::to_string(static_cast<long long>(ctx->r2)) +
                " ra=0x" + std::to_string(static_cast<unsigned>(ctx->r31)));
        if (!rw017_trace_enabled()) return;
        std::fprintf(stderr,
                "RW017_GUEST_PFS sequence=%llu phase=exit source=guest_or_host_selftest operation=%s ra=0x%08X result=%lld\n",
                sequence, operation, static_cast<unsigned>(ctx->r31), static_cast<long long>(ctx->r2));
        std::fflush(stderr);
    }
};
#endif

struct DirectoryEntry {
    std::array<u8, 32> raw{};

    u32 game_code() const {
        return (u32(raw[0]) << 24) | (u32(raw[1]) << 16) | (u32(raw[2]) << 8) | raw[3];
    }
    u16 company_code() const { return u16((u16(raw[4]) << 8) | raw[5]); }
    u16 start_page() const { return u16((u16(raw[6]) << 8) | raw[7]); }
    bool occupied() const { return (raw[8] & kDirectoryOccupied) != 0; }
    bool used() const { return company_code() != 0 && game_code() != 0; }
};

u16 read_be16(const u8* data) {
    return u16((u16(data[0]) << 8) | data[1]);
}

void write_be16(u8* data, u16 value) {
    data[0] = u8(value >> 8);
    data[1] = u8(value);
}

void write_be32(u8* data, u32 value) {
    data[0] = u8(value >> 24);
    data[1] = u8(value >> 16);
    data[2] = u8(value >> 8);
    data[3] = u8(value);
}

bool controller_pak_present(int channel) {
    const auto info = ultramodern::input::get_connected_device_info(channel);
    return info.connected_device == ultramodern::input::Device::Controller &&
            info.connected_pak == ultramodern::input::Pak::ControllerPak;
}

s32 availability_error(int channel) {
    const auto info = ultramodern::input::get_connected_device_info(channel);
    if (info.connected_device != ultramodern::input::Device::Controller) return PFS_ERR_CONTRFAIL;
    if (info.connected_pak == ultramodern::input::Pak::None) return PFS_ERR_NOPACK;
    if (info.connected_pak != ultramodern::input::Pak::ControllerPak) return PFS_ERR_DEVICE;
    return 0;
}

bool read_pak(int channel, std::size_t offset, void* data, std::size_t size) {
    const bool ok = offset <= kPakSize && size <= kPakSize - offset &&
            ultramodern::input::read_controller_pak(
                    channel, offset, static_cast<u8*>(data), size);
    ultramodern::runtime_trace(
            "stage=pak-io operation=read channel=" + std::to_string(channel) +
            " offset=" + std::to_string(offset) +
            " size=" + std::to_string(size) +
            " result=" + std::to_string(ok ? 0 : 1));
    return ok;
}
bool write_pak(int channel, std::size_t offset, const void* data, std::size_t size) {
    const bool ok = offset <= kPakSize && size <= kPakSize - offset &&
            ultramodern::input::write_controller_pak(
                    channel, offset, static_cast<const u8*>(data), size);
    ultramodern::runtime_trace(
            "stage=pak-io operation=write channel=" + std::to_string(channel) +
            " offset=" + std::to_string(offset) +
            " size=" + std::to_string(size) +
            " result=" + std::to_string(ok ? 0 : 1));
    return ok;
}
bool initialized(const OSPfs* pfs) {
    return pfs != nullptr && (pfs->status & PFS_INITIALIZED) != 0 &&
            controller_pak_present(pfs->channel);
}

bool read_directory(int channel, int index, DirectoryEntry& entry) {
    return index >= 0 && index < kDirectoryEntries &&
            read_pak(channel, kDirectoryOffset + std::size_t(index) * kBlockSize,
                    entry.raw.data(), entry.raw.size());
}

bool write_directory(int channel, int index, const DirectoryEntry& entry) {
    return index >= 0 && index < kDirectoryEntries &&
            write_pak(channel, kDirectoryOffset + std::size_t(index) * kBlockSize,
                    entry.raw.data(), entry.raw.size());
}

bool inode_checksum_valid(const std::array<u8, kPageSize>& raw) {
    u32 sum = 0;
    for (std::size_t i = kFirstDataPage * 2; i < raw.size(); ++i) sum += raw[i];
    return u8(sum) == raw[1];
}

bool read_inode_copy(int channel, std::size_t offset, std::array<u8, kPageSize>& raw) {
    return read_pak(channel, offset, raw.data(), raw.size());
}

s32 read_inode(int channel, std::array<u16, kPageCount>& inode) {
    std::array<u8, kPageSize> primary{};
    if (!read_inode_copy(channel, kInodeOffset, primary)) return availability_error(channel);
    if (!inode_checksum_valid(primary)) {
        std::array<u8, kPageSize> mirror{};
        if (!read_inode_copy(channel, kMirrorInodeOffset, mirror)) return availability_error(channel);
        if (!inode_checksum_valid(mirror)) return PFS_ERR_INCONSISTENT;
        primary = mirror;
        if (!write_pak(channel, kInodeOffset, primary.data(), primary.size())) {
            return availability_error(channel);
        }
    }
    for (int i = 0; i < kPageCount; ++i) inode[i] = read_be16(primary.data() + i * 2);
    return 0;
}

s32 write_inode(int channel, const std::array<u16, kPageCount>& inode) {
    std::array<u8, kPageSize> raw{};
    for (int i = 0; i < kPageCount; ++i) write_be16(raw.data() + i * 2, inode[i]);
    u32 sum = 0;
    for (std::size_t i = kFirstDataPage * 2; i < raw.size(); ++i) sum += raw[i];
    raw[1] = u8(sum);
    if (!write_pak(channel, kInodeOffset, raw.data(), raw.size()) ||
            !write_pak(channel, kMirrorInodeOffset, raw.data(), raw.size())) {
        return availability_error(channel);
    }
    return 0;
}

s32 collect_file_pages(const OSPfs* pfs, const DirectoryEntry& entry, std::vector<u8>& pages) {
    std::array<u16, kPageCount> inode{};
    s32 ret = read_inode(pfs->channel, inode);
    if (ret != 0) return ret;

    u16 current = entry.start_page();
    std::array<bool, kPageCount> seen{};
    while (current != kPageEnd) {
        const u8 bank = u8(current >> 8);
        const u8 page = u8(current);
        if (bank != 0 || page < kFirstDataPage || page >= kPageCount || seen[page]) {
            return PFS_ERR_INCONSISTENT;
        }
        seen[page] = true;
        pages.push_back(page);
        current = inode[page];
    }
    return pages.empty() ? PFS_ERR_INCONSISTENT : 0;
}

bool guest_bytes_equal(uint8_t* rdram, gpr guest, const u8* host, std::size_t size) {
    if (guest == 0) return true;
    for (std::size_t i = 0; i < size; ++i) {
        if (u8(MEM_BU(i, guest)) != host[i]) return false;
    }
    return true;
}

void guest_store_bytes(uint8_t* rdram, gpr guest, const u8* host, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) MEM_B(i, guest) = host[i];
    std::ostringstream detail;
    detail << "stage=pfs-memory-write guest=0x" << std::uppercase << std::hex
           << static_cast<unsigned>(guest) << " bytes=" << std::dec << size << " data=";
    for (std::size_t i = 0; i < std::min<std::size_t>(size, 32); ++i) {
        if (i != 0) detail << ' ';
        detail << std::setw(2) << std::setfill('0') << static_cast<unsigned>(host[i]);
    }
    ultramodern::runtime_trace(detail.str());
}
void guest_load_bytes(uint8_t* rdram, gpr guest, u8* host, std::size_t size) {
    for (std::size_t i = 0; i < size; ++i) host[i] = u8(MEM_BU(i, guest));
}

s32 find_file(
        uint8_t* rdram, OSPfs* pfs, u16 company, u32 game, gpr game_name, gpr ext_name,
        int& file_no) {
    if (!initialized(pfs)) return availability_error(pfs ? pfs->channel : 0);
    for (int i = 0; i < kDirectoryEntries; ++i) {
        DirectoryEntry entry{};
        if (!read_directory(pfs->channel, i, entry)) return availability_error(pfs->channel);
        if (entry.company_code() == company && entry.game_code() == game &&
                guest_bytes_equal(rdram, game_name, entry.raw.data() + 16, 16) &&
                guest_bytes_equal(rdram, ext_name, entry.raw.data() + 12, 4)) {
            file_no = i;
            return 0;
        }
    }
    file_no = -1;
    return PFS_ERR_INVALID;
}

} // namespace

extern "C" void osPfsInitPak_recomp(uint8_t* rdram, recomp_context* ctx) {
    Rw017ApiScope rw017_scope{"osPfsInitPak", ctx};
    OSPfs* pfs = _arg<1, OSPfs*>(rdram, ctx);
    const int channel = _arg<2, s32>(rdram, ctx);
    const s32 available = availability_error(channel);
    if (pfs == nullptr || channel < 0 || channel >= 4 || available != 0) {
        _return<s32>(ctx, pfs == nullptr || channel < 0 || channel >= 4 ? PFS_ERR_INVALID : available);
        return;
    }

    std::array<u8, 32> id{};
    std::array<u8, 32> label{};
    if (!read_pak(channel, 0x20, id.data(), id.size()) ||
            !read_pak(channel, 0xE0, label.data(), label.size()) || id[26] != 1) {
        _return<s32>(ctx, PFS_ERR_DEVICE);
        return;
    }

    pfs->status = PFS_INITIALIZED;
    pfs->queue = static_cast<PTR(OSMesgQueue)>(ctx->r4);
    pfs->channel = channel;
    pfs->version = id[27];
    pfs->dir_size = kDirectoryEntries;
    pfs->inode_table = 8;
    pfs->minode_table = 16;
    pfs->dir_table = 24;
    pfs->inode_start_page = kFirstDataPage;
    pfs->activebank = 0;
    pfs->banks = 1;
    guest_store_bytes(rdram, ctx->r5 + 12, id.data(), id.size());
    guest_store_bytes(rdram, ctx->r5 + 44, label.data(), label.size());

    std::array<u16, kPageCount> inode{};
    const s32 ret = read_inode(channel, inode);
    std::fprintf(stderr, "RW_G2_PAK_INIT channel=%d result=%d banks=1 size=%zu\n",
            channel, ret, kPakSize);
    std::fflush(stderr);
    _return<s32>(ctx, ret);
}

extern "C" void osPfsFreeBlocks_recomp(uint8_t* rdram, recomp_context* ctx) {
    Rw017ApiScope rw017_scope{"osPfsFreeBlocks", ctx};
    OSPfs* pfs = _arg<0, OSPfs*>(rdram, ctx);
    if (!initialized(pfs)) {
        _return<s32>(ctx, availability_error(pfs ? pfs->channel : 0));
        return;
    }
    std::array<u16, kPageCount> inode{};
    const s32 ret = read_inode(pfs->channel, inode);
    if (ret != 0) { _return<s32>(ctx, ret); return; }
    s32 free_pages = 0;
    for (int page = kFirstDataPage; page < kPageCount; ++page) {
        if (inode[page] == kPageFree) ++free_pages;
    }
    MEM_W(0, ctx->r5) = free_pages * int(kPageSize);
    ultramodern::runtime_trace(
            "stage=pfs-memory-write guest=0x" + std::to_string(static_cast<unsigned>(ctx->r5)) +
            " bytes=4 free_bytes=" + std::to_string(free_pages * int(kPageSize)));
    std::fprintf(stderr, "RW_G2_PAK_FREE_BLOCKS result=0 bytes=%d\n",
            free_pages * int(kPageSize));
    std::fflush(stderr);
    _return<s32>(ctx, 0);
}

extern "C" void osPfsAllocateFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    Rw017ApiScope rw017_scope{"osPfsAllocateFile", ctx};
    OSPfs* pfs = _arg<0, OSPfs*>(rdram, ctx);
    const u16 company = _arg<1, u16>(rdram, ctx);
    const u32 game = _arg<2, u32>(rdram, ctx);
    const gpr game_name = ctx->r7;
    const gpr ext_name = MEM_W(16, ctx->r29);
    const s32 byte_count = MEM_W(20, ctx->r29);
    const gpr file_no_out = MEM_W(24, ctx->r29);
    if (!initialized(pfs) || company == 0 || game == 0 || byte_count <= 0) {
        _return<s32>(ctx, initialized(pfs) ? PFS_ERR_INVALID : availability_error(pfs ? pfs->channel : 0));
        return;
    }
    int existing = -1;
    const s32 existing_ret = find_file(rdram, pfs, company, game, game_name, ext_name, existing);
    if (existing_ret == 0) { MEM_W(0, file_no_out) = existing; _return<s32>(ctx, PFS_ERR_EXIST); return; }

    int free_dir = -1;
    for (int i = 0; i < kDirectoryEntries; ++i) {
        DirectoryEntry entry{};
        if (!read_directory(pfs->channel, i, entry)) { _return<s32>(ctx, availability_error(pfs->channel)); return; }
        if (!entry.used()) { free_dir = i; break; }
    }
    if (free_dir < 0) { _return<s32>(ctx, PFS_DIR_FULL); return; }

    std::array<u16, kPageCount> inode{};
    s32 ret = read_inode(pfs->channel, inode);
    if (ret != 0) { _return<s32>(ctx, ret); return; }
    const int pages_needed = (byte_count + int(kPageSize) - 1) / int(kPageSize);
    std::vector<u8> pages;
    for (int page = kFirstDataPage; page < kPageCount && int(pages.size()) < pages_needed; ++page) {
        if (inode[page] == kPageFree) pages.push_back(u8(page));
    }
    if (int(pages.size()) != pages_needed) { _return<s32>(ctx, PFS_DATA_FULL); return; }
    for (std::size_t i = 0; i < pages.size(); ++i) {
        inode[pages[i]] = i + 1 < pages.size() ? u16(pages[i + 1]) : kPageEnd;
    }
    ret = write_inode(pfs->channel, inode);
    if (ret != 0) { _return<s32>(ctx, ret); return; }

    DirectoryEntry entry{};
    write_be32(entry.raw.data(), game);
    write_be16(entry.raw.data() + 4, company);
    entry.raw[6] = 0;
    entry.raw[7] = pages.front();
    guest_load_bytes(rdram, ext_name, entry.raw.data() + 12, 4);
    guest_load_bytes(rdram, game_name, entry.raw.data() + 16, 16);
    if (!write_directory(pfs->channel, free_dir, entry)) { _return<s32>(ctx, availability_error(pfs->channel)); return; }
    MEM_W(0, file_no_out) = free_dir;
    _return<s32>(ctx, 0);
}

extern "C" void osPfsDeleteFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    Rw017ApiScope rw017_scope{"osPfsDeleteFile", ctx};
    OSPfs* pfs = _arg<0, OSPfs*>(rdram, ctx);
    int file_no = -1;
    const s32 ret = find_file(rdram, pfs, _arg<1, u16>(rdram, ctx), _arg<2, u32>(rdram, ctx),
            ctx->r7, MEM_W(16, ctx->r29), file_no);
    if (ret != 0) { _return<s32>(ctx, ret); return; }
    DirectoryEntry entry{};
    if (!read_directory(pfs->channel, file_no, entry)) { _return<s32>(ctx, availability_error(pfs->channel)); return; }
    std::vector<u8> pages;
    const s32 pages_ret = collect_file_pages(pfs, entry, pages);
    if (pages_ret != 0) { _return<s32>(ctx, pages_ret); return; }
    std::array<u16, kPageCount> inode{};
    const s32 inode_ret = read_inode(pfs->channel, inode);
    if (inode_ret != 0) { _return<s32>(ctx, inode_ret); return; }
    for (u8 page : pages) inode[page] = kPageFree;
    const s32 write_ret = write_inode(pfs->channel, inode);
    if (write_ret != 0) { _return<s32>(ctx, write_ret); return; }
    entry.raw.fill(0);
    _return<s32>(ctx, write_directory(pfs->channel, file_no, entry) ? 0 : availability_error(pfs->channel));
}

extern "C" void osPfsFileState_recomp(uint8_t* rdram, recomp_context* ctx) {
    Rw017ApiScope rw017_scope{"osPfsFileState", ctx};
    OSPfs* pfs = _arg<0, OSPfs*>(rdram, ctx);
    const int file_no = _arg<1, s32>(rdram, ctx);
    const gpr state = ctx->r6;
    if (!initialized(pfs) || file_no < 0 || file_no >= kDirectoryEntries || state == 0) {
        _return<s32>(ctx, initialized(pfs) ? PFS_ERR_INVALID : availability_error(pfs ? pfs->channel : 0));
        return;
    }
    DirectoryEntry entry{};
    if (!read_directory(pfs->channel, file_no, entry)) { _return<s32>(ctx, availability_error(pfs->channel)); return; }
    if (!entry.used()) { _return<s32>(ctx, PFS_ERR_INVALID); return; }
    std::vector<u8> pages;
    const s32 ret = collect_file_pages(pfs, entry, pages);
    if (ret != 0) { _return<s32>(ctx, ret); return; }
    MEM_W(0, state) = s32(pages.size() * kPageSize);
    MEM_W(4, state) = s32(entry.game_code());
    MEM_H(8, state) = s16(entry.company_code());
    guest_store_bytes(rdram, state + 12, entry.raw.data() + 12, 4);
    guest_store_bytes(rdram, state + 16, entry.raw.data() + 16, 16);
    _return<s32>(ctx, 0);
}

extern "C" void osPfsFindFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    Rw017ApiScope rw017_scope{"osPfsFindFile", ctx};
    OSPfs* pfs = _arg<0, OSPfs*>(rdram, ctx);
    int file_no = -1;
    const s32 ret = find_file(rdram, pfs, _arg<1, u16>(rdram, ctx), _arg<2, u32>(rdram, ctx),
            ctx->r7, MEM_W(16, ctx->r29), file_no);
    const gpr out = MEM_W(20, ctx->r29);
    if (out != 0) MEM_W(0, out) = file_no;
    _return<s32>(ctx, ret);
}

extern "C" void osPfsReadWriteFile_recomp(uint8_t* rdram, recomp_context* ctx) {
    Rw017ApiScope rw017_scope{"osPfsReadWriteFile", ctx};
    OSPfs* pfs = _arg<0, OSPfs*>(rdram, ctx);
    const int file_no = _arg<1, s32>(rdram, ctx);
    const u8 flag = _arg<2, u8>(rdram, ctx);
    const int offset = _arg<3, s32>(rdram, ctx);
    const int size = MEM_W(16, ctx->r29);
    const gpr data = MEM_W(20, ctx->r29);
    if (!initialized(pfs) || file_no < 0 || file_no >= kDirectoryEntries ||
            (flag != PFS_READ && flag != PFS_WRITE) || offset < 0 || size <= 0 ||
            offset % int(kBlockSize) != 0 || size % int(kBlockSize) != 0 || data == 0) {
        _return<s32>(ctx, initialized(pfs) ? PFS_ERR_INVALID : availability_error(pfs ? pfs->channel : 0));
        return;
    }
    DirectoryEntry entry{};
    if (!read_directory(pfs->channel, file_no, entry)) { _return<s32>(ctx, availability_error(pfs->channel)); return; }
    if (!entry.used()) { _return<s32>(ctx, PFS_ERR_INVALID); return; }
    if (flag == PFS_READ && !entry.occupied()) { _return<s32>(ctx, PFS_ERR_BAD_DATA); return; }
    std::vector<u8> pages;
    const s32 ret = collect_file_pages(pfs, entry, pages);
    if (ret != 0) { _return<s32>(ctx, ret); return; }
    if (std::size_t(offset) + std::size_t(size) > pages.size() * kPageSize) {
        _return<s32>(ctx, PFS_ERR_INVALID);
        return;
    }

    std::array<u8, kBlockSize> block{};
    for (int position = 0; position < size; position += int(kBlockSize)) {
        const std::size_t file_offset = std::size_t(offset + position);
        const std::size_t page_index = file_offset / kPageSize;
        const std::size_t block_offset = file_offset % kPageSize;
        const std::size_t pak_offset = std::size_t(pages[page_index]) * kPageSize + block_offset;
        bool ok = false;
        if (flag == PFS_READ) {
            ok = read_pak(pfs->channel, pak_offset, block.data(), block.size());
            if (ok) guest_store_bytes(rdram, data + position, block.data(), block.size());
        } else {
            guest_load_bytes(rdram, data + position, block.data(), block.size());
            ok = write_pak(pfs->channel, pak_offset, block.data(), block.size());
        }
        if (!ok) { _return<s32>(ctx, availability_error(pfs->channel)); return; }
    }
    if (flag == PFS_WRITE && !entry.occupied()) {
        entry.raw[8] |= kDirectoryOccupied;
        if (!write_directory(pfs->channel, file_no, entry)) { _return<s32>(ctx, availability_error(pfs->channel)); return; }
    }
    std::fprintf(stderr, "RW_G2_PAK_FILE_IO result=0 operation=%s file=%d offset=%d size=%d\n",
            flag == PFS_READ ? "read" : "write", file_no, offset, size);
    std::fflush(stderr);
    _return<s32>(ctx, 0);
}

extern "C" void osPfsChecker_recomp(uint8_t* rdram, recomp_context* ctx) {
    Rw017ApiScope rw017_scope{"osPfsChecker", ctx};
    OSPfs* pfs = _arg<0, OSPfs*>(rdram, ctx);
    if (!initialized(pfs)) {
        _return<s32>(ctx, availability_error(pfs ? pfs->channel : 0));
        return;
    }
    std::array<u16, kPageCount> inode{};
    _return<s32>(ctx, read_inode(pfs->channel, inode));
}

extern "C" void osPfsNumFiles_recomp(uint8_t* rdram, recomp_context* ctx) {
    Rw017ApiScope rw017_scope{"osPfsNumFiles", ctx};
    OSPfs* pfs = _arg<0, OSPfs*>(rdram, ctx);
    if (!initialized(pfs)) {
        _return<s32>(ctx, availability_error(pfs ? pfs->channel : 0));
        return;
    }
    s32 used = 0;
    for (int i = 0; i < kDirectoryEntries; ++i) {
        DirectoryEntry entry{};
        if (!read_directory(pfs->channel, i, entry)) { _return<s32>(ctx, availability_error(pfs->channel)); return; }
        if (entry.used()) ++used;
    }
    MEM_W(0, ctx->r5) = kDirectoryEntries;
    MEM_W(0, ctx->r6) = used;
    std::fprintf(stderr, "RW_G2_PAK_NUM_FILES result=0 max=%d used=%d\n",
            kDirectoryEntries, used);
    std::fflush(stderr);
    _return<s32>(ctx, 0);
}

extern "C" void osPfsRepairId_recomp(uint8_t* rdram, recomp_context* ctx) {
    Rw017ApiScope rw017_scope{"osPfsRepairId", ctx};
    OSPfs* pfs = _arg<0, OSPfs*>(rdram, ctx);
    _return<s32>(ctx, initialized(pfs) ? 0 : availability_error(pfs ? pfs->channel : 0));
}
