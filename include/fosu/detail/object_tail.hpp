#pragma once
#include <string_view>
#include "prefix.hpp"

namespace fosu::detail {

// Four ASCII digits in the low byte of four 16-bit lanes. The empty high
// bytes keep these additions independent; bit 8 tests both bounds per lane.
inline bool four_sample_digits(uint64_t text) {
    constexpr uint64_t lanes = 0x0100010001000100ull;
    const uint64_t digits = text & 0x00ff00ff00ff00ffull;
    return ((digits + 0x00d000d000d000d0ull) &
            ~(digits + 0x00c600c600c600c6ull) & lanes) == lanes;
}
inline bool short_sample(const char* p) {
#if FOSU_SIMD_X86
    const __m128i text = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(p));
    // Digits become [-128, -119]; each ':' becomes -128. A single
    // comparison rejects both non-digits and misplaced separators.
    const __m128i biased = _mm_add_epi8(text, _mm_set1_epi16(0x4650));
    const __m128i invalid = _mm_cmpgt_epi8(biased, _mm_set1_epi16(-32631));
    return (_mm_movemask_epi8(invalid) & 0xff) == 0;
#else
    const uint64_t text = load_u64_le(p);
    return (text & 0xff00ff00ff00ff00ull) == 0x3a003a003a003a00ull &&
           four_sample_digits(text);
#endif
}

// Only the fields read by the official legacy decoder affect acceptance.
// Unknown trailing sample/edge columns are retained as raw text by callers.
inline bool valid_sample(std::string_view sample, bool banks_only = false) {
    if (sample.empty()) return true;
    if (sample.size() == 3 && sample[1] == ':' &&
        is_digit(sample[0]) && is_digit(sample[2])) return true;
    // The common editor spelling avoids four separate integer conversions.
    if (sample.size() >= 8 && short_sample(sample.data())) return true;
    const char* p = sample.data();
    const char* end = p + sample.size();
    for (int i = 0; i < (banks_only ? 2 : 4); ++i) {
        int64_t value;
        const char* q = parse_osu_int(p, end, value);
        if (q == p || (q < end && *q != ':')) [[unlikely]] return false;
        if (q == end) return i >= 1;
        p = q + 1;
    }
    return true; // The fifth field is an arbitrary filename.
}

inline bool valid_edge_sets(std::string_view sets, int32_t slides) {
    if (sets.empty()) return true;
    if (sets.size() == 7) {
#if FOSU_SIMD_X86
        const __m128i text = _mm_loadl_epi64(reinterpret_cast<const __m128i*>(sets.data()));
        const __m128i biased = _mm_add_epi8(text, _mm_set_epi64x(0, 0x0050465004504650ull));
        const __m128i invalid = _mm_cmpgt_epi8(biased, _mm_set1_epi16(-32631));
        if ((_mm_movemask_epi8(invalid) & 0x7f) == 0) return true;
#else
        const uint64_t text = load_u64_le(sets.data());
        if ((text & 0x0000ff00ff00ff00ull) == 0x00003a007c003a00ull &&
            four_sample_digits(text)) return true;
#endif
    }
    const char* p = sets.data();
    const char* end = p + sets.size();
    const int nodes = (slides > 0 ? slides : 1) + 1;
    for (int i = 0; i < nodes; ++i) {
        // Editor bank pairs are single digits. Validate those directly;
        // additional sample fields and unusual integers use the same fallback.
        if (end - p >= 3 && p[1] == ':' && is_digit(p[0]) && is_digit(p[2])) {
            if (end - p == 3) return true;
            if (p[3] == '|') { p += 4; continue; }
        }
        const auto* separator = static_cast<const char*>(memchr(p, '|', end - p));
        const char* next = separator ? separator : end;
        if (!valid_sample({p, static_cast<size_t>(next - p)})) [[unlikely]] return false;
        if (!separator) break;
        p = next + 1;
    }
    return true;
}

// Circle precedence, omitted hold endpoints and spinner separators follow
// ConvertHitObjectParser. Raw timestamps remain unshifted/unclamped.
template <typename H>
inline bool parse_object_tail(H& h, const char* p, const char* end,
                              std::string_view& sample) {
    if (!(h.type & 1) && (h.type & (8 | 128))) {
        const bool hold = !(h.type & 8);
        if (hold && (p == end || (p + 1 == end && *p == ','))) {
            h.end_time = h.time;
            sample = {};
            return true;
        }
        if (p == end || *p != ',') [[unlikely]] return false;
        double time;
        const char* q = parse_osu_double(p + 1, end, time);
        if (q == p + 1 || (q < end && *q != ',' && !(hold && *q == ':'))) [[unlikely]] return false;
        h.end_time = time;
        p = q;
        if (hold && p < end && *p == ',') p = end;
        else if (p < end) ++p;
    } else if (p < end) ++p;
    if (end - p == 8 && short_sample(p)) {
        sample = {p, 8};
        return true;
    }
    const auto* comma = static_cast<const char*>(memchr(p, ',', end - p));
    const char* sample_end = comma ? comma : end;
    sample = {p, static_cast<size_t>(sample_end - p)};
    return valid_sample(sample);
}

} // namespace fosu::detail
