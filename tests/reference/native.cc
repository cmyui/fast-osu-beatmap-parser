// Canonical serialization of the native result for cross-interface equality.
#include <fosu/beatmap.h>
#include <fosu/parser.h>
#include <tests/support/canonical_dump.h>

#include <cstddef>
#include <string>
#include <sys/types.h>
#include <unistd.h>

int main(int argc, char** argv) {
  if (argc < 2)
    return 2;
  fosu::Parser parser;
  auto*        parsed = parser.parse_file(argv[1]);
  if (!parsed)
    return 1;
  const fosu::Beatmap& bm = *parsed;
  std::string          out;
  fosu_dump::dump(bm, out);
  size_t done = 0;
  while (done < out.size()) {
    const ssize_t w = write(1, out.data() + done, out.size() - done);
    if (w <= 0)
      return 1;
    done += static_cast<size_t>(w);
  }
  return 0;
}
