#pragma once

#include <cstddef>

#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

namespace fosu::internal {

inline size_t os_page_size() {
#if defined(_WIN32)
  SYSTEM_INFO info;
  GetSystemInfo(&info);
  return info.dwPageSize;
#else
  return static_cast<size_t>(sysconf(_SC_PAGESIZE));
#endif
}

inline void* os_reserve(size_t size) {
#if defined(_WIN32)
  return VirtualAlloc(nullptr, size, MEM_RESERVE, PAGE_NOACCESS);
#else
  int flags = MAP_PRIVATE | MAP_ANONYMOUS;
#if defined(MAP_NORESERVE)
  flags |= MAP_NORESERVE;
#endif
  void* memory = mmap(nullptr, size, PROT_NONE, flags, -1, 0);
  if (memory == MAP_FAILED)
    return nullptr;
#if defined(MADV_HUGEPAGE) && !defined(FOSU_ARENA_NO_HUGEPAGE)
  madvise(memory, size, MADV_HUGEPAGE);
#endif
  return memory;
#endif
}

inline bool os_commit(void* memory, size_t size) {
#if defined(_WIN32)
  return VirtualAlloc(memory, size, MEM_COMMIT, PAGE_READWRITE) == memory;
#else
  return mprotect(memory, size, PROT_READ | PROT_WRITE) == 0;
#endif
}

inline void os_release(void* memory, size_t size) {
#if defined(_WIN32)
  (void)size;
  VirtualFree(memory, 0, MEM_RELEASE);
#else
  munmap(memory, size);
#endif
}

}  // namespace fosu::internal
