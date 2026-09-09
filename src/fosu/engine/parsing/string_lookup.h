#pragma once

#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace fosu::internal {

// FNV-1a: constant keys are hashed during compilation; input keys at lookup.
constexpr uint32_t string_hash(std::string_view key) {
  uint32_t hash = 2166136261u;
  for (unsigned char byte : key) {
    hash ^= byte;
    hash *= 16777619u;
  }
  return hash;
}

consteval uint32_t operator""_hash(const char* text, size_t size) {
  return string_hash({text, size});
}

template <typename Value>
struct StringEntry {
  std::string_view key;
  Value value;
  uint32_t hash;

  constexpr StringEntry(std::string_view key, Value value)
      : key(key), value(value), hash(string_hash(key)) {}
};

// Immutable open-addressed table. Zero slots mark misses; occupied slots store
// one-based entry indices. No allocations or runtime table construction.
template <typename Value, size_t N>
class StringLookup {
 public:
  consteval StringLookup(const StringEntry<Value> (&entries)[N])
      : entries_(std::to_array(entries)) {
    for (size_t i = 0; i < N; ++i) {
      if (entries[i].key.size() > max_key_size_)
        max_key_size_ = entries[i].key.size();
      size_t slot = entries[i].hash & kMask;
      while (slots_[slot])
        slot = (slot + 1) & kMask;
      slots_[slot] = i + 1;
    }
  }

  constexpr const Value* find(std::string_view key) const {
    if (key.size() > max_key_size_)
      return nullptr;
    const auto hash = string_hash(key);
    size_t slot = hash & kMask;
    while (slots_[slot]) {
      const auto& entry = entries_[slots_[slot] - 1];
      // Full equality is required even when the entire hash matches.
      if (entry.hash == hash && entry.key == key)
        return &entry.value;
      slot = (slot + 1) & kMask;
    }
    return nullptr;
  }

 private:
  static constexpr size_t kCapacity = std::bit_ceil(N * 2);
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
