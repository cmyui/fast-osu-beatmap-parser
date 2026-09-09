#pragma once

#include <fosu/engine/parsing/numbers.h>

namespace fosu::internal {

// NaN has a defined gameplay meaning only for inherited timing points.
inline const char* parse_beat_length(const char* p, const char* end, double& out) {
  const char* first = skip_numeric_space(p, end);
  if (first < end && (*first == '+' || *first == '-'))
    ++first;
  if (end - first >= 3 && (first[0] | 32) == 'n' && (first[1] | 32) == 'a' &&
      (first[2] | 32) == 'n') {
    out = std::numeric_limits<double>::quiet_NaN();
    return skip_numeric_space(first + 3, end);
  }
  return parse_osu_double(p, end, out);
}

}  // namespace fosu::internal
