// A fresh process reads one file, then times its first C++ parse.
// Input I/O, result destruction and process startup are outside the interval.
#include <chrono>
#include <cstdio>
#include <fosu/parser.h>
int main(int argc, char** argv) {
    if (argc != 2) return 2;
    auto input = fosu::read_file_padded(argv[1]);
    if (!input) return 1;
    const auto start = std::chrono::steady_clock::now();
    fosu::Parser parser;
    auto result = parser.parse(input);
    if (!result) return 1;
    __asm__ volatile("" : : "g"(result.value()) : "memory");
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now() - start).count();
    printf("%zu %lld\n", input.size, static_cast<long long>(ns));
}
