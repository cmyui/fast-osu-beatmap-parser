#pragma once

#include <fosu/enums.h>
#include <optional>

namespace fosu::internal {

inline std::optional<CurveType> parse_curve_type(char value) {
  switch (value) {
    case 'B':
      return CurveType::Bezier;
    case 'C':
      return CurveType::Catmull;
    case 'L':
      return CurveType::Linear;
    case 'P':
      return CurveType::PerfectCurve;
    default:
      return std::nullopt;
  }
}

inline std::optional<SampleSet> parse_sample_set(int64_t value) {
  if (value < 0 || value > 3)
    return std::nullopt;
  return static_cast<SampleSet>(value);
}

}  // namespace fosu::internal
