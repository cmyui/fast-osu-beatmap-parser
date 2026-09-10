#pragma once

#include <fosu/engine/parsing/numbers.h>
#include <fosu/engine/primitives/byte_scan.h>
#include <string_view>

namespace fosu::internal {

inline bool ignored_line(const char* p, const char* end) {
  p = skip_numeric_space(p, end);
  return p == end || (end - p >= 2 && p[0] == '/' && p[1] == '/');
}

inline std::string_view trim(const char* p, const char* end) {
  while (p < end && (*p == ' ' || *p == '\t'))
    ++p;
  while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
    --end;
  return {p, static_cast<size_t>(end - p)};
}

struct Line {
  std::string_view text;
  const char* next;
};

inline Line read_line(const char* p, const char* end) {
  const char* newline = find_byte<'\n'>(p, end);
  const char* line_end = newline;
  if (line_end > p && line_end[-1] == '\r')
    --line_end;
  return {{p, static_cast<size_t>(line_end - p)}, newline + (newline < end)};
}

// Consume a section body, leaving its next header for the document parser.
// Specialized section loops can fuse framing with record parsing instead.
template <typename ParseLine>
inline const char* for_each_section_line(const char* p,
                                         const char* end,
                                         ParseLine parse_line) {
  while (p < end) {
    if (*p == '\r' || *p == '\n') {
      ++p;
      continue;
    }
    if (*p == '[')
      break;
    const auto line = read_line(p, end);
    if (!ignored_line(p, p + line.text.size()))
      parse_line(line.text);
    p = line.next;
  }
  return p;
}

// As above, but stops and reports allocation failure when a line cannot be
// published to parser-owned scratch storage.
template <typename ParseLine>
inline const char* for_each_section_line_until(const char* p,
                                               const char* end,
                                               ParseLine parse_line) {
  while (p < end) {
    if (*p == '\r' || *p == '\n') {
      ++p;
      continue;
    }
    if (*p == '[')
      break;
    const auto line = read_line(p, end);
    if (!ignored_line(p, p + line.text.size()) && !parse_line(line.text))
      return nullptr;
    p = line.next;
  }
  return p;
}

inline const char* skip_section(const char* p, const char* end) {
  const char* header = find_byte<'['>(p, end);
  // A bracket inside a value or comment is not a section header.
  while (header < end && header != p && header[-1] != '\n' && header[-1] != '\r')
    header = find_byte<'['>(header + 1, end);
  return header;
}

}  // namespace fosu::internal
