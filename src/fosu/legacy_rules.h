#pragma once

#include <fosu/arena.h>
#include <fosu/beatmap.h>
#include <fosu/types.h>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <span>

namespace fosu::internal {

// A stable sort through a temporary arena merge buffer: equal elements keep
// their input order.
template <typename T, typename Compare>
inline bool merge_sort(std::span<T> values,
                       Arena*       scratch_arena,
                       Compare      compare) {
  const size_t checkpoint = arena_pos(scratch_arena);
  auto*        scratch = arena_push_array<T>(scratch_arena, values.size());
  if (!scratch)
    return false;
  auto* source = values.data();
  auto* destination = scratch;
  for (size_t width = 1; width < values.size(); width *= 2) {
    for (size_t begin = 0; begin < values.size(); begin += width * 2) {
      const size_t middle = std::min(begin + width, values.size());
      const size_t end = std::min(begin + width * 2, values.size());
      std::merge(source + begin, source + middle, source + middle, source + end,
                 destination + begin, compare);
    }
    std::swap(source, destination);
  }
  if (source != values.data())
    std::memcpy(values.data(), source, values.size_bytes());
  arena_pop_to(scratch_arena, checkpoint);
  return true;
}

// The parsing engines do not allocate. Only an out-of-order map needs this
// temporary merge buffer; equal timestamps retain their original input order.
inline bool sort_hit_objects(std::span<HitObject> objects,
                             Arena*               scratch_arena) {
  return merge_sort(objects, scratch_arena,
                    [](const HitObject& a, const HitObject& b) {
                      return a.time < b.time;
                    });
}

inline bool apply_legacy_rules(Beatmap& map, Arena* scratch_arena) {
  const bool ordered =
      std::is_sorted(map.hit_objects.begin(), map.hit_objects.end(),
                     [](const HitObject& a, const HitObject& b) {
                       return a.time < b.time;
                     });
  if (!ordered && !sort_hit_objects(map.hit_objects, scratch_arena))
    return false;

  // Breaks are sparse. Find the first later object without another full walk
  // over hitobjects. Keep the cursor to match osu! even for unordered breaks.
  auto next_object = map.hit_objects.begin();
  for (const auto& period : map.breaks) {
    next_object =
        std::upper_bound(next_object, map.hit_objects.end(), period.end,
                         [](f64 time, const HitObject& object) {
                           return time < object.time;
                         });
    if (next_object == map.hit_objects.end())
      break;
    next_object->new_combo = true;
  }
  return true;
}

}  // namespace fosu::internal
