#pragma once
#include "canonical_dump.hpp"

template <typename Map>
std::string canonical(const Map& bm) {
    std::string out;
    fosu_dump::dump(bm, out);
    return out;
}
