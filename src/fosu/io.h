#pragma once

#include <fosu/types.h>

#if defined(_WIN32)
#include <algorithm>
#endif
#include <cerrno>
#include <cstddef>
#include <cstdint>  // IWYU pragma: keep (Windows-only UINT32_MAX)
#if defined(_WIN32)
#include <memory>
#include <new>
#endif

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
  switch (error) {
    case ERROR_FILE_NOT_FOUND:
    case ERROR_PATH_NOT_FOUND:
      errno = ENOENT;
      break;
    case ERROR_ACCESS_DENIED:
    case ERROR_SHARING_VIOLATION:
      errno = EACCES;
      break;
    case ERROR_INVALID_NAME:
    case ERROR_INVALID_PARAMETER:
      errno = EINVAL;
      break;
    case ERROR_NOT_ENOUGH_MEMORY:
    case ERROR_OUTOFMEMORY:
      errno = ENOMEM;
      break;
    default:
      errno = EIO;
      break;
  }
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
  if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, path, -1, wide.get(),
                           length)) {
    set_file_error(GetLastError());
    return kInvalidInputFile;
  }
  InputFile file =
      CreateFileW(wide.get(), GENERIC_READ,
                  FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                  nullptr, OPEN_EXISTING,
                  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_SEQUENTIAL_SCAN |
                      FILE_FLAG_BACKUP_SEMANTICS,
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

inline bool input_file_size(InputFile file, u64& size) {
  LARGE_INTEGER value;
  if (!GetFileSizeEx(file, &value)) {
    set_file_error(GetLastError());
    return false;
  }
  if (value.QuadPart < 0) {
    errno = EIO;
    return false;
  }
  size = static_cast<u64>(value.QuadPart);
  return true;
}

inline ptrdiff_t read_input_file(InputFile file, char* data, size_t size) {
  const DWORD amount = static_cast<DWORD>(std::min<size_t>(size, UINT32_MAX));
  DWORD       read = 0;
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

inline bool input_file_size(InputFile file, u64& size) {
  struct stat info;
  if (fstat(file, &info) != 0)
    return false;
  if (info.st_size < 0) {
    errno = EIO;
    return false;
  }
  size = static_cast<u64>(info.st_size);
  return true;
}

inline ptrdiff_t read_input_file(InputFile file, char* data, size_t size) {
  return read(file, data, size);
}
#endif

}  // namespace internal

}  // namespace fosu
