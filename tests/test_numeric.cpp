#include "support/test.hpp"

// Independent numeric oracle: libc conversion over a bounded copy, rather
// than a second copy of the parser's mantissa arithmetic.
static const char* reference_parse_double(const char* p, const char* end,
                                          double& out) {
    std::string bounded(p, end);
    char* next;
    out = strtod(bounded.c_str(), &next);
    return std::isfinite(out) ? p + (next - bounded.c_str()) : p;
}

static uint64_t rng_state = 0x243F6A8885A308D3ull;
static uint64_t rng() {
    rng_state += 0x9E3779B97F4A7C15ull;
    uint64_t z = rng_state;
    z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
    z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
    return z ^ (z >> 31);
}

// Compare decimal conversion against libc, including significands that
// require correct rounding rather than rounding an intermediate integer.
static void test_fuzz_parse_double() {
    char buf[96];
    for (int iter = 0; iter < 300000; ++iter) {
        const int int_digits = 1 + (int)(rng() % 9);
        const int frac_digits = (int)(rng() % 8);
        int len = 0;
        if (rng() % 3 == 0) buf[len++] = '-';
        for (int i = 0; i < int_digits; ++i)
            buf[len++] = char('0' + (i == 0 ? rng() % 9 + (int_digits > 1)
                                            : rng() % 10));
        if (frac_digits || rng() % 4 == 0) {
            buf[len++] = '.';
            for (int i = 0; i < frac_digits; ++i)
                buf[len++] = char('0' + rng() % 10);
        }
        const char* tail = ",4,2\r\n";
        const int payload = len;
        for (const char* t = tail; *t; ++t) buf[len++] = *t;
        memset(buf + len, 0, sizeof(buf) - (size_t)len);

        double got = -1, want = -2;
        const char* gp =
            fosu::internal::parse_double(buf, buf + payload, got);
        const char* wp = reference_parse_double(buf, buf + payload, want);
        CHECK_EQ(gp - buf, wp - buf);
        CHECK(got == want);
        if (g_failures) {
            printf("  failing double: %.*s\n", payload, buf);
            return;
        }
    }
    // The bounded fast_float fallback must agree with the independent libc result.
    const char* long_cases[] = {"342.857142857142857142857",
                                "123456789012345678901", "0.6999999999999999556"};
    for (const char* c : long_cases) {
        std::string padded(c);
        padded.append(64, '\0');
        double got = 0;
        fosu::internal::parse_double(padded.data(), padded.data() + strlen(c), got);
        CHECK(got == strtod(c, nullptr));
    }
}

// Fuzz the SWAR slider coordinate parser against the general path.
static void test_fuzz_parse_coord() {
    char buf[64];
    for (int iter = 0; iter < 300000; ++iter) {
        int len = 0;
        const uint64_t kind = rng() % 16;
        if (kind == 0) buf[len++] = '-';
        if (kind != 1) {
            const int digits = 1 + (int)(rng() % (kind < 12 ? 4 : 8));
            for (int i = 0; i < digits; ++i)
                buf[len++] = char('0' + rng() % 10);
        }
        const int payload = len;
        buf[len++] = (rng() % 2) ? ':' : '|';
        memset(buf + len, 0, sizeof(buf) - (size_t)len);

        int32_t got = -777, want = -777;
        const char* gp = fosu::internal::parse_coord(buf, buf + payload, got);
        int64_t v;
        const char* wp = fosu::internal::parse_i64(buf, buf + payload, v);
        if (wp != buf && (v < -131072 || v > 131072)) wp = buf;
        if (wp != buf) want = static_cast<int32_t>(v);
        CHECK_EQ(gp - buf, wp - buf);
        CHECK_EQ(got, want);
        if (g_failures) {
            printf("  failing coord: %.*s\n", payload, buf);
            return;
        }
    }
}

