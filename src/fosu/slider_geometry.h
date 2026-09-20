#pragma once

// Curve approximation adapted from osu-framework (2026.807.0) and osu!.
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

#include <fosu/beatmap.h>
#include <fosu/types.h>

#include <cmath>
#include <cstring>
#include <numbers>

namespace fosu::internal {

// Match the official decoder's separate single-precision operations.
#if defined(__clang__)
#pragma clang fp contract(off)
#elif defined(__GNUC__)
#pragma GCC push_options
#pragma GCC optimize("fp-contract=off")
#endif

// osu! computes curve geometry in single precision, relative to the slider
// head.
using CurvePoint = PathPoint;

// Duration only needs the distance and final edge, not a retained path array.
struct CurveDistance {
  f64        length = 0;
  CurvePoint last{}, previous{};
  size_t     count = 0;
  bool       first_in_segment = false;

  void begin_segment() { first_in_segment = true; }

  void append(CurvePoint point) {
    if (first_in_segment && count && last == point) {
      first_in_segment = false;
      return;
    }
    first_in_segment = false;
    if (count)
      length += (point - last).length();
    previous = last;
    last = point;
    ++count;
  }
};

inline void subdivide_bezier(std::span<const CurvePoint> points,
                             CurvePoint*                 left,
                             CurvePoint*                 right,
                             CurvePoint*                 midpoints) {
  const size_t count = points.size();
  if (count == 4) {
    const auto a = (points[0] + points[1]) * 0.5f;
    const auto b = (points[1] + points[2]) * 0.5f;
    const auto c = (points[2] + points[3]) * 0.5f;
    const auto d = (a + b) * 0.5f;
    const auto e = (b + c) * 0.5f;
    const auto f = (d + e) * 0.5f;
    left[0] = points[0];
    left[1] = a;
    left[2] = d;
    left[3] = f;
    right[0] = f;
    right[1] = e;
    right[2] = c;
    right[3] = points[3];
    return;
  }
  std::copy(points.begin(), points.end(), midpoints);
  for (size_t i = 0; i < count; ++i) {
    left[i] = midpoints[0];
    right[count - i - 1] = midpoints[count - i - 1];
    for (size_t j = 0; j < count - i - 1; ++j)
      midpoints[j] = (midpoints[j] + midpoints[j + 1]) * 0.5f;
  }
}

template <typename Curve>
inline bool bezier_distance(std::span<const CurvePoint> points,
                            Curve&                      distance,
                            Arena*                      arena);

template <typename Curve>
inline bool bspline_distance(std::span<const CurvePoint> points,
                             u32                         requested_degree,
                             Curve&                      distance,
                             Arena*                      arena) {
  if (points.size() < 2) {
    for (auto point : points)
      distance.append(point);
    return true;
  }
  const size_t point_count = points.size() - 1;
  const size_t degree = std::min<size_t>(requested_degree, point_count);
  if (!degree)
    return false;
  if (degree == point_count)
    return bezier_distance(points, distance, arena);

  auto* work = arena_push_array<CurvePoint>(arena, points.size());
  auto* bezier = arena_push_array<CurvePoint>(arena, degree + 1);
  if (!work || !bezier)
    return false;
  std::copy(points.begin(), points.end(), work);
  for (size_t i = 0; i < point_count - degree; ++i) {
    bezier[0] = work[i];
    for (size_t j = 0; j < degree - 1; ++j) {
      bezier[j + 1] = work[i + 1];
      for (size_t k = 1; k < degree - j; ++k) {
        const size_t weight = std::min(k, point_count - degree - i);
        work[i + k] =
            (work[i + k] * static_cast<f32>(weight) + work[i + k + 1]) /
            static_cast<f32>(weight + 1);
      }
    }
    bezier[degree] = work[i + 1];
    distance.begin_segment();
    if (!bezier_distance({bezier, degree + 1}, distance, arena))
      return false;
  }
  distance.begin_segment();
  return bezier_distance({work + point_count - degree, degree + 1}, distance,
                         arena);
}

template <typename Curve>
inline bool bezier_distance(std::span<const CurvePoint> points,
                            Curve&                      distance,
                            Arena*                      arena) {
  const size_t count = points.size();
  auto*        work = arena_push_array<CurvePoint>(arena, count * 4);
  if (!work)
    return false;
  auto* left = work + count;
  auto* right = left + count;
  auto* midpoints = right + count;
  std::copy(points.begin(), points.end(), work);

  // Depth-first subdivision, with reusable arena buffers for pending right
  // halves. f32 coordinates converge long before 64 subdivisions.
  CurvePoint* pending[64]{};
  size_t      depth = 0;
  for (;;) {
    bool flat = true;
    for (size_t i = 1; i + 1 < count; ++i) {
      if ((work[i - 1] - work[i] * 2 + work[i + 1]).squared_length() > 0.25f) {
        flat = false;
        break;
      }
    }
    subdivide_bezier({work, count}, left, right, midpoints);
    if (flat) {
      distance.append(work[0]);
      for (size_t i = 1; i + 1 < count; ++i) {
        const auto at = [&](size_t index) {
          return index < count ? left[index] : right[index - count + 1];
        };
        distance.append((at(2 * i - 1) + at(2 * i) * 2 + at(2 * i + 1)) *
                        0.25f);
      }
      if (!depth)
        break;
      std::copy_n(pending[--depth], count, work);
    } else {
      if (depth == std::size(pending))
        return false;
      if (!pending[depth])
        pending[depth] = arena_push_array<CurvePoint>(arena, count);
      if (!pending[depth])
        return false;
      std::copy_n(right, count, pending[depth++]);
      std::copy_n(left, count, work);
    }
  }
  distance.append(points.back());
  return true;
}

inline CurvePoint catmull_point(CurvePoint a,
                                CurvePoint b,
                                CurvePoint c,
                                CurvePoint d,
                                f32        t) {
  const f32 t2 = t * t, t3 = t * t2;
  return (b * 2 + (c - a) * t + (a * 2 - b * 5 + c * 4 - d) * t2 +
          (b * 3 - a - c * 3 + d) * t3) *
         0.5f;
}

template <typename Curve>
inline void catmull_distance(std::span<const CurvePoint> points,
                             Curve&                      distance) {
  for (size_t i = 0; i + 1 < points.size(); ++i) {
    const auto a = points[i ? i - 1 : i], b = points[i], c = points[i + 1];
    const auto d = i + 2 < points.size() ? points[i + 2] : c * 2 - b;
    for (i32 sample = 0; sample < 100; ++sample) {
      const f32 t = static_cast<f32>((sample + 1) / 2) / 50;
      distance.append(catmull_point(a, b, c, d, t));
    }
  }
}

template <typename Curve>
inline bool circular_arc_distance(std::span<const CurvePoint> points,
                                  Curve&                      distance) {
  const auto a = points[0], b = points[1], c = points[2];
  const f32 divisor = 2 * (a.x * (b - c).y + b.x * (c - a).y + c.x * (a - b).y);
  const auto centre =
      CurvePoint{
          a.squared_length() * (b - c).y + b.squared_length() * (c - a).y +
              c.squared_length() * (a - b).y,
          a.squared_length() * (c - b).x + b.squared_length() * (a - c).x +
              c.squared_length() * (b - a).x} /
      divisor;
  const auto    first = a - centre, last = c - centre;
  const f32     radius = first.length();
  const f64     start = std::atan2(static_cast<f64>(first.y), first.x);
  f64           end = std::atan2(static_cast<f64>(last.y), last.x);
  constexpr f64 circle = 2 * std::numbers::pi;
  while (end < start)
    end += circle;
  f64 range = end - start, direction = 1;
  if ((c - a).y * (b - a).x - (c - a).x * (b - a).y < 0) {
    direction = -1;
    range = circle - range;
  }
  const f64 amount =
      2 * radius <= 0.1f
          ? 2
          : std::max(2.0, std::ceil(range / (2 * std::acos(static_cast<f64>(
                                                     1 - 0.1f / radius)))));
  if (!std::isfinite(amount) || amount >= 1000)
    return false;
  for (i32 i = 0; i < static_cast<i32>(amount); ++i) {
    const f64 theta = start + direction * (i / (amount - 1)) * range;
    distance.append(centre + CurvePoint{static_cast<f32>(std::cos(theta)),
                                        static_cast<f32>(std::sin(theta))} *
                                 radius);
  }
  return true;
}

template <typename Curve>
inline bool approximate_curve_segment(std::span<const CurvePoint> points,
                                      CurveType                   type,
                                      std::optional<u32>          degree,
                                      Curve&                      segment,
                                      Arena*                      arena) {
  if (points.size() == 1 || type == CurveType::Linear ||
      (points.size() == 2 && type != CurveType::Catmull)) {
    for (auto point : points)
      segment.append(point);
  } else if (type == CurveType::Catmull) {
    catmull_distance(points, segment);
  } else if (type != CurveType::PerfectCurve || points.size() != 3 ||
             !circular_arc_distance(points, segment)) {
    const u32 spline_degree = degree.value_or(points.size() - 1);
    if (!bspline_distance(points, spline_degree, segment, arena))
      return false;
  }
  return true;
}

template <typename Curve>
inline bool approximate_curve_segment(std::span<const CurvePoint> points,
                                      CurveType                   type,
                                      Curve&                      segment,
                                      Arena*                      arena) {
  return approximate_curve_segment(points, type, std::nullopt, segment, arena);
}

inline bool curve_segment_distance(std::span<const CurvePoint> points,
                                   CurveType                   type,
                                   CurveDistance&              distance,
                                   Arena*                      arena) {
  CurveDistance segment{.length = distance.length};
  if (!approximate_curve_segment(points, type, segment, arena))
    return false;
  // Adjacent segments share their first vertex. The last edge is unchanged
  // unless the new segment consists solely of that shared vertex.
  if (segment.count > 1 || !distance.count) {
    distance.previous = segment.previous;
    distance.last = segment.last;
    distance.count += segment.count;
  }
  distance.length = segment.length;
  return true;
}

inline CurveType legacy_curve_type(std::span<const CurvePoint> points,
                                   CurveType                   type) {
  if (type != CurveType::PerfectCurve)
    return type;
  if (points.size() != 3)
    return CurveType::Bezier;
  const f32 cross = (points[1].y - points[0].y) * (points[2].x - points[0].x) -
                    (points[1].x - points[0].x) * (points[2].y - points[0].y);
  return std::abs(cross) < 1e-3f ? CurveType::Linear : type;
}

inline CurveType lazer_curve_type(std::span<const CurvePoint> points,
                                  CurveType                   type) {
  return type == CurveType::PerfectCurve && points.size() > 3
             ? CurveType::Bezier
             : type;
}

inline bool calculate_legacy_slider_distance(std::span<const CurvePoint> points,
                                             CurveType                   type,
                                             CurveDistance& distance,
                                             Arena*         arena) {
  type = legacy_curve_type(points, type);
  size_t begin = 0;
  for (size_t i = 1; i < points.size(); ++i) {
    if (points[i] != points[i - 1] || i == points.size() - 1 ||
        (type == CurveType::Catmull && i > 1)) {
      continue;
    }
    if (i - begin > 1 &&
        !curve_segment_distance(points.subspan(begin, i - begin), type,
                                distance, arena)) {
      return false;
    }
    begin = i;
  }
  return curve_segment_distance(points.subspan(begin), type, distance, arena);
}

inline bool calculate_lazer_slider_distance(
    std::span<const CurvePoint>   points,
    std::span<const CurveSegment> segments,
    CurveType                     type,
    CurveDistance&                distance,
    Arena*                        arena) {
  if (segments.empty()) {
    type = lazer_curve_type(points, type);
    size_t begin = 0;
    for (size_t i = 1; i < points.size(); ++i) {
      if (points[i] != points[i - 1] || i == points.size() - 1)
        continue;
      if (i - begin > 1 &&
          !curve_segment_distance(points.subspan(begin, i - begin), type,
                                  distance, arena)) {
        return false;
      }
      begin = i;
    }
    return curve_segment_distance(points.subspan(begin), type, distance, arena);
  }

  for (size_t i = 0; i < segments.size(); ++i) {
    const auto&  source = segments[i];
    const size_t start = source.point_begin + (i == 0 ? 0 : 1);
    const size_t count = source.point_count + (i == 0 ? 1 : 0);
    const auto   segment_points = points.subspan(start, count);
    const auto   segment_type = lazer_curve_type(segment_points, source.type);
    distance.begin_segment();
    if (!approximate_curve_segment(segment_points, segment_type, source.degree,
                                   distance, arena)) {
      return false;
    }
  }
  return true;
}

inline Result<f64> slider_distance(const Beatmap&   map,
                                   const HitObject& object,
                                   Arena*           arena) {
  const auto& slider = map.sliders[object.slider];
  const auto  control_points =
      map.slider_points.subspan(slider.point_begin, slider.point_count);
  // A non-degenerate final linear edge can always reach the declared length.
  // No approximation or scratch allocation is needed to establish its distance.
  if (slider.length > 0 && !control_points.empty()) {
    const auto last = control_points.back();
    const auto previous = control_points.size() > 1
                              ? control_points[control_points.size() - 2]
                              : SliderPoint{object.x, object.y};
    if (last.x != previous.x || last.y != previous.y)
      return slider.length;
  }
  const TempArena temp{arena};
  auto* points = arena_push_array<CurvePoint>(arena, control_points.size() + 1);
  if (!points)
    return Error{ErrorCode::AllocationFailure};
  points[0] = {};
  for (size_t i = 0; i < control_points.size(); ++i)
    points[i + 1] = {static_cast<f32>(control_points[i].x - object.x),
                     static_cast<f32>(control_points[i].y - object.y)};
  const size_t                      count = control_points.size() + 1;
  CurveDistance                     distance;
  const std::span<const CurvePoint> relative_points{points, count};
  if (map.format_version >= 128) {
    const auto segments =
        map.slider_segments.subspan(slider.segment_begin, slider.segment_count);
    if (!calculate_lazer_slider_distance(relative_points, segments,
                                         slider.curve_type, distance, arena)) {
      return Error{ErrorCode::AllocationFailure};
    }
  } else {
    if (!calculate_legacy_slider_distance(relative_points, slider.curve_type,
                                          distance, arena)) {
      return Error{ErrorCode::AllocationFailure};
    }
  }
  // A missing/zero declared length uses the natural path. Otherwise osu! trims
  // or extends it, except when a duplicate final vertex prevents extension.
  if (slider.length > 0 && distance.count > 1 &&
      !(distance.last == distance.previous && slider.length > distance.length))
    return slider.length;
  return distance.length;
}

struct CurveVertexChunk {
  static constexpr size_t capacity = 64;

