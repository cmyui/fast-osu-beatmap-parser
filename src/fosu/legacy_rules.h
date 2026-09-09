#pragma once

#include <fosu/beatmap.h>
#include <algorithm>

namespace fosu::internal {

// The parsing engines do not allocate. Only an out-of-order map needs this
// temporary merge buffer; equal timestamps retain their original input order.
inline bool sort_hit_objects(std::span<HitObject> objects, Arena* scratch_arena) {
  const size_t checkpoint = arena_pos(scratch_arena);
  auto* scratch = arena_push_array<HitObject>(scratch_arena, objects.size());
  if (!scratch)
    return false;
  auto* source = objects.data();
  auto* destination = scratch;
  for (size_t width = 1; width < objects.size(); width *= 2) {
    for (size_t begin = 0; begin < objects.size(); begin += width * 2) {
      const size_t middle = std::min(begin + width, objects.size());
      const size_t end = std::min(begin + width * 2, objects.size());
      std::merge(source + begin, source + middle, source + middle, source + end,
                 destination + begin, [](const HitObject& a, const HitObject& b) {
                   return a.time < b.time;
                 });
    }
    std::swap(source, destination);
  }
  if (source != objects.data())
    std::memcpy(objects.data(), source, objects.size_bytes());
  arena_pop_to(scratch_arena, checkpoint);
  return true;
}

inline bool apply_legacy_rules(Beatmap& map, Arena* scratch_arena) {
  const bool ordered = std::is_sorted(map.hit_objects.begin(), map.hit_objects.end(),
                                      [](const HitObject& a, const HitObject& b) {
                                        return a.time < b.time;
                                      });
  if (!ordered && !sort_hit_objects(map.hit_objects, scratch_arena))
    return false;

  // Breaks are sparse. Find the first later object without another full walk
  // over hitobjects. Keep the cursor to match osu! even for unordered breaks.
  auto next_object = map.hit_objects.begin();
  for (const auto& period : map.breaks) {
    next_object = std::upper_bound(next_object, map.hit_objects.end(), period.end,
                                   [](double time, const HitObject& object) {
                                     return time < object.time;
                                   });
    if (next_object == map.hit_objects.end())
      break;
    next_object->new_combo = true;
  }
  return true;
}

}  // namespace fosu::internal
