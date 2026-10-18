#pragma once

// osu!stable's osu!standard slider calculations, from SliderOsu's
// UpdateCalculations: its own curve approximations, then whole-millisecond
// end times and scoring points. Stable works in absolute playfield
// coordinates; paths and events are stored relative to the head, as lazer's
// are.

#include <fosu/arena.h>
#include <fosu/beatmap.h>
#include <fosu/compiler.h>
#include <fosu/enums.h>
#include <fosu/legacy_rules.h>
#include <fosu/slider_event.h>
#include <fosu/slider_geometry.h>
#include <fosu/slider_path.h>
#include <fosu/types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <span>

namespace fosu::internal {

// Stable's XNA vector arithmetic is single precision, one operation at a time.
FOSU_FP_CONTRACT_OFF_BEGIN

// .NET's (int) conversion on x86: truncation toward zero, with NaN and
// out-of-range values becoming int.MinValue.
inline i32 stable_int(f64 value) {
  return value > -2147483649.0 && value < 2147483648.0
             ? static_cast<i32>(value)
             : std::numeric_limits<i32>::min();
}

// .NET's unchecked int arithmetic, which wraps.
inline i32 wrapping_add(i32 a, i32 b) {
  return static_cast<i32>(static_cast<u32>(a) + static_cast<u32>(b));
}

inline i32 wrapping_sub(i32 a, i32 b) {
  return static_cast<i32>(static_cast<u32>(a) - static_cast<u32>(b));
}

// Stable's work grows with path detail, spans and ticks. A map demanding more
// than this fails the parse, as lazer's event expansion does.
struct StableBudget {
  size_t work = size_t{1} << 28;
  size_t events = size_t{1} << 20;

  bool spend(size_t& pool, size_t amount) {
    if (amount > pool)
      return false;
    pool -= amount;
    return true;
  }
};

// One straight piece of a stable path. Stable keeps each piece's ends
// separately, so consecutive pieces can leave sub-pixel gaps.
struct StableLine {
  CurvePoint p1, p2;

  f32 length() const { return (p2 - p1).length(); }
};

// Counts a path's lines when `lines` is null, and stores them otherwise.
struct StableLines {
  StableLine* lines = nullptr;
  size_t      count = 0;

  void add(CurvePoint p1, CurvePoint p2) {
    if (lines)
      lines[count] = {p1, p2};
    ++count;
  }
};

// Lines between consecutive points, for the shared Bézier approximation.
struct StableCurve {
  StableLines& out;
  CurvePoint   last{};
  bool         started = false;

  void begin_segment() {}

