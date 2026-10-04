#define XXH_INLINE_ALL
#include "xxhash.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <vector>

int main(int argc, char **argv) {
    if (argc != 2) {
        std::fprintf(stderr, "usage: rom_xxh3 <rom>\n");
        return 2;
    }
    std::ifstream stream(std::filesystem::path(argv[1]), std::ios::binary);
    if (!stream) return 3;
    std::vector<std::uint8_t> bytes;
    bytes.assign(std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>());
    std::printf("%016llX\n", static_cast<unsigned long long>(
            XXH3_64bits(bytes.data(), bytes.size())));
    return 0;
}
