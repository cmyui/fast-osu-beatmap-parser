#pragma once

#include <cstdio>
#include <cstring>
#include <memory>
#include <string_view>

namespace fosu {

// The parser requires kBufferPadding readable zero bytes past the end of
// the input so vector loads and strtod never run off the buffer.
inline constexpr size_t kBufferPadding = 64;

struct FileBuffer {
    std::unique_ptr<char[]> data;
    size_t size = 0;

    explicit operator bool() const { return data != nullptr; }
    std::string_view view() const { return {data.get(), size}; }
};

inline FileBuffer read_file_padded(const char* path) {
    FileBuffer buf;
    FILE* f = fopen(path, "rb");
    if (!f) return buf;
    fseek(f, 0, SEEK_END);
    const long len = ftell(f);
    if (len < 0) {
        fclose(f);
        return buf;
    }
    fseek(f, 0, SEEK_SET);
    auto data = std::make_unique<char[]>(static_cast<size_t>(len) + kBufferPadding);
    const size_t got = fread(data.get(), 1, static_cast<size_t>(len), f);
    fclose(f);
    memset(data.get() + got, 0, kBufferPadding);
    buf.data = std::move(data);
    buf.size = got;
    return buf;
}

// For tests/benchmarks: copy an in-memory string into a padded buffer.
inline FileBuffer make_padded(std::string_view content) {
    FileBuffer buf;
    buf.data = std::make_unique<char[]>(content.size() + kBufferPadding);
    memcpy(buf.data.get(), content.data(), content.size());
    memset(buf.data.get() + content.size(), 0, kBufferPadding);
    buf.size = content.size();
    return buf;
}

}  // namespace fosu
