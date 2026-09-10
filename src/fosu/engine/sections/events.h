#pragma once
#include <fosu/beatmap.h>
#include <fosu/engine/parsing/arena_list.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/parsing/numbers.h>
#include <fosu/engine/parsing/string_lookup.h>
#include <fosu/engine/primitives/byte_scan.h>
#include <algorithm>
#include <optional>

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
      const auto second = trailing_zeros(commas & (commas - 1));
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

inline bool parse_background_event(Beatmap& bm,
                                   Arena*,
                                   ArenaList<Break>&,
                                   const char* rest,
                                   const char* end,
                                   int) {
  if (const auto filename = parse_event_filename(rest, end))
    bm.background = *filename;
  return true;
}

inline bool parse_video_event(Beatmap& bm,
                              Arena*,
                              ArenaList<Break>&,
                              const char* rest,
                              const char* end,
                              int) {
  if (const auto filename = parse_event_filename(rest, end))
    bm.video = *filename;
  return true;
}

inline bool parse_break_event(Beatmap& bm,
                              Arena* arena,
                              ArenaList<Break>& breaks,
                              const char* rest,
                              const char* end,
                              int time_offset) {
  double start, stop;
  const char* q = parse_osu_double(rest, end, start);
  if (q == rest || q >= end || *q != ',') {
    ++bm.stats.malformed_lines;
    return true;
  }
  const char* r = parse_osu_double(q + 1, end, stop);
  if (r == q + 1 || r != end) {
    ++bm.stats.malformed_lines;
    return true;
  }
  start += time_offset;
  const Break parsed{start, std::max(start, stop + time_offset)};
  return arena_list_push(arena, breaks, parsed) != nullptr;
}

using EventHandler =
    bool (*)(Beatmap&, Arena*, ArenaList<Break>&, const char*, const char*, int);
inline constexpr auto kEventHandlers = make_string_lookup<EventHandler>({
    {"0", parse_background_event},
    {"1", parse_video_event},
    {"Video", parse_video_event},
    {"2", parse_break_event},
    {"Break", parse_break_event},
});

inline bool parse_event_line(Beatmap& bm,
                             Arena* arena,
                             ArenaList<Break>& breaks,
                             const char* p,
                             size_t len,
                             int time_offset) {
  // Storyboard commands are indented; count and skip them.
  if (len == 0 || *p == ' ' || *p == '_') {
    ++bm.stats.storyboard_lines;
    return true;
  }
  const char* end = p + len;
  const auto* c1 = find_byte<','>(p, end);
  if (c1 == end) {
    ++bm.stats.storyboard_lines;
    return true;
  }
  const std::string_view f0{p, static_cast<size_t>(c1 - p)};
  const char* rest = c1 + 1;
  if (const auto* handler = kEventHandlers.find(f0))
    return (*handler)(bm, arena, breaks, rest, end, time_offset);
  else {
    ++bm.stats.storyboard_lines;
    return true;
  }
}

#if FOSU_SIMD
inline const char* parse_events_section_simd(Beatmap& bm,
                                             Arena* arena,
                                             ArenaList<Break>& breaks,
                                             const char* p,
                                             const char* file_end,
                                             int time_offset) {
  // Fused loop: skip indented storyboard commands on their first byte and
  // find line endings with two 32-byte vector compares.
  uint32_t storyboard_lines = 0;
  while (p < file_end) {
    const char c = *p;
    if (c == '\r' || c == '\n') {
      ++p;
      continue;
    }
    if (c == '[')
      break;

    const Bytes32 a = load32(p);
    const Bytes32 b = load32(p + 32);
    const uint64_t nl = equal_mask32(a, broadcast_byte<'\n'>()) |
                        (uint64_t(equal_mask32(b, broadcast_byte<'\n'>())) << 32);
    const char* line = p;
    const char* next_line;
    const char* line_end;
    if (nl) {
      line_end = p + trailing_zeros(nl);
      next_line = line_end + 1;
    } else {
      line_end = find_byte<'\n'>(p + 64, file_end);
      next_line = line_end + (line_end < file_end);
    }
    if (line_end[-1] == '\r')
      --line_end;  // line_end > line: c is not CR
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
    if (!parse_event_line(bm, arena, breaks, line, len, time_offset))
      return nullptr;
  }
  bm.stats.storyboard_lines += storyboard_lines;
  return p;
}
#endif

inline const char* parse_events_section_scalar(Beatmap& bm,
                                               Arena* arena,
                                               ArenaList<Break>& breaks,
                                               const char* p,
                                               const char* file_end,
                                               int time_offset) {
  return for_each_section_line_until(p, file_end, [&](std::string_view line) {
    return parse_event_line(bm, arena, breaks, line.data(), line.size(), time_offset);
  });
}

inline const char* parse_events_section(Beatmap& bm,
                                        Arena* arena,
                                        ArenaList<Break>& breaks,
                                        const char* p,
                                        const char* file_end,
                                        int time_offset = 0) {
#if FOSU_SIMD
  return parse_events_section_simd(bm, arena, breaks, p, file_end, time_offset);
#else
  return parse_events_section_scalar(bm, arena, breaks, p, file_end, time_offset);
#endif
}

}  // namespace fosu::internal