  CurveVertexChunk*       next = nullptr;
  size_t                  count = 0;
  CurvePoint              points[capacity];
};

struct CurveVertices {
  Arena*            arena;
  CurveVertexChunk* first = nullptr;
  CurveVertexChunk* last = nullptr;
  size_t            count = 0;
  CurvePoint        last_point{};
  bool              first_in_segment = true;
  bool              failed = false;

  void begin_segment() { first_in_segment = true; }

  void append(CurvePoint point) {
    const bool shared = first_in_segment && count && last_point == point;
    first_in_segment = false;
    if (shared || failed)
      return;
    if (!last || last->count == CurveVertexChunk::capacity) {
      auto* chunk = arena_push_array<CurveVertexChunk>(arena, 1);
      if (!chunk) {
        failed = true;
        return;
      }
      ::new (chunk) CurveVertexChunk;
      if (last)
        last->next = chunk;
      else
        first = chunk;
      last = chunk;
    }
    last->points[last->count++] = point;
    last_point = point;
    ++count;
  }

  void copy_to(CurvePoint* destination) const {
    for (const auto* chunk = first; chunk; chunk = chunk->next) {
      std::memcpy(destination, chunk->points,
                  chunk->count * sizeof(CurvePoint));
      destination += chunk->count;
    }
  }
};

inline bool calculate_legacy_slider_curve(std::span<const CurvePoint> points,
                                          CurveType                   type,
                                          CurveVertices&              curve,
                                          Arena*                      arena) {
  type = legacy_curve_type(points, type);
  size_t begin = 0;
  for (size_t i = 1; i <= points.size(); ++i) {
    if (i < points.size() &&
        (points[i] != points[i - 1] || i == points.size() - 1 ||
         (type == CurveType::Catmull && i > 1))) {
      continue;
    }
    if (i == points.size() || i - begin > 1) {
      curve.first_in_segment = i - begin > 1;
      if (!approximate_curve_segment(points.subspan(begin, i - begin), type,
                                     curve, arena) ||
          curve.failed) {
        return false;
      }
    }
    begin = i;
  }
  return true;
}

inline bool calculate_lazer_slider_curve(std::span<const CurvePoint>   points,
                                         std::span<const CurveSegment> segments,
                                         CurveType                     type,
                                         CurveVertices&                curve,
                                         Arena*                        arena) {
  if (segments.empty()) {
    type = lazer_curve_type(points, type);
    size_t begin = 0;
    for (size_t i = 1; i <= points.size(); ++i) {
      if (i < points.size() &&
          (points[i] != points[i - 1] || i == points.size() - 1)) {
        continue;
      }
      if (i == points.size() || i - begin > 1) {
        curve.first_in_segment = i - begin > 1;
        if (!approximate_curve_segment(points.subspan(begin, i - begin), type,
                                       curve, arena) ||
            curve.failed) {
          return false;
        }
      }
      begin = i;
    }
    return true;
  }

  for (size_t i = 0; i < segments.size(); ++i) {
    const auto&  source = segments[i];
    const size_t start = source.point_begin + (i == 0 ? 0 : 1);
    const size_t count = source.point_count + (i == 0 ? 1 : 0);
    const auto   segment_points = points.subspan(start, count);
    const auto   segment_type = lazer_curve_type(segment_points, source.type);
    curve.begin_segment();
    if (!approximate_curve_segment(segment_points, segment_type, source.degree,
                                   curve, arena) ||
        curve.failed) {
      return false;
    }
  }
  return true;
}

inline Result<SliderPath> calculate_slider_path(const Beatmap&   map,
                                                const HitObject& object,
                                                Arena*           result_arena,
                                                Arena* scratch_arena) {
  const auto& slider = map.sliders[object.slider];
  const auto  control_points =
      map.slider_points.subspan(slider.point_begin, slider.point_count);
  const TempArena work{scratch_arena};
  auto*           points =
      arena_push_array<CurvePoint>(scratch_arena, control_points.size() + 1);
  if (!points)
    return Error{ErrorCode::AllocationFailure};
  points[0] = {};
  for (size_t i = 0; i < control_points.size(); ++i)
    points[i + 1] = {static_cast<f32>(control_points[i].x - object.x),
                     static_cast<f32>(control_points[i].y - object.y)};
  const size_t  count = control_points.size() + 1;
  CurveVertices curve{scratch_arena};
  // The first typed control point is itself a one-vertex segment in osu!.
  // Circular approximation can produce a slightly different first vertex.
  if (count > 1 && points[0] != points[1])
    curve.append(points[0]);
  const std::span<const CurvePoint> relative_points{points, count};
  if (map.format_version >= 128) {
    const auto segments =
        map.slider_segments.subspan(slider.segment_begin, slider.segment_count);
    if (!calculate_lazer_slider_curve(relative_points, segments,
                                      slider.curve_type, curve,
                                      scratch_arena)) {
      return Error{ErrorCode::AllocationFailure};
    }
  } else {
    if (!calculate_legacy_slider_curve(relative_points, slider.curve_type,
                                       curve, scratch_arena)) {
      return Error{ErrorCode::AllocationFailure};
    }
  }
  auto* output = arena_push_array<PathPoint>(result_arena, curve.count);
  auto* lengths = arena_push_array<f64>(result_arena, curve.count);
  if (!output || !lengths)
    return Error{ErrorCode::AllocationFailure};
  curve.copy_to(output);
  lengths[0] = 0;
  for (size_t i = 1; i < curve.count; ++i)
    lengths[i] = lengths[i - 1] + (output[i] - output[i - 1]).length();
  size_t    end = curve.count - 1;
  const f64 expected = slider.length;
  if (expected > 0 && end && expected != lengths[end] &&
      !(expected > lengths[end] && output[end] == output[end - 1])) {
    while (end > 1 && lengths[end - 1] >= expected)
      --end;
    const auto edge = output[end] - output[end - 1];
    output[end] =
        output[end - 1] +
        (edge / edge.length()) * static_cast<f32>(expected - lengths[end - 1]);
    lengths[end] = expected;
  }
  const SliderPath result{{output, end + 1}, {lengths, end + 1}};
  return result;
}

inline bool set_slider_paths(Beatmap& map,
                             Arena*   result_arena,
                             Arena*   scratch_arena) {
  if (map.sliders.empty())
    return true;
  auto* paths = arena_push_array<SliderPath>(result_arena, map.sliders.size());
  if (!paths)
    return false;
  bool success = true;
  for (const auto& object : map.hit_objects) {
    if (object.slider == HitObject::kNoSlider)
      continue;
    auto path = calculate_slider_path(map, object, result_arena, scratch_arena);
    if (!path) {
      success = false;
      break;
    }
    paths[object.slider] = path.value();
  }
  if (success)
    map.slider_paths = {paths, map.sliders.size()};
  return success;
}

#if defined(__GNUC__) && !defined(__clang__)
#pragma GCC pop_options
#endif
}  // namespace fosu::internal
