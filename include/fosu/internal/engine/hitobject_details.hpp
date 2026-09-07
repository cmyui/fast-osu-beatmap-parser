#pragma once

// Hitobject kind classification and kind-specific trailing fields.
#include <optional>
#include <string_view>
#include "prefix.hpp"

namespace fosu::internal {

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

enum class HitObjectKind {
    Circle,
    Slider,
    Spinner,
    Hold,
    Invalid,
};

// A type value may contain several kind bits. The official decoder resolves
// them in this order while leaving combo and colour-skip bits untouched.
inline HitObjectKind classify_hitobject_kind(uint32_t type) {
    if (type & 1) return HitObjectKind::Circle;
    if (type & 2) return HitObjectKind::Slider;
    if (type & 8) return HitObjectKind::Spinner;
    if (type & 128) return HitObjectKind::Hold;
    return HitObjectKind::Invalid;
}

inline std::optional<std::string_view> parse_hit_sample(
    const char* p, const char* end, bool banks_only = false) {
    if (end - p == 8 && short_sample(p)) return std::string_view{p, 8};
    const auto* comma = static_cast<const char*>(memchr(p, ',', end - p));
    const char* sample_end = comma ? comma : end;
    const std::string_view sample{p, static_cast<size_t>(sample_end - p)};
    if (!valid_sample(sample, banks_only)) return std::nullopt;
    return sample;
}

struct CircleDetails {
    std::string_view hit_sample;
};

inline std::optional<CircleDetails> parse_circle_details(
    const char* p, const char* end) {
    if (p == end) return CircleDetails{};
    if (*p != ',') return std::nullopt;
    const auto sample = parse_hit_sample(p + 1, end);
    if (!sample) return std::nullopt;
    return CircleDetails{*sample};
}

struct TimedHitObjectDetails {
    double end_time;
    std::string_view hit_sample;
};

// Raw timestamps remain unshifted and unclamped.
inline std::optional<TimedHitObjectDetails> parse_spinner_details(
    const char* p, const char* end) {
    if (p == end || *p != ',') return std::nullopt;
    double end_time;
    const char* next = parse_osu_double(p + 1, end, end_time);
    if (next == p + 1 || (next < end && *next != ',')) return std::nullopt;
    const auto sample = parse_hit_sample(next < end ? next + 1 : end, end);
    if (!sample) return std::nullopt;
    return TimedHitObjectDetails{end_time, *sample};
}

// Omitted endpoints and the ':' separator follow ConvertHitObjectParser.
inline std::optional<TimedHitObjectDetails> parse_hold_details(
    double start_time, const char* p, const char* end) {
    if (p == end || (p + 1 == end && *p == ','))
        return TimedHitObjectDetails{start_time, {}};
    if (*p != ',') return std::nullopt;
    double end_time;
    const char* next = parse_osu_double(p + 1, end, end_time);
    if (next == p + 1 ||
        (next < end && *next != ',' && *next != ':'))
        return std::nullopt;
    // The official decoder ignores a comma-separated value here; a hold's
    // hit sample belongs after the ':' in objectParams.
    if (next < end && *next == ',') return TimedHitObjectDetails{end_time, {}};
    const auto sample = parse_hit_sample(next < end ? next + 1 : end, end);
    if (!sample) return std::nullopt;
    return TimedHitObjectDetails{end_time, *sample};
}

} // namespace fosu::internal
