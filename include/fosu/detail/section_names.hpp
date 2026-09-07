#pragma once
#include <cstdint>
#include <string_view>
namespace fosu::detail {
enum class Section : uint8_t { None, General, Editor, Metadata, Difficulty, Events, TimingPoints, Colours, HitObjects, Unknown };
inline Section match_section(std::string_view line) {
    if (line.size() < 3) return Section::Unknown;
    switch (line[1]) {
        case 'G': if (line == "[General]") return Section::General; break;
        case 'E':
            if (line == "[Editor]") return Section::Editor;
            if (line == "[Events]") return Section::Events;
            break;
        case 'M': if (line == "[Metadata]") return Section::Metadata; break;
        case 'D': if (line == "[Difficulty]") return Section::Difficulty; break;
        case 'T': if (line == "[TimingPoints]") return Section::TimingPoints; break;
        case 'C': if (line == "[Colours]") return Section::Colours; break;
        case 'H': if (line == "[HitObjects]") return Section::HitObjects; break;
    }
    return Section::Unknown;
}
}  // namespace fosu::detail
