#pragma once
#include "scalar_parse.hpp"

namespace fosu::detail {

inline std::string_view trim(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) --end;
    return {p, static_cast<size_t>(end - p)};
}

inline bool split_kv(const char* p, size_t len, std::string_view& key,
                     std::string_view& val) {
    const auto* colon = static_cast<const char*>(memchr(p, ':', len));
    if (!colon) return false;
    key = trim(p, colon);
    val = trim(colon + 1, p + len);
    return !key.empty();
}

}  // namespace fosu::detail
