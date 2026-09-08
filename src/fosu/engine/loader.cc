#include <fosu/engine/cpu_features.h>
#include <fosu/engine/loader.h>
#include <fosu/engine/parse_document.h>
#include <fosu/engine/string_lookup.h>

#include <dlfcn.h>
#include <limits.h>
#include <unistd.h>
#include <atomic>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace fosu::internal {
namespace {
// Trivial lifetime permits a host's late exit callback to initialize again
// after library cleanup. No parser or arena lives in an engine library.
constinit std::atomic<const ParsingEngine*> selected{nullptr};
constinit std::atomic_flag selecting = ATOMIC_FLAG_INIT;
constinit void* library = nullptr;
constexpr ParsingEngine unavailable{};
constexpr auto kEngineKinds = make_string_lookup<EngineKind>({
    {"scalar", EngineKind::Scalar},
    {"avx2", EngineKind::Avx2},
    {"neon", EngineKind::Neon},
});

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

const ParsingEngine* load(const char* name) {
  char path[PATH_MAX];
  if (!engine_path(name, path))
    return nullptr;
  void* loaded = dlopen(path, RTLD_NOW | RTLD_LOCAL);
  if (!loaded)
    return nullptr;
  const auto entry =
      reinterpret_cast<const ParsingEngine* (*)()>(dlsym(loaded, "fosu_engine_v1"));
  if (!entry) {
    dlclose(loaded);
    return nullptr;
  }
  library = loaded;
  return entry();
}

const ParsingEngine* choose() {
  const char* request = std::getenv("FOSU_BACKEND");
  if (!request) {
    const char* scalar = std::getenv("FOSU_FORCE_SCALAR");
    request = scalar && std::strcmp(scalar, "1") == 0 ? "scalar" : FOSU_DEFAULT_BACKEND;
  }
  if (std::strcmp(request, "auto") == 0) {
    for (EngineKind kind : {EngineKind::Avx2, EngineKind::Neon}) {
      if (engine_available(kind))
        if (const auto* engine = load(engine_name(kind)))
          return engine;
    }
    return &scalar_engine;
  }
  const auto* kind = find_engine_kind(request);
  if (!kind)
    return nullptr;
  if (*kind == EngineKind::Scalar)
    return &scalar_engine;
  return engine_available(*kind) ? load(request) : nullptr;
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
  char path[PATH_MAX];
  return supported && engine_path(engine_name(kind), path) && access(path, R_OK) == 0;
}

const ParsingEngine* selected_engine() {
  auto* engine = selected.load(std::memory_order_acquire);
  if (!engine) {
    while (selecting.test_and_set(std::memory_order_acquire)) {
    }
    engine = selected.load(std::memory_order_relaxed);
    if (!engine) {
      engine = choose();
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
  if (library)
    dlclose(library);
  library = nullptr;
}
}  // namespace fosu::internal
