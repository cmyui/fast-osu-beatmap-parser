// Complete scalar/SIMD/record-layout equivalence on an existing local corpus.
// Run with sanitizers; map contents and identifiers never leave the host.
#include <fosu/parser.h>
#include <tests/support/canonical_dump.h>
#include <tests/support/scalar_engine.h>

#include <cassert>
#include <cstddef>
#include <dlfcn.h>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <string>
int main(int argc, char** argv) {
  if (argc < 2 || argc > 3)
    return 2;
  using Oracle = void (*)(const char*, size_t, std::string&);
  void* library = argc == 3 ? dlopen(argv[2], RTLD_NOW | RTLD_LOCAL) : nullptr;
  auto  oracle =
      library ? reinterpret_cast<Oracle>(dlsym(library, "fosu_numeric_oracle"))
              : nullptr;
  if (argc == 3 && !oracle) {
    std::cerr << "numeric oracle load failed\n";
    return 1;
  }
  size_t files = 0, bytes = 0, objects = 0, malformed = 0, unloadable = 0;
  fosu::Parser scalar_parser(fosu_test::scalar_engine());
  fosu::Parser simd_parser;
  for (const auto& entry :
       std::filesystem::recursive_directory_iterator(argv[1])) {
    if (entry.path().extension() != ".osu")
      continue;
    std::ifstream     file(entry.path(), std::ios::binary);
    const std::string input(std::istreambuf_iterator<char>(file), {});
    if (!file.is_open() || file.bad()) {
      std::cerr << "input read failed at file " << files << '\n';
      return 1;
    }
    // stable may refuse a map lazer reads; both engines must agree on it.
    const fosu::ParseOptions stable{.calculate_slider_end_times = true};
    const bool               stable_loads = scalar_parser.parse(input, stable);
    if (stable_loads != bool(simd_parser.parse(input, stable)) ||
        scalar_parser.error().line != simd_parser.error().line) {
      std::cerr << "stable engines disagree at file " << files << '\n';
      return 1;
    }
    unloadable += !stable_loads;
    const fosu::ParseOptions lazer{.calculate_slider_end_times = true,
                                   .client = fosu::Client::Lazer};
    auto*                    scalar = scalar_parser.parse(input, lazer);
    auto*                    simd = simd_parser.parse(input, lazer);
    if (!scalar || !simd) {
      std::cerr << "parse failed at file " << files << '\n';
      return 1;
    }
    auto a = *scalar;
    auto b = *simd;
    a.stats.fast_path_lines = a.stats.slow_path_lines = 0;
    b.stats.fast_path_lines = b.stats.slow_path_lines = 0;
    std::string x, y;
    fosu_dump::dump(a, x);
    fosu_dump::dump(b, y);
    if (x != y) {
      std::cerr << "field mismatch at file " << files << '\n';
      return 1;
    }
    if (oracle) {
      std::string expected;
      oracle(input.data(), input.size(), expected);
      if (x != expected) {
        std::cerr << "numeric oracle mismatch at file " << files << '\n';
        return 1;
      }
    }
    ++files;
    bytes += input.size();
    objects += a.hit_objects.size();
    malformed += a.stats.malformed_lines;
    if (files % 2000 == 0)
      std::cout << "verified " << files << std::endl;
  }
  std::cout << "files=" << files << " bytes=" << bytes << " objects=" << objects
            << " malformed_lines=" << malformed
            << " stable_unloadable=" << unloadable << '\n';
  if (library)
    dlclose(library);
  return files ? 0 : 1;
}
