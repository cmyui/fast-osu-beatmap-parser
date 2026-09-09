#include <fosu/engine/runtime/cpu_features.h>
#include <fosu/parser.h>
#include <fosu/runtime.h>
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
  bool avx2 = false, neon = false;
#ifdef FOSU_TEST_avx2
  avx2 = host_supports_avx2();
#endif
#ifdef FOSU_TEST_neon
  neon = host_supports_neon();
#endif
  const bool available = !strcmp(requested, "auto") || !strcmp(requested, "scalar") ||
                         (!strcmp(requested, "avx2") && avx2) ||
                         (!strcmp(requested, "neon") && neon);
  // Race the first selection and allocation; all callers must retain one engine.
  std::vector<std::thread> threads;
  for (int i = 0; i < 8; ++i)
    threads.emplace_back([&] {
      const auto* engine = fosu::runtime_engine();
      assert(bool(engine) == available);
      if (!engine) {
        return;
      }
      if (!strcmp(requested, "auto"))
        assert(engine->kind == (avx2   ? fosu::EngineKind::Avx2
                                : neon ? fosu::EngineKind::Neon
                                       : fosu::EngineKind::Scalar));
      if (!strcmp(requested, "scalar"))
        assert(engine->kind == fosu::EngineKind::Scalar);
      if (!strcmp(requested, "avx2"))
        assert(engine->kind == fosu::EngineKind::Avx2);
      if (!strcmp(requested, "neon"))
        assert(engine->kind == fosu::EngineKind::Neon);
      fosu::Parser parser(*engine);
      constexpr char input[] = "[Metadata]\nTitle:dispatch\n[HitObjects]\n1,2,3,1,0\n";
      auto result = parser.parse(input, sizeof(input) - 1);
      assert(result && result.value()->hit_objects.size() == 1);
      assert(result.value()->hit_objects[0].x == 1);
    });
  for (auto& t : threads)
    t.join();
  const auto* before = fosu::runtime_engine();
  setenv("FOSU_BACKEND", "unknown", 1);
  assert(before == fosu::runtime_engine());
}