#if FOSU_SIMD_X86
// Fuzz the one-pass timing point parser against the generic reference:
// whenever it accepts a line, every field must be bitwise identical.
// Shapes: 8-field editor lines plus old 2..7-field forms, decimal and
// negative offsets, integer and long-fraction beatLengths, and injected
// junk bytes (a '|' posing as the decimal point caught a real bug here).
static void test_fuzz_timing_point() {
    char buf[256];
    size_t accepted = 0;
    for (int iter = 0; iter < 400000; ++iter) {
        int len = 0;
        const uint64_t shape = rng() % 10;
        if (shape == 9) buf[len++] = '-';
        const int od = 1 + (int)(rng() % 11);
        for (int i = 0; i < od; ++i) buf[len++] = char('0' + rng() % 10);
        if (shape == 8) {
            buf[len++] = '.';
            for (int i = 0; i < 3; ++i) buf[len++] = char('0' + rng() % 10);
        }
        buf[len++] = ',';
        if (rng() % 2) buf[len++] = '-';
        const int bi = 1 + (int)(rng() % 4);
        for (int i = 0; i < bi; ++i) buf[len++] = char('0' + rng() % 10);
        if (rng() % 2) {
            buf[len++] = '.';
            const int bf = 1 + (int)(rng() % 15);
            for (int i = 0; i < bf; ++i) buf[len++] = char('0' + rng() % 10);
        }
        const int nf = shape == 7 ? (int)(rng() % 6) : 6;
        for (int f = 0; f < nf; ++f) {
            buf[len++] = ',';
            const int fd = 1 + (int)(rng() % 3);
            for (int i = 0; i < fd; ++i) buf[len++] = char('0' + rng() % 10);
        }
        if (rng() % 3 == 0) {
            const char junk[] = {'-', '.', ',', 'x', ' ', ':', '|', ','};
            buf[rng() % (uint64_t)len] = junk[rng() % 8];
        }
        memset(buf + len, 0, sizeof(buf) - (size_t)len);

        fosu::TimingPoint tp{};
        const auto a = _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf));
        const auto b =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf + 32));
        if (!fosu::internal::fast_parse_timing_point(a, b, buf, (size_t)len, tp))
            continue;
        ++accepted;
        fosu::Beatmap ref;
        fosu::internal::parse_timing_point_line(ref, buf, (size_t)len);
        CHECK(!ref.timing_points.empty());
        if (!ref.timing_points.empty()) {
            const fosu::TimingPoint& w = ref.timing_points[0];
            CHECK(memcmp(&tp.time, &w.time, 8) == 0);
            CHECK(memcmp(&tp.beat_length, &w.beat_length, 8) == 0);
            CHECK_EQ(tp.meter, w.meter);
            CHECK_EQ(tp.sample_set, w.sample_set);
            CHECK_EQ(tp.sample_index, w.sample_index);
            CHECK_EQ(tp.volume, w.volume);
            CHECK_EQ(tp.uninherited, w.uninherited);
            CHECK_EQ(tp.effects, w.effects);
        }
        if (g_failures) {
            printf("  failing timing line: %.*s\n", len, buf);
            return;
        }
    }
    printf("  timing fuzz: fast path accepted %zu lines\n", accepted);
    CHECK(accepted > 80000);
}

// Generate a value that renders with exactly `digits` decimal digits.
static uint64_t value_with_digits(int digits, uint64_t max) {
    const uint64_t lo = digits == 1 ? 0 : fosu::internal::kPow10[digits - 1] < 1e19
                            ? (uint64_t)fosu::internal::kPow10[digits - 1]
                            : 0;
    uint64_t hi = (uint64_t)fosu::internal::kPow10[digits] - 1;
    if (hi > max) hi = max;
    if (lo > hi) return hi;
    return lo + rng() % (hi - lo + 1);
}

static void test_fuzz_equivalence() {
    char buf[128];
    int fast_taken = 0;
    for (int iter = 0; iter < 300000; ++iter) {
        const int lx = 1 + (int)(rng() % 3);
        const int ly = 1 + (int)(rng() % 3);
        const int lt = 1 + (int)(rng() % 10);
        const int lty = 1 + (int)(rng() % 3);
        const uint64_t x = value_with_digits(lx, 999);
        const uint64_t y = value_with_digits(ly, 999);
        const uint64_t t = value_with_digits(lt, 9999999999ull);
        const uint64_t ty = value_with_digits(lty, 255);
        const uint64_t hs = rng() % 100;
        int len = snprintf(buf, sizeof buf,
                           "%" PRIu64 ",%" PRIu64 ",%" PRIu64 ",%" PRIu64
                           ",%" PRIu64 ",0:0:0:0:",
                           x, y, t, ty, hs);
        memset(buf + len, 0, sizeof(buf) - (size_t)len);

        // Randomly corrupt some lines; the invariant is that whenever the
        // fast path accepts, it must agree exactly with the scalar path.
        if (rng() % 4 == 0) {
            const int pos = (int)(rng() % (uint64_t)len);
            const char junk[] = {'-', '.', ',', 'x', ' ', '|'};
            buf[pos] = junk[rng() % sizeof junk];
        }

        fosu::HitObject fast{}, ref{};
        uint32_t nl_mask;
        const int fn = fosu::internal::fast_parse_prefix(buf, fast, nl_mask);
        const int rn = fosu::internal::scalar_parse_prefix(buf, strlen(buf), ref);
        if (fn < 0) continue;
        ++fast_taken;
        CHECK(rn >= 0);
        CHECK_EQ(fn, rn);
        CHECK_EQ(fast.x, ref.x);
        CHECK_EQ(fast.y, ref.y);
        CHECK_EQ(fast.time, ref.time);
        CHECK_EQ(fast.type, ref.type);
        CHECK_EQ(fast.hitsound, ref.hitsound);
        if (g_failures) {
            printf("  failing line: %s\n", buf);
            return;
        }
    }
    printf("  fuzz: fast path accepted %d lines\n", fast_taken);
    CHECK(fast_taken > 100000);
}
#endif

