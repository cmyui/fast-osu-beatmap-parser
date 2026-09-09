#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fosu::internal {

// Seeded FNV-1a with a final mix so the low bucket bits use the entire hash.
constexpr uint32_t string_hash(std::string_view key, uint32_t seed = 0) {
  uint32_t hash = 2166136261u ^ seed;
  for (unsigned char byte : key) {
    hash ^= byte;
    hash *= 16777619u;
  }
  hash ^= hash >> 16;
  hash *= 0x85ebca6bu;
  return hash ^ (hash >> 13);
}

template <typename Value>
struct StringEntry {
  std::string_view key;
  Value value;
};

// Choose a collision-free seed for known keys at compile time. Unknown keys
// still require full equality. Zero slots mark misses; others hold entry + 1.
template <typename Value, size_t N>
class StringLookup {
 public:
  consteval StringLookup(const StringEntry<Value> (&entries)[N])
      : entries_(std::to_array(entries)) {
    for (size_t i = 0; i < N; ++i) {
      if (entries[i].key.size() > max_key_size_)
        max_key_size_ = entries[i].key.size();
    }
    for (seed_ = 0; seed_ < 4096; ++seed_) {
      slots_.fill(0);
      bool collision = false;
      for (size_t i = 0; i < N; ++i) {
        const auto slot = hash_key(entries[i].key) & kMask;
        if (slots_[slot]) {
          collision = true;
          break;
        }
        slots_[slot] = i + 1;
      }
      if (!collision)
        return;
    }
    throw "No perfect hash found within search budget";
  }

  constexpr const Value* find(std::string_view key) const {
    const auto index = find_index(key);
    return index < N ? &entries_[index].value : nullptr;
  }
  constexpr size_t find_index(std::string_view key) const {
    if (key.size() > max_key_size_)
      return N;
    const auto index = slots_[hash_key(key) & kMask];
    return index && entries_[index - 1].key == key ? index - 1 : N;
  }
  static constexpr size_t size = N;
  constexpr size_t slot_for(std::string_view key) const {
    return key.size() > max_key_size_ ? kCapacity + N : hash_key(key) & kMask;
  }
  constexpr size_t slot_at(size_t index) const {
    // Unused switch cases lie outside every possible input slot.
    return index < N ? hash_key(entries_[index].key) & kMask : kCapacity + index;
  }
  constexpr auto key_at(size_t index) const { return entries_[index].key; }
  constexpr Value value_at(size_t index) const { return entries_[index].value; }

 private:
  constexpr uint32_t hash_key(std::string_view key) const {
    return string_hash(key, seed_);
  }
  uint32_t seed_ = 0;
  static constexpr size_t kCapacity = std::bit_ceil(N * 4);
  static constexpr size_t kMask = kCapacity - 1;
  std::array<StringEntry<Value>, N> entries_;
  std::array<size_t, kCapacity> slots_{};
  size_t max_key_size_ = 0;
};

template <typename Value, size_t N>
consteval auto make_string_lookup(const StringEntry<Value> (&entries)[N]) {
  return StringLookup<Value, N>(entries);
}

}  // namespace fosu::internal
