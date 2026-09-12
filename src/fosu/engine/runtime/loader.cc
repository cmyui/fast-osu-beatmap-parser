#include <fosu/engine/parse_document.h>
#include <fosu/engine/parsing/string_lookup.h>
#include <fosu/engine/runtime/cpu_features.h>
#include <fosu/engine/runtime/loader.h>

#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <cwchar>
#else
#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#endif

namespace fosu::internal {
static_assert(!FOSU_SIMD, "the core embeds only the scalar engine");
namespace {
constexpr const ParsingEngine& embedded_scalar_engine = compiled_engine;
static_assert(embedded_scalar_engine.kind == EngineKind::Scalar);

// Trivial lifetime permits a host's late exit callback to initialize again
// after library cleanup. No parser or arena lives in an engine library.
constinit std::atomic<const ParsingEngine*> selected{nullptr};
constinit std::atomic_flag selecting = ATOMIC_FLAG_INIT;
#if defined(_WIN32)
using EngineLibrary = HMODULE;
#else
using EngineLibrary = void*;
#endif
constinit EngineLibrary library = nullptr;
constexpr ParsingEngine unavailable{};
constexpr auto kEngineKinds = make_string_lookup<EngineKind>({
    {"scalar", EngineKind::Scalar},
    {"avx2", EngineKind::Avx2},
    {"neon", EngineKind::Neon},
});

#if defined(_WIN32)
inline constexpr size_t kPathCapacity = 32768;
int module_anchor;

bool engine_path(const char* name, wchar_t (&path)[kPathCapacity]) {
  HMODULE module;
  if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS |
                              GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                          reinterpret_cast<const wchar_t*>(&module_anchor), &module)) {
    return false;
  }
  const DWORD length = GetModuleFileNameW(module, path, kPathCapacity);
  if (!length || length >= kPathCapacity)
    return false;
  wchar_t* slash = std::wcsrchr(path, L'\\');
  if (!slash)
    return false;
  const wchar_t* filename =
      std::strcmp(name, "avx2") == 0 ? L"fosu_engine_avx2.dll" : L"fosu_engine_neon.dll";
  const size_t directory_length = static_cast<size_t>(slash - path) + 1;
  const size_t filename_length = std::wcslen(filename) + 1;
  if (directory_length + filename_length > kPathCapacity)
    return false;
  std::wmemcpy(path + directory_length, filename, filename_length);
  return true;
}
#else
bool engine_path(const char* name, char (&path)[PATH_MAX]) {
  Dl_info info{};
  if (!dladdr(reinterpret_cast<const void*>(&engine_path), &info))
    return false;
  const char* slash = std::strrchr(info.dli_fname, '/');
  if (!slash)
    return false;
  const int length =
      std::snprintf(path, sizeof(path), "%.*s/libfosu_engine_%s" FOSU_ENGINE_SUFFIX,
                    static_cast<int>(slash - info.dli_fname), info.dli_fname, name);
  return length > 0 && static_cast<size_t>(length) < sizeof(path);
}
#endif

const ParsingEngine* load_engine_library(const char* name) {
#if defined(_WIN32)
  wchar_t path[kPathCapacity];
  if (!engine_path(name, path))
    return nullptr;
  HMODULE loaded = LoadLibraryW(path);
  if (!loaded)
    return nullptr;
  const auto entry = reinterpret_cast<const ParsingEngine* (*)()>(
      GetProcAddress(loaded, "fosu_engine_v2"));
  if (!entry) {
    FreeLibrary(loaded);
    return nullptr;
  }
#else
  char path[PATH_MAX];
  if (!engine_path(name, path))
    return nullptr;
  void* loaded = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!loaded)
    return nullptr;
  const auto entry =
      reinterpret_cast<const ParsingEngine* (*)()>(dlsym(loaded, "fosu_engine_v2"));
  if (!entry) {
    dlclose(loaded);
    return nullptr;
  }
#endif
  library = loaded;
  return entry();
}

const ParsingEngine* select_engine_from_environment() {
#if defined(_WIN32)
  char request_buffer[16];
  const DWORD request_length =
      GetEnvironmentVariableA("FOSU_BACKEND", request_buffer, sizeof(request_buffer));
  if (request_length >= sizeof(request_buffer))
    return nullptr;
  const char* request = request_length ? request_buffer : FOSU_DEFAULT_BACKEND;
#else
  const char* request = std::getenv("FOSU_BACKEND");
  if (!request)
    request = FOSU_DEFAULT_BACKEND;
#endif
  if (std::strcmp(request, "auto") == 0) {
    for (EngineKind kind : {EngineKind::Avx2, EngineKind::Neon}) {
      if (engine_available(kind))
        if (const auto* engine = load_engine_library(engine_name(kind)))
          return engine;
    }
    return &embedded_scalar_engine;
  }
  const auto* kind = find_engine_kind(request);
  if (!kind)
    return nullptr;
  if (*kind == EngineKind::Scalar)
    return &embedded_scalar_engine;
  return engine_available(*kind) ? load_engine_library(request) : nullptr;
}
}  // namespace

const EngineKind* find_engine_kind(std::string_view name) {
  return kEngineKinds.find(name);
}

const char* engine_name(EngineKind kind) {
  switch (kind) {
    case EngineKind::Scalar:
      return "scalar";
    case EngineKind::Avx2:
      return "avx2";
    case EngineKind::Neon:
      return "neon";
  }
  return nullptr;
}

bool engine_available(EngineKind kind) {
  if (kind == EngineKind::Scalar)
    return true;
  bool supported = false;
#ifdef FOSU_HAS_AVX2
  if (kind == EngineKind::Avx2) {
    static const bool avx2 = fosu_dispatch::host_supports_avx2();
    supported = avx2;
  }
#endif
#ifdef FOSU_HAS_NEON
  if (kind == EngineKind::Neon) {
    static const bool neon = fosu_dispatch::host_supports_neon();
    supported = neon;
  }
#endif
#if defined(_WIN32)
  wchar_t path[kPathCapacity];
  return supported && engine_path(engine_name(kind), path) &&
         GetFileAttributesW(path) != INVALID_FILE_ATTRIBUTES;
#else
  char path[PATH_MAX];
  return supported && engine_path(engine_name(kind), path) && access(path, R_OK) == 0;
#endif
}

const ParsingEngine* selected_engine() {
  auto* engine = selected.load(std::memory_order_acquire);
  if (!engine) {
    while (selecting.test_and_set(std::memory_order_acquire)) {
    }
    engine = selected.load(std::memory_order_relaxed);
    if (!engine) {
      engine = select_engine_from_environment();
      if (!engine)
        engine = &unavailable;
      selected.store(engine, std::memory_order_release);
    }
    selecting.clear(std::memory_order_release);
  }
  return engine == &unavailable ? nullptr : engine;
}

void unload_engine() {
  selected.store(nullptr, std::memory_order_relaxed);
  if (library) {
#if defined(_WIN32)
    FreeLibrary(library);
#else
    dlclose(library);
#endif
  }
  library = nullptr;
}
}  // namespace fosu::internal
