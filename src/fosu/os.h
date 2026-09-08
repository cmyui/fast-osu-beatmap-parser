#pragma once

#include <sys/mman.h>
#include <unistd.h>

#include <cstddef>
#include <string_view>

namespace fosu::internal {

inline size_t os_page_size() {
  return static_cast<size_t>(sysconf(_SC_PAGESIZE));
}

inline void* os_reserve(size_t size) {
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
}

inline bool os_commit(void* memory, size_t size) {
  return mprotect(memory, size, PROT_READ | PROT_WRITE) == 0;
}

inline bool os_lock(void* memory, size_t size) {
  return mlock(memory, size) == 0;
}

inline void os_release(void* memory, size_t size) {
  munmap(memory, size);
}

inline void os_log(std::string_view message) {
  (void)write(STDERR_FILENO, message.data(), message.size());
}

}  // namespace fosu::internal
