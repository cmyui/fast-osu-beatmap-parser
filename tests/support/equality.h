#pragma once
#include <tests/support/canonical_dump.h>

#include <string>

template <typename Map>
std::string canonical(const Map& bm) {
  std::string out;
  fosu_dump::dump(bm, out);
  return out;
}
