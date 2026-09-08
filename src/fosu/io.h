#pragma once

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cerrno>
#include <cstdint>
#include <cstring>
#include <memory>
#include <new>
#include <string_view>

namespace fosu {

// The parser requires kBufferPadding readable zero bytes past the end of
// the input so vector loads and speculative SWAR reads never run
// off the buffer. 128 bounds the worst-case speculative read of the
// one-pass timing point parser on garbage input (~90 bytes past a line
// start near EOF).
inline constexpr size_t kBufferPadding = 128;
inline constexpr size_t kMaxInputSize = 64u * 1024u * 1024u;

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
// Returns false on an I/O error or an input larger than kMaxInputSize.
inline bool read_into(const char* path, FileBuffer& buf) {
  buf.size = 0;
  const int fd = open(path, O_RDONLY);
  if (fd < 0)
    return false;
  struct stat st;
  const int stat_result = fstat(fd, &st);
  if (stat_result != 0 || st.st_size < 0) {
    const int error = stat_result ? errno : EIO;
    close(fd);
    errno = error;
    return false;
  }
  if (static_cast<uint64_t>(st.st_size) > kMaxInputSize) {
    close(fd);
    errno = EFBIG;
    return false;
  }
  const size_t len = static_cast<size_t>(st.st_size);
  if (buf.capacity < len + kBufferPadding) {
    auto data = std::unique_ptr<char[]>(new (std::nothrow) char[len + kBufferPadding]);
    if (!data) {
      close(fd);
      errno = ENOMEM;
      return false;
    }
    buf.data = std::move(data);
    buf.capacity = len + kBufferPadding;
  }
  size_t got = 0;
  while (got < len) {
    const ssize_t r = read(fd, buf.data.get() + got, len - got);
    if (r < 0 && errno == EINTR)
      continue;
    if (r <= 0) {
      const int error = r < 0 ? errno : EIO;
      close(fd);
      errno = error;
      return false;
    }
    got += static_cast<size_t>(r);
  }
  close(fd);
  memset(buf.data.get() + got, 0, kBufferPadding);
  buf.size = got;
  return true;
}

inline FileBuffer read_file_padded(const char* path) {
  FileBuffer buf;
  if (!read_into(path, buf))
    return {};
  return buf;
}

// For tests/benchmarks: copy an in-memory string into a padded buffer.
inline FileBuffer make_padded(std::string_view content) {
  if (content.size() > kMaxInputSize)
    return {};
  FileBuffer buf;
  buf.data.reset(new (std::nothrow) char[content.size() + kBufferPadding]);
  if (!buf.data)
    return {};
  buf.capacity = content.size() + kBufferPadding;
  if (!content.empty())
    memcpy(buf.data.get(), content.data(), content.size());
  memset(buf.data.get() + content.size(), 0, kBufferPadding);
  buf.size = content.size();
  return buf;
}

}  // namespace fosu
