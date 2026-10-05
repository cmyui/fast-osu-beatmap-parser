#pragma once
#include <fosu/beatmap.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/parsing/numbers.h>
#include <fosu/engine/parsing/string_lookup.h>
#include <fosu/engine/primitives/byte_scan.h>
#include <fosu/engine/primitives/vector_ops.h>
#include <fosu/format.h>
#include <fosu/types.h>

#include <algorithm>
#include <cstddef>
#include <optional>
#include <string_view>

namespace fosu::internal {

inline std::string_view strip_quotes(std::string_view v) {
  if (v.size() >= 2 && v.front() == '"' && v.back() == '"')
    return v.substr(1, v.size() - 2);
  return v;
}

inline std::optional<std::string_view> parse_event_filename(const char* rest,
                                                            const char* end) {
#if FOSU_SIMD
  // Timestamp and filename usually fit in one window. Reuse its comma mask
  // for both boundaries without exposing SIMD state to the event handlers.
  if (rest < end) {
    const auto commas = equal_mask32(load32(rest), broadcast_byte<','>());
    const auto first = trailing_zeros(commas);
    if (first < 32 && first < static_cast<size_t>(end - rest)) {
      const char* filename = rest + first + 1;
      const auto  second = trailing_zeros(commas & (commas - 1));
      const char* next = second < 32 && second < static_cast<size_t>(end - rest)
                             ? rest + second
                             : find_byte<','>(filename, end);
      return strip_quotes(trim(filename, next));
    }
  }
#endif
  const auto* comma = find_byte<','>(rest, end);
  if (comma == end)
    return std::nullopt;
  const char* filename = comma + 1;
  const auto* next = find_byte<','>(filename, end);
  return strip_quotes(trim(filename, next));
}

inline void parse_background_event(Beatmap& bm,
                                   size_t&,
                                   const char* rest,
                                   const char* end) {
  if (const auto filename = parse_event_filename(rest, end))
    bm.background = *filename;
}

inline void parse_video_event(Beatmap& bm,
                              size_t&,
                              const char* rest,
                              const char* end) {
  if (const auto filename = parse_event_filename(rest, end))
    bm.video = *filename;
}

template <Format F>
void parse_break_event(Beatmap&    bm,
                       size_t&     break_count,
                       const char* rest,
                       const char* end) {
  f64         start, stop;
  const char* q = parse_osu_double(rest, end, start);
  if (q == rest || q >= end || *q != ',') {
    ++bm.stats.malformed_lines;
    return;
  }
  // osu! ignores any fields after the end time.
  const char* r = parse_osu_double(q + 1, end, stop);
  if (r == q + 1 || (r != end && *r != ',')) {
    ++bm.stats.malformed_lines;
    return;
  }
  start += F.time_offset;
  bm.breaks[break_count++] = {start, std::max(start, stop + F.time_offset)};
}

using EventHandler = void (*)(Beatmap&, size_t&, const char*, const char*);
template <Format F>
inline constexpr auto kEventHandlers = make_string_lookup<EventHandler>({
    {"0", parse_background_event},
    {"1", parse_video_event},
    {"Video", parse_video_event},
    {"2", parse_break_event<F>},
    {"Break", parse_break_event<F>},
});

template <Format F>
void parse_event_line(Beatmap&    bm,
                      size_t&     break_count,
                      const char* p,
                      size_t      len) {
  // Storyboard commands are indented; count and skip them.
  if (len == 0 || *p == ' ' || *p == '_') {
    ++bm.stats.storyboard_lines;
    return;
  }
  const char* end = p + len;
  const auto* c1 = find_byte<','>(p, end);
  if (c1 == end) {
    ++bm.stats.storyboard_lines;
    return;
  }
  const std::string_view f0{p, static_cast<size_t>(c1 - p)};
  const char*            rest = c1 + 1;
  if (const auto* handler = kEventHandlers<F>.find(f0))
    (*handler)(bm, break_count, rest, end);
  else
    ++bm.stats.storyboard_lines;
}

#if FOSU_SIMD
template <Format F>
const char* parse_events_section_simd(Beatmap&    bm,
                                      size_t&     break_count,
                                      const char* p,
                                      const char* file_end) {
  // Fused loop: skip indented storyboard commands on their first byte and
  // find line endings with two 32-byte vector loads.
  u32 storyboard_lines = 0;
  while (p < file_end) {
    const char c = *p;
    if (c == '\r' || c == '\n') {
      ++p;
      continue;
    }
    const Bytes32 a = load32(p);
    const Bytes32 b = load32(p + 32);
    const u64   endings = line_end_mask32(a) | (u64(line_end_mask32(b)) << 32);
    const char* line = p;
    const char* next_line;
    const char* line_end;
    if (endings) {
      line_end = p + trailing_zeros(endings);
    } else {
      line_end = find_line_end(p + 64, file_end);
    }
    next_line = after_line_ending(line_end, file_end);
    if (c == '[' && section_header_line(line, line_end))
      break;
    p = next_line;

    if (fosu::internal::ignored_line(line, line_end))
      continue;
    if (c == ' ' || c == '_') {  // indented storyboard command
      ++storyboard_lines;
      continue;
    }
    const auto len = static_cast<size_t>(line_end - line);
    if (len >= 2 && c == '/' && line[1] == '/')
      continue;  // comment
    parse_event_line<F>(bm, break_count, line, len);
  }
  bm.stats.storyboard_lines += storyboard_lines;
  return p;
}
#endif

template <Format F>
const char* parse_events_section_scalar(Beatmap&    bm,
                                        size_t&     break_count,
                                        const char* p,
                                        const char* file_end) {
  return for_each_section_line(p, file_end, [&](std::string_view line) {
    parse_event_line<F>(bm, break_count, line.data(), line.size());
  });
}

template <Format F>
const char* parse_events_section(Beatmap&    bm,
                                 size_t&     break_count,
                                 const char* p,
                                 const char* file_end) {
#if FOSU_SIMD
  return parse_events_section_simd<F>(bm, break_count, p, file_end);
#else
  return parse_events_section_scalar<F>(bm, break_count, p, file_end);
#endif
}

}  // namespace fosu::internal
