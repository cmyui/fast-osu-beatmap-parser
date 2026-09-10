#pragma once

// Storyboard decoding semantics adapted from osu!.
// Copyright (c) ppy Pty Ltd <contact@ppy.sh>.
//
// Permission is hereby granted, free of charge, to any person obtaining a copy
// of this software and associated documentation files (the "Software"), to deal
// in the Software without restriction, including without limitation the rights
// to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
// copies of the Software, and to permit persons to whom the Software is
// furnished to do so, subject to the following conditions:
//
// The above copyright notice and this permission notice shall be included in
// all copies or substantial portions of the Software.
//
// THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
// IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
// FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
// AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
// LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
// OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
// THE SOFTWARE.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>
#include <limits>
#include <numbers>
#include <optional>

#include <fosu/beatmap.h>
#include <fosu/engine/parsing/field_values.h>
#include <fosu/engine/parsing/lines.h>
#include <fosu/engine/parsing/string_lookup.h>
#include <fosu/engine/primitives/byte_scan.h>
#include <fosu/io.h>
#include <fosu/parse_options.h>

namespace fosu::internal {

struct StoryboardVariable {
  std::string_view name;
  std::string_view value;
};

inline std::optional<std::string_view> replace_storyboard_variables(
    std::string_view line,
    std::span<const StoryboardVariable> variables,
    Arena* scratch) {
  for (const auto& variable : variables) {
    if (variable.name.empty())
      continue;
    size_t count = 0;
    for (size_t at = line.find(variable.name); at != std::string_view::npos;
         at = line.find(variable.name, at + variable.name.size()))
      ++count;
    if (!count)
      continue;
    const size_t removed = count * variable.name.size();
    const size_t added = count * variable.value.size();
    if (removed > line.size() ||
        added > std::numeric_limits<size_t>::max() - (line.size() - removed))
      return std::nullopt;
    const size_t size = line.size() - removed + added;
    auto* output = static_cast<char*>(arena_push(scratch, size, 1));
    if (!output)
      return std::nullopt;
    char* out = output;
    size_t start = 0;
    while (start < line.size()) {
      const size_t at = line.find(variable.name, start);
      if (at == std::string_view::npos)
        break;
      const size_t prefix = at - start;
      std::memcpy(out, line.data() + start, prefix);
      out += prefix;
      std::memcpy(out, variable.value.data(), variable.value.size());
      out += variable.value.size();
      start = at + variable.name.size();
    }
    std::memcpy(out, line.data() + start, line.size() - start);
    line = {output, size};
  }
  return line;
}

inline Result<std::span<const char>> expand_storyboard_variables(
    std::span<const char> input,
    uint32_t selected_sections,
    Arena* scratch) noexcept {
  if (!(selected_sections & kSectionEvents) || input.empty())
    return input;

  const std::string_view source{input.data(), input.size()};
  const size_t first_events = source.find("[Events]");
  if (first_events == std::string_view::npos ||
      source.substr(0, first_events).find("[Variables]") == std::string_view::npos)
    return input;

  size_t variable_count = 0;
  Section section = Section::None;
  for (const char *p = input.data(), *end = p + input.size(); p < end;) {
    const Line line = read_line(p, end);
    p = line.next;
    if (!line.text.empty() && line.text.front() == '[') {
      section = match_section(line.text);
      continue;
    }
    if (section == Section::Variables &&
        !ignored_line(line.text.data(), line.text.data() + line.text.size()) &&
        line.text.find('=') != std::string_view::npos)
      ++variable_count;
  }
  if (!variable_count)
    return input;

  auto* variables = arena_push_array<StoryboardVariable>(scratch, variable_count);
  if (!variables)
    return Error{ErrorCode::AllocationFailure};
  size_t variable_index = 0;
  section = Section::None;
  bool event_uses_variables = false;
  for (const char *p = input.data(), *end = p + input.size(); p < end;) {
    const Line line = read_line(p, end);
    p = line.next;
    if (!line.text.empty() && line.text.front() == '[') {
      section = match_section(line.text);
      continue;
    }
    if (section == Section::Variables) {
      const size_t separator = line.text.find('=');
      if (separator != std::string_view::npos) {
        variables[variable_index++] = {
            trim(line.text.data(), line.text.data() + separator),
            trim(line.text.data() + separator + 1, line.text.data() + line.text.size()),
        };
      }
    } else if (section == Section::Events &&
               line.text.find('$') != std::string_view::npos) {
      event_uses_variables = true;
    }
  }
  if (!event_uses_variables)
    return input;
  const std::span<const StoryboardVariable> definitions{variables, variable_index};

  auto transformed_size = [&]() -> std::optional<size_t> {
    TempArena temporary{scratch};
    size_t total = 0;
    Section current = Section::None;
    for (const char *p = input.data(), *end = p + input.size(); p < end;) {
      const Line source = read_line(p, end);
      p = source.next;
      if (!source.text.empty() && source.text.front() == '[')
        current = match_section(source.text);
      const auto line =
          current == Section::Events
              ? replace_storyboard_variables(source.text, definitions, scratch)
              : std::optional{source.text};
      if (!line)
        return std::nullopt;
      const size_t ending =
          static_cast<size_t>(source.next - (source.text.data() + source.text.size()));
      if (ending > std::numeric_limits<size_t>::max() - total ||
          line->size() > std::numeric_limits<size_t>::max() - total - ending)
        return std::nullopt;
      total += line->size() + ending;
    }
    return total;
  }();
  if (!transformed_size || !can_pad_input(*transformed_size))
    return Error{ErrorCode::InputTooLarge};

  auto* output =
      static_cast<char*>(arena_push(scratch, *transformed_size + kBufferPadding, 1));
  if (!output)
    return Error{ErrorCode::AllocationFailure};
  char* out = output;
  section = Section::None;
  for (const char *p = input.data(), *end = p + input.size(); p < end;) {
    const Line source = read_line(p, end);
    p = source.next;
    if (!source.text.empty() && source.text.front() == '[')
      section = match_section(source.text);
    TempArena temporary{scratch};
    const auto line = section == Section::Events ? replace_storyboard_variables(
                                                       source.text, definitions, scratch)
                                                 : std::optional{source.text};
    if (!line)
      return Error{ErrorCode::AllocationFailure};
    std::memcpy(out, line->data(), line->size());
    out += line->size();
    const char* ending = source.text.data() + source.text.size();
    const size_t ending_size = static_cast<size_t>(source.next - ending);
    std::memcpy(out, ending, ending_size);
    out += ending_size;
  }
  std::memset(out, 0, kBufferPadding);
  return std::span<const char>{output, *transformed_size};
}

inline std::string_view strip_quotes(std::string_view value) {
  value = trim(value.data(), value.data() + value.size());
  if (value.size() >= 2 && value.front() == '"' && value.back() == '"')
    return value.substr(1, value.size() - 2);
  return value;
}

struct EventFields {
  std::array<std::string_view, 10> values{};
  size_t count = 0;

