#include <fosu/engine/parsing/key_value.h>
#include <fosu/engine/parsing/string_lookup.h>
#include <cassert>
#include <string>

using fosu::internal::make_string_lookup;
using fosu::internal::string_hash;

// These distinct strings collide across the full 32-bit FNV-1a hash, not
// merely the table's bucket mask. Neither may impersonate the other.
static_assert(string_hash("costarring") == string_hash("liquid"));
constexpr auto collisions = make_string_lookup<int>({
    {"costarring", 1},
    {"liquid", 2},
    {"", 3},
});
static_assert(*collisions.find("costarring") == 1);
static_assert(*collisions.find("liquid") == 2);
static_assert(*collisions.find("") == 3);
static_assert(collisions.find("Costarring") == nullptr);

constexpr auto fields = make_string_lookup<fosu::internal::FieldParser>({
    {"Title", fosu::internal::assign_field_text<&fosu::BeatmapHeader::title>},
    {"Artist", fosu::internal::assign_field_text<&fosu::BeatmapHeader::artist>},
});

int main() {
  fosu::Beatmap map;
  fosu::internal::parse_key_value<fields>(map, {"Title", "accepted"});
  assert(map.title == "accepted");
  bool tested_collision = false;
  for (int byte = 0; byte < 256; ++byte) {
    std::string unknown = "bad";
    unknown += static_cast<char>(byte);
    if (fields.slot_for(unknown) != fields.slot_at(0))
      continue;
    assert(fields.find(unknown) == nullptr);
    fosu::internal::parse_key_value<fields>(map, {unknown, "must not overwrite"});
    assert(map.title == "accepted");
    assert(map.stats.malformed_lines == 0);
    tested_collision = true;
    break;
  }
  assert(tested_collision);
  constexpr auto single = make_string_lookup<int>({{"costarring", 1}});
  assert(single.find("liquid") == nullptr);
  for (const auto key : {"cost", "costarring-extra", "unknown"})
    assert(collisions.find(key) == nullptr);
  assert(*collisions.find("costarring") == 1);
  assert(*collisions.find("liquid") == 2);

  constexpr auto handlers = make_string_lookup<int (*)()>({
      {"one",
       +[] {
         return 1;
       }},
      {"two",
       +[] {
         return 2;
       }},
  });
  assert((*handlers.find("two"))() == 2);
}