#if FOSU_SIMD_X86
// Shape-cache equivalence: any line whose (comma, nondigit, len) key
// matches a cached shape must (a) be accepted by the reference parser and
// (b) convert bit-identically through tp_shape_convert.
void test_fuzz_tp_shape_cache() {
    printf("timing shape-cache fuzz\n");
    using namespace fosu::internal;
    const char* seeds[] = {
        "277,342.466666666667,4,2,1,60,1,0",
        "1234,-100,4,2,1,60,0,1",
        "56676,-83.3333333333333,4,2,1,45,0,1",
        "120,300,4,0,0,100,1,0",
        "1885,352.941176470588,4,1,0,70,1,0",
        "141476,-66.6666666666667,4,3,2,5,0,8",
    };
    uint64_t rng = 0x5EED5EEDULL;
    auto rnd = [&rng] {
        rng ^= rng << 13; rng ^= rng >> 7; rng ^= rng << 17;
        return rng;
    };
    TpShapeCache cache{};
    char buf[64 + 80] = {};
    const char muts[] = "0123456789,.-x";
    int checked = 0;
    for (int it = 0; it < 300000; ++it) {
        const char* seed = seeds[rnd() % 6];
        size_t len = strlen(seed);
        memset(buf, 0, sizeof buf);
        memcpy(buf, seed, len);
        for (int m = int(rnd() % 4); m-- > 0;)
            buf[rnd() % len] = muts[rnd() % 14];
        if (rnd() % 8 == 0) cache = TpShapeCache{};  // section reset
        if (len > 64 || len < 15) continue;
        const __m256i a =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf));
        const __m256i b =
            _mm256_loadu_si256(reinterpret_cast<const __m256i*>(buf + 32));
        const uint64_t line_mask = len == 64 ? ~0ull : ((1ull << len) - 1);
        const uint64_t commas =
            (comma_mask32(a) | uint64_t(comma_mask32(b)) << 32) & line_mask;
        const uint64_t nondig =
            (nondigit_mask32(a) | uint64_t(nondigit_mask32(b)) << 32) &
            line_mask;
        fosu::TimingPoint ref{};
        TpGeom geom;
        const bool ref_ok = fast_parse_timing_point_masked(
            commas, nondig, buf, len, ref, &geom);
        const TpShapeRow& row = cache.rows[TpShapeCache::slot(commas)];
        if (tp_shape_match(row, commas, nondig, len, buf)) {
            CHECK(ref_ok);  // cached shape implies structural validity
            fosu::TimingPoint got{};
            tp_shape_convert(row, buf, got);
            CHECK(memcmp(&got, &ref, sizeof(fosu::TimingPoint)) == 0);
            if (g_failures) {
                printf("  failing line: %s\n", buf);
                return;
            }
            ++checked;
        } else if (ref_ok) {
            tp_shape_insert(cache, commas, nondig, len, geom);
        }
    }
    printf("  shape-cache fuzz: %d hits verified\n", checked);
    CHECK(checked > 30000);
}
#endif

int main() {
    test_fuzz_parse_double();
    test_fuzz_parse_coord();
#if FOSU_SIMD_X86
    test_fuzz_equivalence();
    test_fuzz_timing_point();
    test_fuzz_tp_shape_cache();
    puts("SIMD path: enabled");
#else
    puts("SIMD path: not built");
#endif
    return test_result();
}