  std::string_view operator[](size_t index) const {
    return index < count ? values[index] : std::string_view{};
  }
};

inline EventFields split_event_fields(std::string_view line) {
  EventFields result;
  const char* p = line.data();
  const char* end = p + line.size();
  while (result.count < result.values.size()) {
    const char* comma = find_byte<','>(p, end);
    result.values[result.count++] = {p, static_cast<size_t>(comma - p)};
    if (comma == end)
      break;
    p = comma + 1;
  }
  return result;
}

inline std::optional<StoryboardLayer> parse_storyboard_layer(std::string_view value) {
  static constexpr auto names = make_string_lookup<StoryboardLayer>({
      {"Background", StoryboardLayer::Background},
      {"Fail", StoryboardLayer::Fail},
      {"Pass", StoryboardLayer::Pass},
      {"Foreground", StoryboardLayer::Foreground},
      {"Overlay", StoryboardLayer::Overlay},
      {"Video", StoryboardLayer::Video},
  });
  if (const auto* named = names.find(value))
    return *named;
  const auto number = parse_field_integer(value);
  if (!number || *number < 0 || *number > 5)
    return std::nullopt;
  return static_cast<StoryboardLayer>(*number);
}

inline std::optional<StoryboardOrigin> parse_storyboard_origin(std::string_view value) {
  static constexpr auto names = make_string_lookup<StoryboardOrigin>({
      {"TopLeft", StoryboardOrigin::TopLeft},
      {"Centre", StoryboardOrigin::Centre},
      {"CentreLeft", StoryboardOrigin::CentreLeft},
      {"TopRight", StoryboardOrigin::TopRight},
      {"BottomCentre", StoryboardOrigin::BottomCentre},
      {"TopCentre", StoryboardOrigin::TopCentre},
      {"Custom", StoryboardOrigin::Custom},
      {"CentreRight", StoryboardOrigin::CentreRight},
      {"BottomLeft", StoryboardOrigin::BottomLeft},
      {"BottomRight", StoryboardOrigin::BottomRight},
  });
  if (const auto* named = names.find(value))
    return *named;
  const auto number = parse_field_integer(value);
  if (!number || *number < 0 || *number > 9)
    return std::nullopt;
  return static_cast<StoryboardOrigin>(*number);
}

inline std::optional<AnimationLoopType> parse_animation_loop(std::string_view value) {
  if (value.empty() || value == "LoopForever" || value == "0")
    return AnimationLoopType::LoopForever;
  if (value == "LoopOnce" || value == "1")
    return AnimationLoopType::LoopOnce;
  // The official decoder falls back for undefined numeric values.
  return parse_field_integer(value) ? std::optional{AnimationLoopType::LoopForever}
                                    : std::nullopt;
}

inline std::optional<std::string_view> parse_event_filename(const EventFields& fields,
                                                            size_t index) {
  if (index >= fields.count)
    return std::nullopt;
  return strip_quotes(fields[index]);
}

inline bool supported_video_filename(std::string_view filename) {
  const size_t dot = filename.rfind('.');
  if (dot == std::string_view::npos)
    return false;
  const auto extension = filename.substr(dot);
  constexpr std::string_view extensions[] = {
      ".mp4", ".mov", ".avi", ".flv", ".mpg", ".wmv", ".m4v",
  };
  for (const auto candidate : extensions) {
    if (extension.size() != candidate.size())
      continue;
    bool equal = true;
    for (size_t i = 0; i < extension.size(); ++i) {
      const char c = extension[i] >= 'A' && extension[i] <= 'Z'
                         ? static_cast<char>(extension[i] + ('a' - 'A'))
                         : extension[i];
      equal &= c == candidate[i];
    }
    if (equal)
      return true;
  }
  return false;
}

inline bool parse_background_event(Beatmap& map, const EventFields& fields) {
  const auto filename = parse_event_filename(fields, 2);
  if (!filename)
    return false;
  float offset_x = 0;
  float offset_y = 0;
  if (fields.count > 4) {
    const auto x = parse_field_float(fields[3]);
    const auto y = parse_field_float(fields[4]);
    if (!x || !y)
      return false;
    offset_x = static_cast<float>(*x);
    offset_y = static_cast<float>(*y);
  }
  map.background = *filename;
  map.storyboard_background_offset_x = offset_x;
  map.storyboard_background_offset_y = offset_y;
  return true;
}

inline bool parse_video_event(Beatmap& map,
                              size_t& element_count,
                              size_t command_count,
                              const EventFields& fields) {
  const auto filename = parse_event_filename(fields, 2);
  if (!filename)
    return false;
  const auto offset =
      fields[1].empty() ? std::optional<double>{0} : parse_field_double(fields[1]);
  if (!offset)
    return false;
  if (!supported_video_filename(*filename)) {
    map.background = *filename;
    return true;
  }
  if (element_count == map.storyboard_elements.size())
    return false;
  map.video = *filename;
  map.video_offset = *offset;
  map.storyboard_elements[element_count++] = {
      .type = StoryboardElementType::Video,
      .layer = StoryboardLayer::Video,
      .filename = *filename,
      .time = *offset,
      .command_begin = static_cast<uint32_t>(command_count),
  };
  return true;
}

inline bool parse_break_event(Beatmap& map,
                              size_t& break_count,
                              const EventFields& fields,
                              int time_offset) {
  if (fields.count < 3)
    return false;
  const auto start_value = parse_field_double(fields[1]);
  const auto end_value = parse_field_double(fields[2]);
  if (!start_value || !end_value)
    return false;
  const double start = *start_value + time_offset;
  map.breaks[break_count++] = {start, std::max(start, *end_value + time_offset)};
  return true;
}

inline bool parse_storyboard_element(Beatmap& map,
                                     size_t& element_count,
                                     size_t command_count,
                                     const EventFields& fields) {
  if (element_count == map.storyboard_elements.size())
    return false;
  StoryboardElement element{};
  element.command_begin = static_cast<uint32_t>(command_count);
  if (fields[0] == "5" || fields[0] == "Sample") {
    const auto time = parse_field_double(fields[1]);
    const auto layer = parse_storyboard_layer(fields[2]);
    const auto filename = parse_event_filename(fields, 3);
    const auto volume =
        fields.count > 4 ? parse_field_float(fields[4]) : std::optional<double>{100};
    if (!time || !layer || !filename || !volume)
      return false;
    element.type = StoryboardElementType::Sample;
    element.layer = *layer;
    element.filename = *filename;
    element.time = *time;
    element.volume = static_cast<int32_t>(*volume);
    map.storyboard_elements[element_count++] = element;
    return true;
  }

  const auto layer = parse_storyboard_layer(fields[1]);
  const auto origin = parse_storyboard_origin(fields[2]);
  const auto filename = parse_event_filename(fields, 3);
  const auto x = parse_field_float(fields[4]);
  const auto y = parse_field_float(fields[5]);
  if (!layer || !origin || !filename || !x || !y)
    return false;
  element.layer = *layer;
  element.origin = *origin;
  element.filename = *filename;
  element.x = static_cast<float>(*x);
  element.y = static_cast<float>(*y);
  if (fields[0] == "4" || fields[0] == "Sprite") {
    element.type = StoryboardElementType::Sprite;
    if (map.background.empty())
      map.background = *filename;
  } else {
    const auto frames = parse_field_integer(fields[6]);
    const auto delay = parse_field_double(fields[7]);
    const auto loop = parse_animation_loop(fields[8]);
    if (!frames || !delay || !loop)
      return false;
    element.type = StoryboardElementType::Animation;
    element.frame_count = *frames;
    element.frame_delay = *delay;
    if (map.format_version < 6)
      element.frame_delay =
          std::round(0.015 * element.frame_delay) * 1.186 * (1000.0 / 60.0f);
    element.loop_type = *loop;
  }
  map.storyboard_elements[element_count++] = element;
  return true;
}

inline bool parse_command_header(const EventFields& fields, StoryboardCommand& command) {
  if (fields.count < 5)
    return false;
  const auto easing = parse_field_integer(fields[1]);
  const auto start_time = parse_field_double(fields[2]);
  const auto end_time = fields[3].empty() ? start_time : parse_field_double(fields[3]);
  if (!easing || !start_time || !end_time)
    return false;
  command.easing = *easing;
  command.start_time = *start_time;
  command.end_time = *end_time;
  return true;
}

inline bool parse_command_values(const EventFields& fields,
                                 StoryboardCommand& command,
                                 size_t value_count) {
  if (!parse_command_header(fields, command) || fields.count < 4 + value_count)
    return false;
  for (size_t i = 0; i < value_count; ++i) {
    const auto start = parse_field_float(fields[4 + i]);
    const auto end = fields.count > 4 + value_count + i
                         ? parse_field_float(fields[4 + value_count + i])
                         : start;
    if (!start || !end)
      return false;
    command.start_value[i] = static_cast<float>(*start);
    command.end_value[i] = static_cast<float>(*end);
  }
  return true;
}

inline bool parse_storyboard_command(Beatmap& map,
                                     size_t element_index,
                                     size_t& command_count,
                                     const EventFields& fields,
                                     size_t depth) {
  if (element_index == static_cast<size_t>(-1) || fields.count == 0)
    return false;
  StoryboardCommand command{};
  command.depth = static_cast<uint8_t>(std::min(depth, size_t{255}));
  const auto publish = [&](const StoryboardCommand& value) {
    if (command_count == map.storyboard_commands.size())
      return false;
    map.storyboard_commands[command_count++] = value;
    ++map.storyboard_elements[element_index].command_count;
    return true;
  };

  if (fields[0] == "L") {
    const auto start = parse_field_double(fields[1]);
    const auto repeats = parse_field_integer(fields[2]);
    if (!start || !repeats)
      return false;
    command.type = StoryboardCommandType::Loop;
    command.start_time = *start;
    command.repeat_count = std::max(0, *repeats - 1);
    return publish(command);
  }
  if (fields[0] == "T") {
    if (fields.count < 2)
      return false;
    command.type = StoryboardCommandType::Trigger;
    command.trigger_name = fields[1];
    if (fields.count > 2 && !fields[2].empty()) {
      const auto start = parse_field_double(fields[2]);
      if (!start)
        return false;
      command.start_time = *start;
    } else
      command.start_time = std::numeric_limits<double>::lowest();
    if (fields.count > 3 && !fields[3].empty()) {
      const auto end = parse_field_double(fields[3]);
      if (!end)
        return false;
      command.end_time = *end;
    } else
      command.end_time = std::numeric_limits<double>::max();
    if (fields.count > 4 && !fields[4].empty()) {
      const auto group = parse_field_integer(fields[4]);
      if (!group)
        return false;
      command.group_number = -*group;
    }
    return publish(command);
  }
  if (fields[0] == "P") {
    if (!parse_command_header(fields, command))
      return false;
    command.type = StoryboardCommandType::Parameter;
    if (fields[4] == "A")
      command.parameter = StoryboardParameter::Additive;
    else if (fields[4] == "H")
      command.parameter = StoryboardParameter::FlipHorizontal;
    else if (fields[4] == "V")
      command.parameter = StoryboardParameter::FlipVertical;
    else
      return false;
    return publish(command);
  }

  if (fields[0] == "M") {
    if (!parse_command_values(fields, command, 2))
      return false;
    if (map.storyboard_commands.size() - command_count < 2)
      return false;
    StoryboardCommand x = command;
    x.type = StoryboardCommandType::MoveX;
    StoryboardCommand y = command;
    y.type = StoryboardCommandType::MoveY;
    y.start_value[0] = y.start_value[1];
    y.end_value[0] = y.end_value[1];
    publish(x);
    return publish(y);
  }

  size_t value_count = 1;
  if (fields[0] == "V") {
    command.type = StoryboardCommandType::VectorScale;
    value_count = 2;
  } else if (fields[0] == "C") {
    command.type = StoryboardCommandType::Colour;
    value_count = 3;
  } else if (fields[0] == "F")
    command.type = StoryboardCommandType::Fade;
  else if (fields[0] == "S")
    command.type = StoryboardCommandType::Scale;
  else if (fields[0] == "R")
    command.type = StoryboardCommandType::Rotate;
  else if (fields[0] == "MX")
    command.type = StoryboardCommandType::MoveX;
  else if (fields[0] == "MY")
    command.type = StoryboardCommandType::MoveY;
  else
    return false;

  if (!parse_command_values(fields, command, value_count))
    return false;
  if (command.type == StoryboardCommandType::Rotate) {
    constexpr float radians_to_degrees = 180.0f / std::numbers::pi_v<float>;
    command.start_value[0] *= radians_to_degrees;
    command.end_value[0] *= radians_to_degrees;
  } else if (command.type == StoryboardCommandType::Colour) {
    for (size_t i = 0; i < 3; ++i) {
      command.start_value[i] /= 255.0f;
      command.end_value[i] /= 255.0f;
    }
  }
  return publish(command);
}

inline void parse_event_line(Beatmap& map,
                             size_t& break_count,
                             size_t& element_count,
                             size_t& command_count,
                             size_t& current_element,
                             const char* p,
                             size_t len,
                             int time_offset) {
  for (size_t i = 1; i < len; ++i) {
    if (p[i - 1] == '/' && p[i] == '/') {
      len = i - 1;
      break;
    }
  }
  while (len && (p[len - 1] == ' ' || p[len - 1] == '\t'))
    --len;
  size_t depth = 0;
  while (depth < len && (p[depth] == ' ' || p[depth] == '_'))
    ++depth;
  const EventFields fields = split_event_fields({p + depth, len - depth});
  if (!depth) {
    current_element = static_cast<size_t>(-1);
    bool accepted = false;
    bool storyboard_line = false;
    if (fields[0] == "0" || fields[0] == "Background")
      accepted = parse_background_event(map, fields);
    else if (fields[0] == "1" || fields[0] == "Video") {
      const size_t before = element_count;
      accepted = parse_video_event(map, element_count, command_count, fields);
      if (accepted && element_count != before)
        current_element = element_count - 1;
    } else if (fields[0] == "2" || fields[0] == "Break")
      accepted = parse_break_event(map, break_count, fields, time_offset);
    else if (fields[0] == "3" || fields[0] == "Colour") {
      storyboard_line = true;
      accepted = true;  // Ignored by the current official storyboard decoder.
    } else if (fields[0] == "4" || fields[0] == "Sprite" || fields[0] == "5" ||
               fields[0] == "Sample" || fields[0] == "6" || fields[0] == "Animation") {
      storyboard_line = true;
      accepted = parse_storyboard_element(map, element_count, command_count, fields);
      if (accepted && map.storyboard_elements[element_count - 1].type !=
                          StoryboardElementType::Sample)
        current_element = element_count - 1;
    } else {
      storyboard_line = true;
      accepted = true;  // Preserve FOSU's tolerant unknown-event behavior.
    }
    map.stats.storyboard_lines += storyboard_line;
    if (!accepted)
      ++map.stats.malformed_lines;
    return;
  }
  ++map.stats.storyboard_lines;
  if (!parse_storyboard_command(map, current_element, command_count, fields, depth))
    ++map.stats.malformed_lines;
}

inline const char* parse_events_section(Beatmap& map,
                                        size_t& break_count,
                                        size_t& element_count,
                                        size_t& command_count,
                                        const char* p,
                                        const char* file_end,
                                        int time_offset = 0) {
  size_t current_element = static_cast<size_t>(-1);
  return for_each_section_line(p, file_end, [&](std::string_view line) {
    parse_event_line(map, break_count, element_count, command_count, current_element,
                     line.data(), line.size(), time_offset);
  });
}

}  // namespace fosu::internal
