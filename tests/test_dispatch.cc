#include <fosu/bindings/c_api.h>
#include <fosu/engine/cpu_features.h>
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <thread>
#include <vector>

int main(int argc, char** argv) {
  using namespace fosu_dispatch;
  CpuFeatures full{required_leaf1, required_leaf7, required_extended, 6};
  assert(supports_avx2(full));
  for (unsigned bit = 0; bit < 32; ++bit) {
    auto f = full;
    f.leaf1 &= ~(1u << bit);
    assert(supports_avx2(f) == !(required_leaf1 & (1u << bit)));
    f = full;
    f.leaf7 &= ~(1u << bit);
    assert(supports_avx2(f) == !(required_leaf7 & (1u << bit)));
    f = full;
    f.extended &= ~(1u << bit);
    assert(supports_avx2(f) == !(required_extended & (1u << bit)));
  }
  for (unsigned x = 0; x < 8; ++x) {
    auto f = full;
    f.xcr0 = x;
    assert(supports_avx2(f) == ((x & 6) == 6));
  }
  assert(argc == 2);
  const char* requested = argv[1];
  setenv("FOSU_BACKEND", requested, 1);
  const bool available = !strcmp(requested, "auto") || fosu_backend_available(requested);
  assert(!fosu_backend_available(nullptr) && !fosu_backend_available("unknown"));
  // Race the first selection and allocation; all handles must retain one ABI.
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
    threads.emplace_back([&] {
      auto* h = fosu_new();
      if (!available) {
        assert(!h && !fosu_backend_name());
        assert(fosu_parse(nullptr, nullptr, 0, FOSU_ALL) == FOSU_INVALID_ARGUMENT);
        fosu_free(nullptr);
        return;
      }
      assert(h);
      const char* expected = !strcmp(requested, "auto")
                                 ? (fosu_backend_available("avx2")   ? "avx2"
                                    : fosu_backend_available("neon") ? "neon"
                                                                     : "scalar")
                                 : requested;
      assert(!strcmp(fosu_backend_name(), expected));
      constexpr char input[] = "[Metadata]\nTitle:dispatch\n[HitObjects]\n1,2,3,1,0\n";
      assert(fosu_parse(h, input, sizeof(input) - 1, FOSU_ALL) == FOSU_OK);
      const auto* v = fosu_get_view(h);
      assert(v && v->hit_object_count == 1 && v->hit_objects[0].x == 1);
      fosu_free(h);
    });
  for (auto& t : threads)
    t.join();
  const char* before = fosu_backend_name();
  setenv("FOSU_BACKEND", "unknown", 1);
  assert(before == fosu_backend_name());
}
