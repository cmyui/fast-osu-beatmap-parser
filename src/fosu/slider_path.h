#pragma once

#include <algorithm>
#include <cmath>
#include <span>

namespace fosu {

// Subpixel coordinates relative to the slider head, in osu! playfield pixels.
struct PathPoint {
  float x, y;
  bool operator==(const PathPoint&) const = default;
  PathPoint operator+(PathPoint b) const { return {x + b.x, y + b.y}; }
  PathPoint operator-(PathPoint b) const { return {x - b.x, y - b.y}; }
  PathPoint operator*(float scale) const { return {x * scale, y * scale}; }
  PathPoint operator/(float scale) const { return {x / scale, y / scale}; }
  float squared_length() const { return x * x + y * y; }
  float length() const { return std::sqrt(squared_length()); }
};

struct SliderPath {
  std::span<PathPoint> points;
  std::span<double> cumulative_lengths;

  double distance() const {
    return cumulative_lengths.empty() ? 0 : cumulative_lengths.back();
  }
};

// A pure query: no approximation, allocation, or cached mutation. Progress is
// clamped to [0, 1]; add the hit object's x/y to obtain playfield coordinates.
inline PathPoint slider_position_at(const SliderPath& path, double progress) {
  if (path.points.empty())
    return {};
  const double distance = std::clamp(progress, 0.0, 1.0) * path.distance();
  const auto found = std::lower_bound(path.cumulative_lengths.begin(),
                                      path.cumulative_lengths.end(), distance);
  const auto i = static_cast<size_t>(found - path.cumulative_lengths.begin());
  if (!i)
    return path.points.front();
  if (i >= path.points.size())
    return path.points.back();
  const double start = path.cumulative_lengths[i - 1];
  const double length = path.cumulative_lengths[i] - start;
  if (std::abs(length) < 1e-7)
    return path.points[i - 1];
  return path.points[i - 1] + (path.points[i] - path.points[i - 1]) *
                                  static_cast<float>((distance - start) / length);
}

}  // namespace fosu