  void append(CurvePoint point) {
    if (started)
      out.add(last, point);
    last = point;
    started = true;
  }
};

// XNA's Vector2.Normalize.
inline CurvePoint stable_normalize(CurvePoint v) {
  const f32 inverse = 1.0f / std::sqrt(v.x * v.x + v.y * v.y);
  return {v.x * inverse, v.y * inverse};
}

// XNA's Vector2.CatmullRom, per component.
inline f32 stable_catmull(f32 v1, f32 v2, f32 v3, f32 v4, f32 amount) {
  const f32 squared = amount * amount;
  const f32 cubed = amount * squared;
  return 0.5f *
         ((((2.0f * v2) + ((-v1 + v3) * amount)) +
           (((((2.0f * v1) - (5.0f * v2)) + (4.0f * v3)) - v4) * squared)) +
          ((((-v1 + (3.0f * v2)) - (3.0f * v3)) + v4) * cubed));
}

inline CurvePoint stable_catmull(CurvePoint v1,
                                 CurvePoint v2,
                                 CurvePoint v3,
                                 CurvePoint v4,
                                 f32        amount) {
  return {stable_catmull(v1.x, v2.x, v3.x, v4.x, amount),
          stable_catmull(v1.y, v2.y, v3.y, v4.y, amount)};
}

inline constexpr i32 kStableCurveDetail = 50;  // SLIDER_DETAIL_LEVEL

// A straight line as stable adds one: a single piece interpolated between
// its ends.
inline void add_stable_straight_line(CurvePoint   p1,
                                     CurvePoint   p2,
                                     StableLines& out) {
  out.add(p1 + (p2 - p1) * 0.0f, p1 + (p2 - p1) * 1.0f);
}

// The evenly sampled curve v9 maps use, which stops short of its last point
// ("sliders will be 1/50 too short").
inline bool add_stable_wrong_bezier(std::span<const CurvePoint> points,
                                    StableLines&                out,
                                    StableBudget&               budget,
                                    Arena*                      arena) {
  const size_t count = points.size();
  const size_t steps = kStableCurveDetail * count;
  if (count > 1 << 16 || !budget.spend(budget.work, steps * count * count))
    return false;
  const TempArena temp{arena};
  auto*           working = arena_push_array<CurvePoint>(arena, count);
  if (!working)
    return false;
  StableCurve curve{out};
  for (size_t step = 0; step < steps; ++step) {
    std::copy(points.begin(), points.end(), working);
    const f32 t = static_cast<f32>(step) / static_cast<f32>(steps);
    for (size_t level = 0; level < count; ++level)
      for (size_t i = 0; i + level + 1 < count; ++i)
        working[i] = working[i] + (working[i + 1] - working[i]) * t;
    curve.append(working[0]);
  }
  return true;
}

inline bool add_stable_bezier(std::span<const CurvePoint> points,
                              StableLines&                out,
                              Arena*                      arena) {
  const TempArena temp{arena};
  StableCurve     curve{out};
  return bezier_distance(points, curve, arena);
}

// Bézier paths split into segments at repeated points; each version's rules.
inline bool add_stable_bezier_path(std::span<const CurvePoint> points,
                                   i32                         version,
                                   StableLines&                out,
                                   StableBudget&               budget,
                                   Arena*                      arena) {
  const size_t count = points.size();
  size_t       last = 0;
  for (size_t i = 0; i < count; ++i) {
    if (version > 6) {
      const bool multipart = i + 2 < count && points[i] == points[i + 1];
      if (!multipart && i != count - 1)
        continue;
      const auto segment = points.subspan(last, i - last + 1);
      if (version > 8 && segment.size() == 2) {
        add_stable_straight_line(segment[0], segment[1], out);
      } else if (version > 8 && version < 10) {
        if (!add_stable_wrong_bezier(segment, out, budget, arena))
          return false;
      } else if (!add_stable_bezier(segment, out, arena)) {
        return false;
      }
      if (multipart)
        ++i;  // The repeated point starts the next segment.
      last = i;
    } else if ((i > 0 && points[i] == points[i - 1]) || i == count - 1) {
      // Older maps split after a repeated point, keeping both copies.
      if (!add_stable_bezier(points.subspan(last, i - last + 1), out, arena))
        return false;
      last = i;
    }
  }
  return true;
}

// The circle through three points, in chords of 8 to 16 pixels of arc; under
// 16 pixels, one straight line.
inline bool add_stable_arc(CurvePoint    a,
                           CurvePoint    b,
                           CurvePoint    c,
                           StableLines&  out,
                           StableBudget& budget) {
  // Stable wraps angles with a single-precision pi.
  constexpr f64 kTwoPi = 2 * static_cast<f64>(3.14159274f);
  const f32 d = 2 * (a.x * (b.y - c.y) + b.x * (c.y - a.y) + c.x * (a.y - b.y));
  const f32 a_squared = a.squared_length();
  const f32 b_squared = b.squared_length();
  const f32 c_squared = c.squared_length();
  const CurvePoint centre{(a_squared * (b.y - c.y) + b_squared * (c.y - a.y) +
                           c_squared * (a.y - b.y)) /
                              d,
                          (a_squared * (c.x - b.x) + b_squared * (a.x - c.x) +
                           c_squared * (b.x - a.x)) /
                              d};
  const f32        radius = (centre - a).length();
  const auto       angle = [&](CurvePoint point) {
    return std::atan2(static_cast<f64>(point.y - centre.y),
                      static_cast<f64>(point.x - centre.x));
  };
  const f64 t_initial = angle(a);
  f64       t_mid = angle(b);
  f64       t_final = angle(c);
  while (t_mid < t_initial)
    t_mid += kTwoPi;
  while (t_final < t_initial)
    t_final += kTwoPi;
  if (t_mid > t_final)
    t_final -= kTwoPi;
  const i32 segments =
      stable_int(std::abs((t_final - t_initial) * radius) * 0.125f);
  if (segments > 1 && !budget.spend(budget.work, static_cast<size_t>(segments)))
    return false;
  CurvePoint last = a;
  for (i32 i = 1; i < segments; ++i) {
    const f64        progress = static_cast<f64>(i) / segments;
    const f64        t = t_final * progress + t_initial * (1 - progress);
    const CurvePoint point =
        CurvePoint{static_cast<f32>(std::cos(t) * radius),
                   static_cast<f32>(std::sin(t) * radius)} +
        centre;
    out.add(last, point);
    last = point;
  }
  out.add(last, c);
  return true;
}

inline bool stable_straight(CurvePoint a, CurvePoint b, CurvePoint c) {
  return (b.x - a.x) * (c.y - a.y) - (c.x - a.x) * (b.y - a.y) == 0.0f;
}

// A slider's lines through its absolute control points, head first.
inline bool add_stable_path(std::span<const CurvePoint> points,
                            CurveType                   type,
                            i32                         version,
                            StableLines&                out,
                            StableBudget&               budget,
                            Arena*                      arena) {
  const size_t count = points.size();
  if (type == CurveType::Catmull) {
    if (count > 1 &&
        !budget.spend(budget.work, (count - 1) * kStableCurveDetail))
      return false;
    for (size_t j = 0; j + 1 < count; ++j) {
      const auto v1 = j >= 1 ? points[j - 1] : points[j];
      const auto v2 = points[j];
      const auto v3 = points[j + 1];
      const auto v4 = j + 2 < count ? points[j + 2] : v3 + (v3 - v2);
      for (i32 k = 0; k < kStableCurveDetail; ++k)
        out.add(stable_catmull(v1, v2, v3, v4,
                               static_cast<f32>(k) / kStableCurveDetail),
                stable_catmull(v1, v2, v3, v4,
                               static_cast<f32>(k + 1) / kStableCurveDetail));
    }
    return true;
  }
  if (type == CurveType::PerfectCurve && count == 3 &&
      !stable_straight(points[0], points[1], points[2]))
    return add_stable_arc(points[0], points[1], points[2], out, budget);
  if (type == CurveType::Linear ||
      (type == CurveType::PerfectCurve && count <= 3)) {
    for (size_t i = 1; i < count; ++i)
      add_stable_straight_line(points[i - 1], points[i], out);
    return true;
  }
  return add_stable_bezier_path(points, version, out, budget, arena);
}

// Timing points as stable orders them: by time, uninherited first.
inline std::span<const TimingPoint> stable_timing_points(const Beatmap& map,
                                                         Arena*         arena) {
  const auto compare = [](const TimingPoint& a, const TimingPoint& b) {
    return a.time < b.time ||
           (a.time == b.time && a.uninherited && !b.uninherited);
  };
  if (std::is_sorted(map.timing_points.begin(), map.timing_points.end(),
                     compare))
    return map.timing_points;
  auto* points = arena_push_array<TimingPoint>(arena, map.timing_points.size());
  if (!points)
    return {};
  std::copy(map.timing_points.begin(), map.timing_points.end(), points);
  const std::span<TimingPoint> sorted{points, map.timing_points.size()};
  if (!merge_sort(sorted, arena, compare))
    return {};
  return sorted;
}

// A slider speed multiplier, as stable clamps an inherited point's.
inline f32 stable_bpm_multiplier(const TimingPoint& point) {
  if (point.beat_length >= 0)
    return 1;
  return std::clamp(static_cast<f32>(-point.beat_length), 10.0f, 1000.0f) /
         100.0f;
}

// The timing points in effect at a time: stable's last uninherited and last
// inherited point at or before it, each the first point if none is, and the
// last point of either kind (BeatLengthAt and ControlPointAt).
struct StableTimingCursor {
  std::span<const TimingPoint> points;
  size_t                       next = 0;
  size_t                       uninherited = 0;
  size_t                       inherited = 0;

