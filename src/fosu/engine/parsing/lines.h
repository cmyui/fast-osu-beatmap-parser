#pragma once

#include <fosu/engine/parsing/numbers.h>
#include <fosu/engine/primitives/byte_scan.h>

#include <cstddef>
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
  const char*      next;
};

inline const char* after_line_ending(const char* line_end, const char* end) {
  if (line_end == end)
    return end;
  const char* next = line_end + 1;
  return *line_end == '\r' && next < end && *next == '\n' ? next + 1 : next;
}

inline Line read_line(const char* p, const char* end) {
  const char* line_end = find_line_end(p, end);
  return {{p, static_cast<size_t>(line_end - p)},
          after_line_ending(line_end, end)};
}

// A stray '[' at the start of a malformed record must not end its section.
inline bool section_header_line(const char* p, const char* end) {
  if (p == end || *p != '[')
    return false;
  while (end > p && (end[-1] == ' ' || end[-1] == '\t'))
    --end;
  return end - p >= 2 && end[-1] == ']';
}

// Consume a section body, leaving its next header for the document parser.
// Specialized section loops can fuse framing with record parsing instead.
template <typename ParseLine>
inline const char* for_each_section_line(const char* p,
                                         const char* end,
                                         ParseLine   parse_line) {
  while (p < end) {
    if (*p == '\r' || *p == '\n') {
      ++p;
      continue;
    }
    const auto line = read_line(p, end);
    if (section_header_line(p, p + line.text.size()))
      break;
    if (!ignored_line(p, p + line.text.size()))
      parse_line(line.text);
    p = line.next;
  }
  return p;
}

inline const char* skip_section(const char* p, const char* end) {
  const char* header = find_byte<'['>(p, end);
  // A bracket inside a value or an incomplete header is not a section.
  while (header < end) {
    if (header == p || header[-1] == '\n' || header[-1] == '\r') {
      const auto line = read_line(header, end);
      if (section_header_line(header, header + line.text.size()))
        return header;
    }
    header = find_byte<'['>(header + 1, end);
  }
  return end;
}

}  // namespace fosu::internal
