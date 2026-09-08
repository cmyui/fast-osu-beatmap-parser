#pragma once
#include "byte_scan.hpp"
#include "scalar_parse.hpp"

namespace fosu::internal {

inline std::string_view trim(const char* p, const char* end) {
    while (p < end && (*p == ' ' || *p == '\t')) ++p;
    while (end > p && (end[-1] == ' ' || end[-1] == '\t')) --end;
    return {p, static_cast<size_t>(end - p)};
}

inline bool split_kv(const char* p, size_t len, std::string_view& key,
                     std::string_view& val) {
  const auto* colon = find_byte<':'>(p, p + len);
  if (colon == p + len)
    return false;
  key = trim(p, colon);
  val = trim(colon + 1, p + len);
  return !key.empty();
}

}  // namespace fosu::internal
