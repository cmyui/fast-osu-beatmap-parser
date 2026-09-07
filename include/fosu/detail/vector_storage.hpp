#pragma once
#include <new>
#include <type_traits>
#include <cstring>
#include "../beatmap.hpp"

namespace fosu::detail {

// Publishes records written directly into a vector's reserved capacity.
// libstdc++ and libc++ lay a vector out as {begin, end, capacity_end};
// Debug containers and ASan container annotations require public vector
// operations. Define FOSU_PORTABLE_VECTORS to force that path for testing or
// ablation; ordinary release builds keep the direct path on known layouts.
#if defined(__has_feature)
#if __has_feature(address_sanitizer)
#define FOSU_DETAIL_ADDRESS_SANITIZER 1
#endif
#endif
#if defined(FOSU_PORTABLE_VECTORS) || defined(__SANITIZE_ADDRESS__) || \
    defined(FOSU_DETAIL_ADDRESS_SANITIZER) || defined(_GLIBCXX_DEBUG) || \
    (defined(_GLIBCXX_SANITIZE_VECTOR) && _GLIBCXX_SANITIZE_VECTOR) || \
    defined(_LIBCPP_DEBUG) || defined(_LIBCPP_ENABLE_DEBUG_MODE) || \
    (defined(_LIBCPP_DEBUG_LEVEL) && _LIBCPP_DEBUG_LEVEL > 0) || \
    (defined(_LIBCPP_HARDENING_MODE_DEBUG) && _LIBCPP_HARDENING_MODE == _LIBCPP_HARDENING_MODE_DEBUG)
inline constexpr bool kDirectVectorWrites = false;
#elif defined(__GLIBCXX__) || defined(_LIBCPP_VERSION)
inline constexpr bool kDirectVectorWrites = sizeof(std::vector<char>) == 3 * sizeof(char*);
#else
inline constexpr bool kDirectVectorWrites = false;
#endif
#undef FOSU_DETAIL_ADDRESS_SANITIZER
template <typename T>
inline void set_vector_size(std::vector<T>& v, size_t n) {
    T* raw[3];
    static_assert(sizeof(v) == sizeof(raw));
    void* const object = &v;
    memcpy(raw, object, sizeof raw);
    raw[1] = raw[0] + n;
    memcpy(object, raw, sizeof raw);
}
// Containers with a native set_size (the C ABI arena vectors) always take
// the direct path; std::vector takes it on the known layouts.
template <typename V>
inline constexpr bool has_set_size_v = requires(V& v) { v.set_size(size_t{}); };
template <typename V>
inline constexpr bool direct_vector_writes_v = has_set_size_v<V> || kDirectVectorWrites;
template <typename V>
inline void publish_size(V& v, size_t n) {
    if constexpr (has_set_size_v<V>) v.set_size(n);
    else set_vector_size(v, n);
}

// Start lifetimes in reserved storage without value-initializing scalar
// fields. Rejected records and speculative point slots may be overwritten
// before publication, so these records must need no destruction.
template <typename T>
inline T* construct_record(T* slot) {
    static_assert(std::is_trivially_destructible_v<T>);
    if constexpr (requires { typename T::uninit_t; })
        return ::new (static_cast<void*>(slot)) T(typename T::uninit_t{});
    else
        return ::new (static_cast<void*>(slot)) T;
}

}  // namespace fosu::detail
