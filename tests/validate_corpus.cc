// Complete scalar/SIMD/record-layout equivalence on an existing local corpus.
// Run with sanitizers; map contents and identifiers never leave the host.
#include <cassert>
#include <filesystem>
#include <iostream>
#include <dlfcn.h>
#include <fosu/parser.h>
#include "support/canonical_dump.h"
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3) return 2;
    using Oracle = void(*)(const char*, size_t, std::string&);
    void* library = argc == 3 ? dlopen(argv[2], RTLD_NOW | RTLD_LOCAL) : nullptr;
    auto oracle = library ? reinterpret_cast<Oracle>(dlsym(library, "fosu_numeric_oracle")) : nullptr;
    if (argc == 3 && !oracle) { std::cerr << "numeric oracle load failed\n"; return 1; }
    size_t files = 0, bytes = 0, objects = 0, malformed = 0;
    fosu::Parser scalar_parser(fosu::internal::scalar_engine);
    fosu::Parser simd_parser;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(argv[1])) {
        if (entry.path().extension() != ".osu") continue;
        auto input = fosu::read_file_padded(entry.path().c_str());
        if (!input) { std::cerr << "input read failed at file " << files << '\n'; return 1; }
        auto scalar = scalar_parser.parse(input);
        auto simd = simd_parser.parse(input);
        if (!scalar || !simd) {
            std::cerr << "parse failed at file " << files << '\n';
            return 1;
        }
        auto a = *scalar.value();
        auto b = *simd.value();
        a.stats.fast_path_lines = a.stats.slow_path_lines = 0;
        b.stats.fast_path_lines = b.stats.slow_path_lines = 0;
        std::string x, y;
        fosu_dump::dump(a, x); fosu_dump::dump(b, y);
        if (x != y) { std::cerr << "field mismatch at file " << files << '\n'; return 1; }
        if (oracle) {
            std::string expected;
            oracle(input.data.get(), input.size, expected);
            if (x != expected) { std::cerr << "numeric oracle mismatch at file " << files << '\n'; return 1; }
        }
        ++files; bytes += input.size; objects += a.hit_objects.size();
        malformed += a.stats.malformed_lines;
        if (files % 2000 == 0) std::cout << "verified " << files << std::endl;
    }
    std::cout << "files=" << files << " bytes=" << bytes << " objects=" << objects
              << " malformed_lines=" << malformed << '\n';
    if (library) dlclose(library);
    return files ? 0 : 1;
}