  void advance(f64 time) {
    for (; next < points.size() && points[next].time <= time; ++next)
      (points[next].uninherited ? uninherited : inherited) = next;
  }

  f64 beat_length() const {
    if (points.empty())
      return 0;
    f64 multiplier = 1;
    if (inherited > uninherited && points[inherited].beat_length < 0)
      multiplier = stable_bpm_multiplier(points[inherited]);
    return points[uninherited].beat_length * multiplier;
  }

  f32 bpm_multiplier() const {
    if (points.empty())
      return 1;
    return stable_bpm_multiplier(points[next ? next - 1 : 0]);
  }
};

// Where a stable slider ends, for stacking. Absolute and unstacked.
struct StableSliderEnd {
  CurvePoint end_position;  // after the last span
  CurvePoint path_end;      // the far end of the path
};

// A tick or a span's end, as stable's scoring walk finds it.
struct StableScoringPoint {
  i32        time;
  i32        span;
  CurvePoint position;
  f64        progress;
};

// What a stable slider's timing walk works out. `complete` is false when a
// visit stopped it.
struct StableWalk {
  i32        end_time;
  CurvePoint end_position;
  bool       complete = true;
};

// Stable's walk along a slider's lines, span by span: the time its ball
// takes over each line, and its ticks, from the distance travelled since the
// last. Calls visit(point) for each scoring point, stopping if it returns
// false, and span_start(span, time) at each span's start.
template <typename Visit, typename SpanStart>
inline StableWalk walk_stable_slider(std::span<const StableLine> lines,
                                     std::span<const f64>        cumulative,
                                     i32                         spans,
                                     i32                         start_time,
                                     f64                         velocity,
                                     f64                         tick_distance,
                                     Visit                       visit,
                                     SpanStart                   span_start) {
  const f64  total = cumulative.back();
  const auto line_count = static_cast<ptrdiff_t>(lines.size());
  const auto time_at_length = [&](f32 length) {
    return stable_int(start_time + (length / velocity) * 1000);
  };
  f64        scoring_length_total = 0;
  f64        current_time = start_time;
  f64        scoring = 0;  // distance since the last tick
  CurvePoint p1{}, p2{};
  for (i32 span = 0; span < spans; ++span) {
    span_start(span, stable_int(current_time));
    f64        distance_to_end = total;
    bool       skip_tick = false;
    const f64  min_tick_distance_from_end = 0.01 * velocity;
    const bool reverse = span % 2 == 1;
    for (ptrdiff_t j = reverse ? line_count - 1 : 0;
         j != (reverse ? -1 : line_count); j += reverse ? -1 : 1) {
      const f64 before = j == 0 ? 0 : cumulative[j - 1];
      const f32 distance = static_cast<f32>(cumulative[j] - before);
      p1 = reverse ? lines[j].p2 : lines[j].p1;
      p2 = reverse ? lines[j].p1 : lines[j].p2;
      current_time += 1000.0f * distance / velocity;
      scoring += distance;
      while (scoring >= tick_distance && !skip_tick) {
        scoring_length_total += tick_distance;
        scoring -= tick_distance;
        distance_to_end -= tick_distance;
        skip_tick = distance_to_end <= min_tick_distance_from_end;
        if (skip_tick)
          break;
        const f32 ratio = 1 - static_cast<f32>(scoring / (p2 - p1).length());
        const f64 along = ratio * static_cast<f64>(distance);
        if (!visit(StableScoringPoint{
                time_at_length(static_cast<f32>(scoring_length_total)), span,
                p1 + (p2 - p1) * ratio,
                (reverse ? cumulative[j] - along : before + along) / total}))
          return {0, p2, false};
      }
    }
    scoring_length_total += scoring;
    if (!visit(StableScoringPoint{
            time_at_length(static_cast<f32>(scoring_length_total)), span, p2,
            static_cast<f64>(!reverse)}))
      return {0, p2, false};
    // A tick too near the end isn't mirrored on the way back.
    if (skip_tick) {
      scoring = 0;
    } else {
      scoring_length_total -= tick_distance - scoring;
      scoring = tick_distance - scoring;
    }
  }
  return {stable_int(current_time), p2};
}

// The absolute point `distance` along a slider's lines.
inline CurvePoint stable_position_at(std::span<const StableLine> lines,
                                     std::span<const f64>        cumulative,
                                     f64                         distance) {
  const auto found =
      std::lower_bound(cumulative.begin(), cumulative.end(), distance);
  const size_t j = std::min(static_cast<size_t>(found - cumulative.begin()),
                            lines.size() - 1);
  const f64    start = j == 0 ? 0 : cumulative[j - 1];
  const f64    length = cumulative[j] - start;
  if (length <= 0)
    return lines[j].p2;
  const auto& line = lines[j];
  return line.p1 + (line.p2 - line.p1) *
                       static_cast<f32>(
                           std::clamp((distance - start) / length, 0.0, 1.0));
}

struct StableOutputs {
  bool paths;
  bool events;
  bool end_times;
};

// One slider's stable calculations. Stable does its whole-millisecond
// arithmetic without the 24 ms fosu adds to a pre-v5 map's times.
inline bool calculate_stable_slider(
    Beatmap&                          map,
    HitObject&                        object,
    StableTimingCursor&               timing,
    StableOutputs                     outputs,
    StableBudget&                     budget,
    std::span<SliderPath>             paths,
    std::span<std::span<SliderEvent>> event_ranges,
    std::span<f64>                    velocities,
    std::span<StableSliderEnd>        ends,
    Arena*                            result_arena,
    Arena*                            scratch_arena) {
  const Slider&    slider = map.sliders[object.slider];
  const TempArena  temp{scratch_arena};

  // Old maps list the head among the points; newer ones leave it out.
  const CurvePoint head{object.x, object.y};
  const auto       control =
      map.slider_points.subspan(slider.point_begin, slider.point_count);
  const bool listed_head =
      !control.empty() && control[0].x == head.x && control[0].y == head.y;
  const size_t count = control.size() + !listed_head;
  auto*        points = arena_push_array<CurvePoint>(scratch_arena, count);
  if (!points)
    return false;
  points[0] = head;
  for (size_t i = listed_head; i < control.size(); ++i)
    points[i + !listed_head] = {control[i].x, control[i].y};
  const std::span<const CurvePoint> control_points{points, count};

  const i32                         version = map.format_version;
  StableLines                       counted;
  if (!add_stable_path(control_points, slider.curve_type, version, counted,
                       budget, scratch_arena) ||
      !budget.spend(budget.work, counted.count))
    return false;
  StableLines path{arena_push_array<StableLine>(scratch_arena, counted.count)};
  if (!path.lines && counted.count)
    return false;
  if (!add_stable_path(control_points, slider.curve_type, version, path, budget,
                       scratch_arena))
    return false;

  // Velocity in pixels per second, and tick spacing, at the slider's start.
  const f64 shift = map.format_version < 5 ? 24 : 0;
  const i32 start_time = stable_int(object.time - shift);
  timing.advance(start_time + shift);
  const f64 scoring_distance =
      100 * map.slider_multiplier / map.slider_tick_rate;
  const f64 beat_length = timing.beat_length();
  const f64 velocity =
      beat_length > 0
          ? scoring_distance * map.slider_tick_rate * (1000.0f / beat_length)
          : scoring_distance * map.slider_tick_rate;
  // From v8, ticks stay evenly spaced in time at any slider speed.
  f64 tick_distance = map.format_version < 8
                          ? scoring_distance
                          : scoring_distance / timing.bpm_multiplier();

  // Trim or extend the last lines to the declared length.
  f64 total = 0;
  for (size_t i = 0; i < path.count; ++i)
    total += path.lines[i].length();
  if (total > 0) {
    const f64 spatial_length = slider.length == 0 ? total : slider.length;
    if (tick_distance > spatial_length)
      tick_distance = spatial_length;
    constexpr f64 kMinSegmentLength = 0.0001;
    f64           excess = total - spatial_length;
    while (path.count) {
      StableLine& last = path.lines[path.count - 1];
      const f32   last_length = (last.p1 - last.p2).length();
      if (last_length > excess + kMinSegmentLength) {
        if (last.p2 != last.p1)
          last.p2 = last.p1 + stable_normalize(last.p2 - last.p1) *
                                  (last.length() - static_cast<f32>(excess));
        break;
      }
      --path.count;
      excess -= last_length;
    }
  }
  const std::span<const StableLine> lines{path.lines, path.count};

  velocities[object.slider] = velocity / 1000;
  StableWalk walk{start_time, head};
  auto*      cumulative = arena_push_array<f64>(scratch_arena, lines.size());
  i32*       span_starts = arena_push_array<i32>(scratch_arena, slider.slides);
  if ((!cumulative && !lines.empty()) || !span_starts)
    return false;
  size_t point_count = 0;
  if (!lines.empty()) {
    total = 0;
    for (size_t i = 0; i < lines.size(); ++i)
      cumulative[i] = total += lines[i].length();
    if (!budget.spend(budget.work, lines.size() * slider.slides))
      return false;
    walk = walk_stable_slider(
        lines, {cumulative, lines.size()}, slider.slides, start_time, velocity,
        tick_distance,
        [&](const StableScoringPoint&) {
          ++point_count;
          return budget.spend(budget.events, 1);
        },
        [&](i32 span, i32 time) {
          span_starts[span] = time;
        });
    if (!walk.complete)
      return false;
  } else {
    std::fill_n(span_starts, slider.slides, start_time);
  }
  if (outputs.end_times)
    object.end_time = walk.end_time + shift;
  if (!ends.empty())
    ends[object.slider] = {walk.end_position,
                           lines.empty() ? head : lines.back().p2};

  if (outputs.paths) {
    // Each line's start, where it leaves a gap after the previous line, adds
    // a point that adds no length; lengths are stable's own.
    size_t gaps = 0;
    for (size_t i = 1; i < lines.size(); ++i)
      gaps += lines[i].p1 != lines[i - 1].p2;
    const size_t size = lines.empty() ? 1 : lines.size() + 1 + gaps;
    auto*        output = arena_push_array<PathPoint>(result_arena, size);
    auto*        lengths = arena_push_array<f64>(result_arena, size);
    if (!output || !lengths)
      return false;
    output[0] = (lines.empty() ? head : lines[0].p1) - head;
    lengths[0] = 0;
    for (size_t i = 0, next = 1; i < lines.size(); ++i) {
      if (i && lines[i].p1 != lines[i - 1].p2) {
        output[next] = lines[i].p1 - head;
        lengths[next++] = cumulative[i - 1];
      }
      output[next] = lines[i].p2 - head;
      lengths[next++] = cumulative[i];
    }
    paths[object.slider] = {{output, size}, {lengths, size}};
  }

  if (!outputs.events)
    return true;
  const i32    spans = slider.slides;
  const size_t event_count = point_count + 2;
  auto* events = arena_push_array<SliderEvent>(result_arena, event_count);
  if (!events)
    return false;
  const auto event = [&](SliderEventType type, f64 time, i32 span, f64 progress,
                         CurvePoint position) {
    return SliderEvent{type,     time + shift,
                       span,     span_starts[span] + shift,
                       progress, position - head};
  };
  events[0] = event(SliderEventType::Head, start_time, 0, 0, head);
  if (point_count) {
    // Stable scores every (count / spans)th point as a repeat, which is not
    // always a span's end when spans hold different numbers of ticks.
    const size_t per_span = point_count / spans;
    size_t       next = 1;
    walk_stable_slider(
        lines, {cumulative, lines.size()}, spans, start_time, velocity,
        tick_distance,
        [&](const StableScoringPoint& point) {
          const bool repeat = per_span && next % per_span == 0;
          events[next++] =
              event(repeat ? SliderEventType::Repeat : SliderEventType::Tick,
                    point.time, point.span, point.progress, point.position);
          return true;
        },
        [](i32, i32) {});
    // The last point, judged a little early and inaudibly, replaces the end.
    const i32 length = wrapping_sub(walk.end_time, start_time);
    const i32 final_span = spans - 1;
    const i32 final_start = span_starts[final_span];
    const i32 last_time = stable_int(events[point_count].time - shift);
    const i32 legacy_time = std::max(wrapping_add(start_time, length / 2),
                                     wrapping_sub(last_time, 36));
    f64       progress = static_cast<f64>(spans % 2);
    if (walk.end_time > final_start) {
      progress = std::clamp((static_cast<f64>(legacy_time) - final_start) /
                                (static_cast<f64>(walk.end_time) - final_start),
                            0.0, 1.0);
      if (spans % 2 == 0)
        progress = 1 - progress;
    }
    events[point_count] = event(
        SliderEventType::LegacyLastTick, legacy_time, final_span, progress,
        stable_position_at(lines, {cumulative, lines.size()},
                           progress * total));
  }
  events[event_count - 1] =
      event(SliderEventType::Tail, walk.end_time, spans - 1,
            static_cast<f64>(spans % 2), walk.end_position);
  event_ranges[object.slider] = {events, event_count};
  return true;
}

// Stable's paths, end times, events and slider velocities for an
// osu!standard map. `ends`, when not empty, receives what stacking needs.
inline bool set_stable_slider_calculations(Beatmap&                   map,
                                           StableOutputs              outputs,
                                           std::span<StableSliderEnd> ends,
                                           Arena* result_arena,
                                           Arena* scratch_arena) {
  if (map.sliders.empty())
    return true;
  const size_t sliders = map.sliders.size();
  auto*        paths = outputs.paths
                           ? arena_push_array<SliderPath>(result_arena, sliders)
                           : nullptr;
  auto* ranges = outputs.events ? arena_push_array<std::span<SliderEvent>>(
                                      result_arena, sliders)
                                : nullptr;
  auto* velocities = arena_push_array<f64>(result_arena, sliders);
  const TempArena temp{scratch_arena};
  const auto      timing_points = stable_timing_points(map, scratch_arena);
  if ((outputs.paths && !paths) || (outputs.events && !ranges) || !velocities ||
      (timing_points.empty() && !map.timing_points.empty()))
    return false;
  StableTimingCursor timing{timing_points};
  StableBudget       budget;
  for (auto& object : map.hit_objects) {
    if (object.slider == HitObject::kNoSlider)
      continue;
    if (!calculate_stable_slider(
            map, object, timing, outputs, budget, {paths, paths ? sliders : 0},
            {ranges, ranges ? sliders : 0}, {velocities, sliders}, ends,
            result_arena, scratch_arena))
      return false;
  }
  if (paths)
    map.slider_paths = {paths, sliders};
  if (ranges)
    map.slider_events = {ranges, sliders};
  if (outputs.end_times)
    map.slider_velocities = {velocities, sliders};
  return true;
}

FOSU_FP_CONTRACT_OFF_END

}  // namespace fosu::internal
