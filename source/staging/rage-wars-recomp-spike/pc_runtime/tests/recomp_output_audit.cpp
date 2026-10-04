#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {
// Owner-preserved checkpoint 5a69d543a31f2154c47e69013ca239e43e1897d1:
// 55 funcs_*.c units contain 3,012 definitions; section count is a different metric.
// Plus pak_menu (6), turret_transform (1), turret_lifecycle (32):
// scripts/*_coverage_group.json pin ROM/snapshot provenance for each addition.
constexpr std::size_t kExpectedFunctions = 3012 + 6 + 1 + 32 + 4;
constexpr std::size_t kExpectedTranslationUnits = 55;

std::size_t count_markers(const std::filesystem::path &file) {
    std::ifstream input(file, std::ios::binary);
    std::string contents((std::istreambuf_iterator<char>(input)), {});
    constexpr std::string_view marker = "RECOMP_FUNC void ";
    std::size_t count = 0;
    std::size_t offset = 0;
    while ((offset = contents.find(marker, offset)) != std::string::npos) {
        ++count;
        offset += marker.size();
    }
    return count;
}
} // namespace

int main(int argc, char **argv) {
    if (argc != 2) {
        std::cerr << "usage: rage_wars_recomp_count <generated-dir>\n";
        return 2;
    }
    const std::filesystem::path generated_dir(argv[1]);
    std::size_t translation_units = 0;
    std::size_t functions = 0;
    for (const auto &entry : std::filesystem::directory_iterator(generated_dir)) {
        const std::string name = entry.path().filename().string();
        if (!entry.is_regular_file() || !name.starts_with("funcs_") ||
                entry.path().extension() != ".c") {
            continue;
        }
        ++translation_units;
        functions += count_markers(entry.path());
    }
    std::cout << "generated_translation_units=" << translation_units
              << " recomp_func_definitions=" << functions << '\n';
    return functions == kExpectedFunctions &&
            translation_units == kExpectedTranslationUnits ? 0 : 1;
}
