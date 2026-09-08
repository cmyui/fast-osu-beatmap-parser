#pragma once

#include <cstdint>

namespace fosu {

enum class CurveType : uint8_t {
  Bezier = 'B',
  Catmull = 'C',
  Linear = 'L',
  PerfectCurve = 'P',
};

// None is the legacy zero value: a timing point uses the beatmap default;
// at the beatmap level it denotes the default normal sample bank.
enum class SampleSet : int32_t {
  None = 0,
  Normal = 1,
  Soft = 2,
  Drum = 3,
};

}  // namespace fosu
