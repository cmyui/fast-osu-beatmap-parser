#include <fosu/parser.hpp>
#include "../oneshot/serialize.hpp"
int main(int argc, char** argv) {
    if (argc < 2 || argc > 3 ||
        (argc == 3 && std::string_view(argv[2]) != "--dump"))
        return 2;
    auto buf = fosu::read_file_padded(argv[1]);
    if (!buf) return 1;
    auto bm = fosu::parse(buf);
    __asm__ volatile("" : : "g"(&bm) : "memory");
    if (argc > 2) {
        auto out = serialize(bm);
        size_t done = 0;
        while (done < out.size()) {
            auto n = write(1, out.data() + done, out.size() - done);
            if (n <= 0) return 1;
            done += n;
        }
    }
    return 0;
}
