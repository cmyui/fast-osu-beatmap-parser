#pragma once

#include <fosu/compiler.h>
#include <fosu/types.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <span>

namespace fosu {

// Keep the official implementation's separate single-precision operations.
FOSU_FP_CONTRACT_OFF_BEGIN

// Subpixel coordinates relative to the slider head, in osu! playfield pixels.
struct PathPoint {
  f32 x, y;
  bool operator==(const PathPoint&) const = default;
  PathPoint operator+(PathPoint b) const { return {x + b.x, y + b.y}; }
  PathPoint operator-(PathPoint b) const { return {x - b.x, y - b.y}; }
  PathPoint operator*(f32 scale) const { return {x * scale, y * scale}; }
  PathPoint operator/(f32 scale) const { return {x / scale, y / scale}; }
  f32 squared_length() const { return x * x + y * y; }
  f32 length() const { return std::sqrt(squared_length()); }
};

struct SliderPath {
  std::span<PathPoint> points;
  std::span<f64>       cumulative_lengths;

  f64 distance() const {
    return cumulative_lengths.empty() ? 0 : cumulative_lengths.back();
  }
};

// A pure query: no approximation, allocation, or cached mutation. Progress is
// clamped to [0, 1]; add the hit object's x/y to obtain playfield coordinates.
inline PathPoint slider_position_at(const SliderPath& path, f64 progress) {
  if (path.points.empty())
    return {};
  const auto& lengths = path.cumulative_lengths;
  const f64   distance = std::clamp(progress, 0.0, 1.0) * path.distance();
  // The first length not below distance. Heads, tails and repeats sit at the
  // path's ends, so search only between them.
  size_t      i = 0;
  if (progress >= 1 && !lengths.empty()) {
    i = lengths.size() - 1;
    while (i && lengths[i - 1] >= distance)
      --i;
  } else if (progress > 0) {
    i = static_cast<size_t>(
        std::lower_bound(lengths.begin(), lengths.end(), distance) -
        lengths.begin());
  }
  if (!i)
    return path.points.front();
  if (i >= path.points.size())
    return path.points.back();
  const f64 start = lengths[i - 1];
  const f64 length = lengths[i] - start;
  if (std::abs(length) < 1e-7)
    return path.points[i - 1];
  return path.points[i - 1] + (path.points[i] - path.points[i - 1]) *
                                  static_cast<f32>((distance - start) / length);
}

FOSU_FP_CONTRACT_OFF_END

}  // namespace fosu
