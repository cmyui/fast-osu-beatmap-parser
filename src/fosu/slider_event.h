#pragma once

#include <fosu/slider_path.h>
#include <cstdint>

namespace fosu {
enum class SliderEventType : uint8_t { Head, Tick, Repeat, Tail };

struct SliderEvent {
  SliderEventType type;
  double time;
  int32_t span_index;
  double span_start_time;
  double path_progress;
  PathPoint position;  // Relative to the slider head, like SliderPath points.
};
}  // namespace fosu
