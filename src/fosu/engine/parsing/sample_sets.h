#pragma once

#include <fosu/enums.h>
#include <cstdint>
#include <optional>

namespace fosu::internal {

inline std::optional<SampleSet> parse_sample_set(int64_t value) {
  if (value < 0 || value > 3)
    return std::nullopt;
  return static_cast<SampleSet>(value);
}

}  // namespace fosu::internal
