#include <fosu/engine/runtime/loader.h>
#include <fosu/runtime.h>

namespace fosu {
const ParsingEngine* runtime_engine() {
  return internal::selected_engine();
}
namespace {
struct RuntimeCleanup {
  ~RuntimeCleanup() { internal::unload_engine(); }
} cleanup;
}  // namespace
}  // namespace fosu
