#pragma once

#include <fosu/enums.h>
#include <fosu/types.h>

#include <optional>

namespace fosu::internal {

inline std::optional<SampleSet> parse_sample_set(i64 value) {
  if (value < 0 || value > 3)
    return std::nullopt;
  return static_cast<SampleSet>(value);
}

}  // namespace fosu::internal
