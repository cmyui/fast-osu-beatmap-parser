#include <dlfcn.h>
#include <fosu/io.h>
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

struct Module {
  const char* name;
  void* dso;
  void* ctx;
  void (*parse)(void*, const char*, size_t, int);
  void (*free)(void*);
};
uint64_t now() {
  return std::chrono::duration_cast<std::chrono::nanoseconds>(
             std::chrono::steady_clock::now().time_since_epoch())
      .count();
}
int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "usage: library_compare corpus reps module1 [module2...]\n");
    return 2;
  }
  const int reps = atoi(argv[2]);
  if (reps < 1)
    return 2;
  std::vector<Module> modules;
  for (int i = 3; i < argc; ++i) {
    void* dso = dlopen(argv[i], RTLD_NOW | RTLD_LOCAL);
    if (!dso) {
      fprintf(stderr, "%s\n", dlerror());
      return 1;
    }
    auto create = reinterpret_cast<void* (*)()>(dlsym(dso, "fosu_bench_new"));
    auto parse = reinterpret_cast<void (*)(void*, const char*, size_t, int)>(
        dlsym(dso, "fosu_bench_parse"));
    auto free = reinterpret_cast<void (*)(void*)>(dlsym(dso, "fosu_bench_free"));
    if (!create || !parse || !free)
      return 1;
    modules.push_back({argv[i], dso, create(), parse, free});
  }
  std::vector<std::string> files;
  for (const auto& f : std::filesystem::directory_iterator(argv[1]))
    if (f.path().extension() == ".osu")
      files.push_back(f.path());
  std::sort(files.begin(), files.end());
  const char* split = getenv("FOSU_BENCH_SPLIT");
  if (split && strcmp(split, "train") && strcmp(split, "eval"))
    return 2;
  if (split) {
    std::vector<std::string> selected;
    for (size_t i = 0; i < files.size(); ++i)
      if ((i % 5 == 0) == (strcmp(split, "train") == 0))
        selected.push_back(files[i]);
    files = std::move(selected);
  }
  if (files.empty())
    return 2;
  puts("file,bytes,rep,variant,reuse,wall_ns");
  fosu::FileBuffer input;
  for (size_t i = 0; i < files.size(); ++i) {
    if (!fosu::read_into(files[i].c_str(), input))
      return 1;
    for (int r = 0; r < reps; ++r) {
      for (size_t j = 0; j < modules.size() * 2; ++j) {
        const size_t slot = (j + r + i) % (modules.size() * 2);
        const int reuse = slot % 2;
        auto& m = modules[slot / 2];
        const auto begin = now();
        m.parse(m.ctx, input.data.get(), input.size, reuse);
        const auto ns = now() - begin;
        printf("%s,%zu,%d,%s,%d,%llu\n", files[i].c_str(), input.size, r, m.name, reuse,
               static_cast<unsigned long long>(ns));
      }
    }
  }
  for (auto& m : modules) {
    m.free(m.ctx);
    dlclose(m.dso);
  }
}
