// One-shot adapter: the same Parser and Beatmap as the library, followed by
// a bounded streaming serializer. The freestanding runtime supplies OS calls;
// it contains no parsing or beatmap storage policy.
#include "runtime.hpp"
#define FOSU_MANAGED_ARENA_CLEANUP
#define FASTFLOAT_ASSERT(x) ((void)0)
#define FASTFLOAT_DEBUG_ASSERT(x) ((void)0)
#include <fosu/parser.hpp>
#include "../tests/support/canonical_dump.hpp"

namespace {

struct Output {
    char buffer[65536];
    size_t used = 0;
    size_t total = 0;

    size_t size() const { return total; }

    void flush() {
        size_t sent = 0;
        while (sent < used) {
            const long n = rt::write(1, buffer + sent, used - sent);
            if (n == -4) continue;
            if (n <= 0) rt::exit(3);
            sent += static_cast<size_t>(n);
        }
        used = 0;
    }

    void raw(const void* data, size_t length) {
        const auto* p = static_cast<const char*>(data);
        total += length;
        while (length) {
            const size_t count = std::min(length, sizeof(buffer) - used);
            memcpy(buffer + used, p, count);
            p += count;
            used += count;
            length -= count;
            if (used == sizeof(buffer)) flush();
        }
    }

    void u8(uint8_t v) { raw(&v, sizeof(v)); }
    void u32(uint32_t v) { raw(&v, sizeof(v)); }
    void i32(int32_t v) { raw(&v, sizeof(v)); }
    void i64(int64_t v) { raw(&v, sizeof(v)); }
    void f64(double v) { raw(&v, sizeof(v)); }
    void str(std::string_view v) {
        u32(static_cast<uint32_t>(v.size()));
        raw(v.data(), v.size());
    }
};

[[noreturn]] void run(int argc, char** argv) {
    if (argc != 2) rt::exit(2);
    fosu::Parser parser;
    const auto result = parser.parse_file(argv[1]);
    if (!result) {
        switch (result.error().code) {
            case fosu::ErrorCode::InputTooLarge: rt::exit(6);
            case fosu::ErrorCode::InvalidInput: rt::exit(2);
            case fosu::ErrorCode::IoFailure:
            case fosu::ErrorCode::AllocationFailure: rt::exit(1);
        }
    }
    Output output;
    fosu_dump::dump_to(*result.value(), output);
    output.flush();
    rt::exit(0);
}

}  // namespace

void* fosu_memchr(const void* s, int c, size_t n) __asm__("memchr");
void* fosu_memchr(const void* s, int c, size_t n) {
    const char* p = static_cast<const char*>(s);
    const char* end = p + n;
    const __m256i v = _mm256_set1_epi8(static_cast<char>(c));
    while (p < end) {
        const uint32_t m = static_cast<uint32_t>(_mm256_movemask_epi8(_mm256_cmpeq_epi8(_mm256_loadu_si256(reinterpret_cast<const __m256i*>(p)), v)));
        const size_t rem = static_cast<size_t>(end - p);
        const uint32_t mm = rem >= 32 ? m : (m & ((1u << rem) - 1));
        if (mm) return const_cast<char*>(p + _tzcnt_u32(mm));
        p += 32;
    }
    return nullptr;
}

extern "C" [[noreturn]] void __assert_fail(const char*, const char*, unsigned, const char*) { rt::exit(5); }

extern "C" [[noreturn]] void main_entry(long* sp) {
    run(static_cast<int>(sp[0]), reinterpret_cast<char**>(sp + 1));
}

__asm__(R"(
.text
.global _start
_start:
    xor %rbp, %rbp
    mov %rsp, %rdi
    and $-16, %rsp
    call main_entry
    hlt
)");
