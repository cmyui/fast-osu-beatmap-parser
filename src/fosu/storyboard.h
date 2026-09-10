#pragma once

#include <cstdint>
#include <string_view>

namespace fosu {

enum class StoryboardElementType : uint8_t {
  Video,
  Sprite,
  Animation,
  Sample,
};

enum class StoryboardLayer : uint8_t {
  Background,
  Fail,
  Pass,
  Foreground,
  Overlay,
  Video,
};

enum class StoryboardOrigin : uint8_t {
  TopLeft,
  Centre,
  CentreLeft,
  TopRight,
  BottomCentre,
  TopCentre,
  Custom,
  CentreRight,
  BottomLeft,
  BottomRight,
};

enum class AnimationLoopType : uint8_t {
  LoopForever,
  LoopOnce,
};

enum class StoryboardCommandType : uint8_t {
  Fade,
  Scale,
  VectorScale,
  Rotate,
  MoveX,
  MoveY,
  Colour,
  Parameter,
  Loop,
  Trigger,
};

enum class StoryboardParameter : uint8_t {
  None,
  Additive,
  FlipHorizontal,
  FlipVertical,
};

struct StoryboardCommand {
  StoryboardCommandType type;
  uint8_t depth;
  int32_t easing;
  double start_time;
  double end_time;
  float start_value[3];
  float end_value[3];
  StoryboardParameter parameter;
  int32_t repeat_count;
  int32_t group_number;
  std::string_view trigger_name;
};

struct StoryboardElement {
  StoryboardElementType type;
  StoryboardLayer layer;
  StoryboardOrigin origin;
  AnimationLoopType loop_type;
  std::string_view filename;
  float x;
  float y;
  double time;
  int32_t volume;
  int32_t frame_count;
  double frame_delay;
  uint32_t command_begin;
  uint32_t command_count;
};

}  // namespace fosu
