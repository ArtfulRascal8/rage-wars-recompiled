#pragma once
#define XXH_INLINE_ALL
#include "xxhash.h"
#include <algorithm>
#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace xr64::rage_wars::demo {
constexpr std::size_t rom_size=8U*1024U*1024U;
constexpr std::uint64_t rom_hash=0x21667BF153183FA0ULL;
inline bool normalize_rom(std::vector<std::uint8_t>& bytes,std::string& error) {
    error.clear();
    if(bytes.size()!=rom_size) {
        error="Select an 8 MiB Turok: Rage Wars US v1.0 ROM.";return false;
    }
    const std::array<std::uint8_t,4> magic{0x80,0x37,0x12,0x40};
    unsigned order=4;
    for(unsigned candidate : {0U,1U,3U})
        if(bytes[0]==magic[0^candidate] && bytes[1]==magic[1^candidate] &&
           bytes[2]==magic[2^candidate] && bytes[3]==magic[3^candidate])order=candidate;
    if(order==4) {error="This file is not an N64 ROM. Choose a .z64, .v64, or .n64 file.";return false;}
    if(order) for(std::size_t i=0;i<bytes.size();i+=4) {
        const std::array<std::uint8_t,4> word{bytes[i],bytes[i+1],bytes[i+2],bytes[i+3]};
        for(unsigned j=0;j<4;++j)bytes[i+j]=word[j^order];
    }
    if(XXH3_64bits(bytes.data(),bytes.size())!=rom_hash) {
        error="This demo needs the unmodified US v1.0 release of Turok: Rage Wars. This ROM does not match.";return false;
    }
    return true;
}
inline bool load_rom(const std::filesystem::path& path,std::vector<std::uint8_t>& bytes,std::string& error) {
    bytes.clear();
    std::ifstream file(path,std::ios::binary|std::ios::ate);
    if(!file){error="The ROM could not be opened. Select it again.";return false;}
    if(file.tellg()!=static_cast<std::streamoff>(rom_size)) {
        error="Select an 8 MiB Turok: Rage Wars US v1.0 ROM.";return false;
    }
    bytes.resize(rom_size);file.seekg(0);
    if(!file.read(reinterpret_cast<char*>(bytes.data()),static_cast<std::streamsize>(bytes.size()))) {
        bytes.clear();error="The ROM could not be read completely.";return false;
    }
    return normalize_rom(bytes,error);
}
}
