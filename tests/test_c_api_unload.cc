#include <fosu/bindings/c_api.h>

#include <dlfcn.h>
#include <sys/mman.h>
#include <sys/wait.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#if defined(__APPLE__)
#include <mach/mach.h>
#include <mach/mach_vm.h>
#endif

void require(bool condition, const char* message) {
  if (!condition) {
    fprintf(stderr, "C API unload: %s\n", message);
    std::_Exit(1);
  }
}
template <typename T>
T symbol(void* library, const char* name) {
  auto function = reinterpret_cast<T>(dlsym(library, name));
  require(function != nullptr, name);
  return function;
}

bool mapped(uintptr_t address) {
#if defined(__APPLE__)
  // Darwin's mincore succeeds even for unmapped holes. Region lookup must
  // return a region covering the address, rather than the next mapping.
  mach_vm_address_t start = address;
  mach_vm_size_t size = 0;
  vm_region_basic_info_data_64_t info;
  mach_msg_type_number_t count = VM_REGION_BASIC_INFO_COUNT_64;
  mach_port_t object = MACH_PORT_NULL;
  kern_return_t result =
      mach_vm_region(mach_task_self(), &start, &size, VM_REGION_BASIC_INFO_64,
                     reinterpret_cast<vm_region_info_t>(&info), &count, &object);
  if (object != MACH_PORT_NULL)
    mach_port_deallocate(mach_task_self(), object);
  require(result == KERN_SUCCESS || result == KERN_INVALID_ADDRESS,
          "mach_vm_region failed unexpectedly");
  return result == KERN_SUCCESS && start <= address && address - start < size;
#else
  unsigned char residency;
  errno = 0;
  const auto page_size = static_cast<uintptr_t>(getpagesize());
  const auto page = address & ~(page_size - 1);
  int result = mincore(reinterpret_cast<void*>(page), static_cast<size_t>(getpagesize()),
                       &residency);
  require(result == 0 || errno == ENOMEM, "mincore failed unexpectedly");
  return result == 0;
#endif
}

struct ExitApi {
  decltype(&fosu_new) make;
  decltype(&fosu_parse) parse;
  decltype(&fosu_get_view) view;
  decltype(&fosu_free) release;
  uintptr_t address;
  int done;
};

bool loaded(const std::string& path) {
  void* library = dlopen(path.c_str(), RTLD_NOW | RTLD_NOLOAD);
  if (library)
    dlclose(library);
  return library != nullptr;
}

ExitApi exit_api;

void parse_at_exit() {
  require(!mapped(exit_api.address), "late callback ran before arena cleanup");
  auto* handle = exit_api.make();
  require(handle != nullptr, "late fosu_new failed");
  const char input[] = "[Metadata]\nTitle:after cleanup\n[HitObjects]\n4,5,6,1,0\n";
  require(exit_api.parse(handle, input, sizeof(input) - 1, FOSU_ALL) == FOSU_OK,
          "late parse failed");
  const auto* view = exit_api.view(handle);
  require(view && view->hit_object_count == 1 && view->hit_objects[0].x == 4 &&
              view->metadata.title.length == 13 &&
              memcmp(view->text + view->metadata.title.offset, "after cleanup", 13) == 0,
          "late parse returned incorrect results");
  exit_api.release(handle);
  require(write(exit_api.done, "x", 1) == 1, "late callback notification failed");
}

void check_late_exit(const char* path) {
  int done[2];
  require(pipe(done) == 0, "pipe failed");
  pid_t child = fork();
  require(child >= 0, "fork failed");
  if (child == 0) {
    close(done[0]);
    exit_api.done = done[1];
    // Earlier registration runs after the library's own exit cleanup.
    // The library stays loaded: calling into it after dlclose is invalid.
    require(std::atexit(parse_at_exit) == 0, "atexit registration failed");
    void* library = dlopen(path, RTLD_NOW | RTLD_LOCAL);
    require(library != nullptr, "late-exit dlopen failed");
    exit_api.make = symbol<decltype(&fosu_new)>(library, "fosu_new");
    exit_api.parse = symbol<decltype(&fosu_parse)>(library, "fosu_parse");
    exit_api.view = symbol<decltype(&fosu_get_view)>(library, "fosu_get_view");
    exit_api.release = symbol<decltype(&fosu_free)>(library, "fosu_free");
    auto* handle = exit_api.make();
    require(handle != nullptr, "late-exit fosu_new failed");
    require(exit_api.parse(handle, nullptr, 0, FOSU_ALL) == FOSU_OK,
            "late-exit initial parse failed");
    exit_api.address = reinterpret_cast<uintptr_t>(exit_api.view(handle)->text);
    exit_api.release(handle);
    std::exit(0);
  }
  close(done[1]);
  int status = 0;
  require(waitpid(child, &status, 0) == child, "waitpid failed");
  char completed = 0;
  ssize_t count = read(done[0], &completed, 1);
  close(done[0]);
  require(WIFEXITED(status) && WEXITSTATUS(status) == 0 && count == 1 && completed == 'x',
          "parse from a late host exit callback failed");
}

