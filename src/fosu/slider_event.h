#pragma once

#include <fosu/slider_path.h>
#include <fosu/types.h>

namespace fosu {
enum class SliderEventType : u8 { Head, Tick, Repeat, LegacyLastTick, Tail };

struct SliderEvent {
  SliderEventType type;
  f64             time;
  i32             span_index;
  f64             span_start_time;
  f64             path_progress;

  PathPoint position;  // Relative to the slider head, like SliderPath points.
};
}  // namespace fosu
