#include <fosu/engine/parsing/string_lookup.h>
#include <fosu/engine/sections/metadata.h>
#include <cassert>

using fosu::internal::make_string_lookup;
using fosu::internal::string_hash;
using fosu::internal::operator""_hash;
static_assert("Title"_hash == string_hash("Title"));
constexpr std::string_view title_collision = "\x01\x20\xeb\xc2\x7e";
static_assert(string_hash(title_collision) == "Title"_hash);

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

int main() {
  fosu::Beatmap map;
  assert(fosu::internal::parse_metadata_field(map, {"Title", "accepted"}));
  assert(map.title == "accepted");
  assert(fosu::internal::parse_metadata_field(map, {title_collision, "ignored"}));
  assert(map.title == "accepted");
  assert(fosu::internal::parse_metadata_field(map, {"Unknown", "ignored"}));
  assert(map.title == "accepted");
  assert(!fosu::internal::parse_metadata_field(map, {"BeatmapID", "invalid"}));
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
