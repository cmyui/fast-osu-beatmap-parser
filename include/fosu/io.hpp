#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cstring>
#include <memory>
#include <string_view>

namespace fosu {

// The parser requires kBufferPadding readable zero bytes past the end of
// the input so vector loads, speculative SWAR reads, and strtod never run
// off the buffer. 128 bounds the worst-case speculative read of the
// one-pass timing point parser on garbage input (~90 bytes past a line
// start near EOF).
inline constexpr size_t kBufferPadding = 128;

struct FileBuffer {
    std::unique_ptr<char[]> data;
    size_t size = 0;
    size_t capacity = 0;  // allocated bytes, padding included

    explicit operator bool() const { return data != nullptr; }
    std::string_view view() const { return {data.get(), size}; }
};

// Reads `path` into `buf`, reusing its allocation when large enough
// (grow-only): in a parse-many loop the steady state performs no
// allocation and touches no new pages. Raw open/fstat/read beats the
// stdio equivalent by ~2.6us/file, and eagerly zero-filling the whole
// buffer (make_unique's value-init) measured ~1% of end-to-end; only the
// padding needs zeroing. mmap measured 13% slower end-to-end than read()
// on the real-map corpus: per-file munmap alone costs about as much as
// the entire read copy, plus soft page faults during the parse.
// Returns false when the file cannot be opened or sized.
inline bool read_into(const char* path, FileBuffer& buf) {
    buf.size = 0;
    const int fd = open(path, O_RDONLY);
    if (fd < 0) return false;
    struct stat st;
    if (fstat(fd, &st) != 0 || st.st_size < 0) {
        close(fd);
        return false;
    }
    const size_t len = static_cast<size_t>(st.st_size);
    if (buf.capacity < len + kBufferPadding) {
        buf.data.reset(new char[len + kBufferPadding]);  // default-init: no memset
        buf.capacity = len + kBufferPadding;
    }
    size_t got = 0;
    while (got < len) {
        const ssize_t r = read(fd, buf.data.get() + got, len - got);
        if (r <= 0) break;
        got += static_cast<size_t>(r);
    }
    close(fd);
    memset(buf.data.get() + got, 0, kBufferPadding);
    buf.size = got;
    return true;
}

inline FileBuffer read_file_padded(const char* path) {
    FileBuffer buf;
    read_into(path, buf);
    return buf;
}

// For tests/benchmarks: copy an in-memory string into a padded buffer.
inline FileBuffer make_padded(std::string_view content) {
    FileBuffer buf;
    buf.data.reset(new char[content.size() + kBufferPadding]);
    buf.capacity = content.size() + kBufferPadding;
    memcpy(buf.data.get(), content.data(), content.size());
    memset(buf.data.get() + content.size(), 0, kBufferPadding);
    buf.size = content.size();
    return buf;
}

}  // namespace fosu