int main(int argc, char** argv) {
  require(argc == 2, "expected the shared library path");
  const std::string core_path = argv[1];
  const auto directory = core_path.substr(0, core_path.find_last_of('/') + 1);
#if defined(__APPLE__)
  const char* suffix = ".dylib";
#else
  const char* suffix = ".so";
#endif
  const char* kinds[] = {"avx2", "neon"};
  const std::string input =
      "[Metadata]\nTitle:before recycling\n[HitObjects]\n"
      "1,2,3,1,0\n" +
      std::string(256 * 1024, ' ');
  for (int round = 0; round < 3; ++round) {
    void* library = dlopen(argv[1], RTLD_NOW | RTLD_LOCAL);
    require(library != nullptr, "dlopen failed");
    const auto make = symbol<decltype(&fosu_new)>(library, "fosu_new");
    const auto parse = symbol<decltype(&fosu_parse)>(library, "fosu_parse");
    const auto view = symbol<decltype(&fosu_get_view)>(library, "fosu_get_view");
    const auto release = symbol<decltype(&fosu_free)>(library, "fosu_free");
    const auto available =
        symbol<decltype(&fosu_backend_available)>(library, "fosu_backend_available");
    const auto name = symbol<decltype(&fosu_backend_name)>(library, "fosu_backend_name");
    for (const auto* kind : kinds) {
      (void)available(kind);
      require(!loaded(directory + "libfosu_engine_" + kind + suffix),
              "availability check loaded an engine");
    }
    auto* first = make();
    require(first != nullptr, "fosu_new failed");
    for (const auto* kind : kinds) {
      require(loaded(directory + "libfosu_engine_" + kind + suffix) ==
                  (strcmp(name(), kind) == 0),
              "selection did not load exactly its requested engine");
    }
    require(parse(first, input.data(), input.size(), FOSU_ALL) == FOSU_OK,
            "initial parse failed");
    require(view(first)->hit_object_count == 1, "initial result is incorrect");
    const auto address = reinterpret_cast<uintptr_t>(view(first)->text);
    release(first);
    require(mapped(address), "freeing a handle did not retain its reusable arena");
    auto* second = make();
    require(second != nullptr, "second fosu_new failed");
    require(parse(second, nullptr, 0, FOSU_ALL) == FOSU_OK, "recycled parse failed");
    const auto* fresh = view(second);
    require(reinterpret_cast<uintptr_t>(fresh->text) == address,
            "arena was not recycled");
    require(fresh->source_size == 0 && fresh->hit_object_count == 0 &&
                fresh->metadata.title.length == 0,
            "recycled arena retained logical results");
    require(fresh->metadata.sample_set == FOSU_SAMPLE_NORMAL,
            "recycled arena lost the default sample set");
    release(second);
    require(dlclose(library) == 0, "dlclose failed");
    void* remaining = dlopen(argv[1], RTLD_NOW | RTLD_NOLOAD);
    if (remaining)
      dlclose(remaining);
    require(remaining == nullptr,
            "library remained loaded after dlclose; the unload check did not run");
    for (const auto* kind : kinds)
      require(!loaded(directory + "libfosu_engine_" + kind + suffix),
              "engine remained loaded after core teardown");
    require(!mapped(address), "unloaded library leaked its parked arena");
  }
  check_late_exit(argv[1]);
  puts(
      "C API: recycling, fresh results, actual unload and late host exit callback "
      "passed");
}
