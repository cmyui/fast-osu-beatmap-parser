// Canonical serialization of the native result for cross-interface equality.
#include <unistd.h>

#include <string>

#include <fosu/parser.hpp>

#include "../support/canonical_dump.hpp"

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const fosu::FileBuffer buf = fosu::read_file_padded(argv[1]);
    if (!buf) return 1;
    fosu::Parser parser;
    auto parsed = parser.parse(buf);
    if (!parsed) return 1;
    const fosu::Beatmap& bm = *parsed.value();
    std::string out;
    fosu_dump::dump(bm, out);
    size_t done = 0;
    while (done < out.size()) {
        const ssize_t w = write(1, out.data() + done, out.size() - done);
        if (w <= 0) return 1;
        done += static_cast<size_t>(w);
    }
    return 0;
}
