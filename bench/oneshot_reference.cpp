// Baseline one-shot parse process, written the obvious way with the
// library: read the file, parse(), write the canonical dump to stdout.
// This is also the reference generator: its output defines "the same
// output as master".
#include <unistd.h>

#include <string>

#include <fosu/parser.hpp>

#include "../oneshot/dump.hpp"

int main(int argc, char** argv) {
    if (argc < 2) return 2;
    const fosu::FileBuffer buf = fosu::read_file_padded(argv[1]);
    if (!buf) return 1;
    const fosu::Beatmap bm = fosu::parse(buf);
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
