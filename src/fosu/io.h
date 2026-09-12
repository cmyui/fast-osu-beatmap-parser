#pragma once

#include <algorithm>
#include <cerrno>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <memory>
#include <new>
#include <string_view>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace fosu {

namespace internal {

#if defined(_WIN32)
using InputFile = HANDLE;
inline const InputFile kInvalidInputFile = INVALID_HANDLE_VALUE;

inline void set_file_error(DWORD error) {
  _dosmaperr(error);
}

inline InputFile open_input_file(const char* path) {
  const int length =
      MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, nullptr, 0);
  if (!length) {
    set_file_error(GetLastError());
    return kInvalidInputFile;
  }
  auto wide = std::unique_ptr<wchar_t[]>(new (std::nothrow) wchar_t[length]);
  if (!wide) {
    errno = ENOMEM;
    return kInvalidInputFile;
  }
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide.get(), length)) {
    set_file_error(GetLastError());
    return kInvalidInputFile;
  }
  InputFile file = CreateFileW(
      wide.get(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
      nullptr, OPEN_EXISTING,
      FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN | FILE_FLAG_BACKUP_SEMANTICS,
      nullptr);
  if (file == kInvalidInputFile) {
    set_file_error(GetLastError());
    return file;
  }
  BY_HANDLE_FILE_INFORMATION info;
  if (!GetFileInformationByHandle(file, &info)) {
    const DWORD error = GetLastError();
    CloseHandle(file);
    set_file_error(error);
    return kInvalidInputFile;
  }
  if (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
    CloseHandle(file);
    errno = EISDIR;
    return kInvalidInputFile;
  }
  return file;
}

inline void close_input_file(InputFile file) {
  CloseHandle(file);
}

inline bool input_file_size(InputFile file, uint64_t& size) {
  LARGE_INTEGER value;
  if (!GetFileSizeEx(file, &value)) {
    set_file_error(GetLastError());
    return false;
  }
  if (value.QuadPart < 0) {
    errno = EIO;
    return false;
  }
  size = static_cast<uint64_t>(value.QuadPart);
  return true;
}

inline ptrdiff_t read_input_file(InputFile file, char* data, size_t size) {
  const DWORD amount = static_cast<DWORD>(std::min<size_t>(size, UINT32_MAX));
  DWORD read = 0;
  if (!ReadFile(file, data, amount, &read, nullptr)) {
    set_file_error(GetLastError());
    return -1;
  }
  return static_cast<ptrdiff_t>(read);
}
#else
using InputFile = int;
inline constexpr InputFile kInvalidInputFile = -1;

inline InputFile open_input_file(const char* path) {
  return open(path, O_RDONLY);
}

inline void close_input_file(InputFile file) {
  close(file);
}

inline bool input_file_size(InputFile file, uint64_t& size) {
  struct stat info;
  if (fstat(file, &info) != 0)
    return false;
  if (info.st_size < 0) {
    errno = EIO;
    return false;
  }
  size = static_cast<uint64_t>(info.st_size);
  return true;
}

inline ptrdiff_t read_input_file(InputFile file, char* data, size_t size) {
  return read(file, data, size);
}
#endif

}  // namespace internal

// The parser requires kBufferPadding readable zero bytes past the end of
// the input so vector loads and speculative SWAR reads never run
// off the buffer. 128 bounds the worst-case speculative read of the
// one-pass timing point parser on garbage input (~90 bytes past a line
// start near EOF).
inline constexpr size_t kBufferPadding = 128;

inline constexpr bool can_pad_input(size_t size) {
  return size <= std::numeric_limits<size_t>::max() - kBufferPadding;
}

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
// Returns false on an I/O error or when the input cannot fit in the address
// space together with the parser's readable padding.
inline bool read_into(const char* path, FileBuffer& buf) {
  buf.size = 0;
  const internal::InputFile file = internal::open_input_file(path);
  if (file == internal::kInvalidInputFile)
    return false;
  uint64_t file_size;
  if (!internal::input_file_size(file, file_size)) {
    const int error = errno;
    internal::close_input_file(file);
    errno = error;
    return false;
  }
  if (file_size > std::numeric_limits<size_t>::max() - kBufferPadding) {
    internal::close_input_file(file);
    errno = EFBIG;
    return false;
  }
  const size_t len = static_cast<size_t>(file_size);
  if (buf.capacity < len + kBufferPadding) {
    auto data = std::unique_ptr<char[]>(new (std::nothrow) char[len + kBufferPadding]);
    if (!data) {
      internal::close_input_file(file);
      errno = ENOMEM;
      return false;
    }
    buf.data = std::move(data);
    buf.capacity = len + kBufferPadding;
  }
  size_t got = 0;
  while (got < len) {
    const ptrdiff_t r = internal::read_input_file(file, buf.data.get() + got, len - got);
    if (r < 0 && errno == EINTR)
      continue;
    if (r <= 0) {
      const int error = r < 0 ? errno : EIO;
      internal::close_input_file(file);
      errno = error;
      return false;
    }
    got += static_cast<size_t>(r);
  }
  internal::close_input_file(file);
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
  if (!can_pad_input(content.size()))
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
