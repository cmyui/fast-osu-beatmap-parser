#include <sys/mman.h>

#include <fosu/parser.hpp>

#include "serialize.hpp"

int main(int argc, char** argv) {
    if (argc < 2 || argc > 3 ||
        (argc == 3 && std::string_view(argv[2]) != "--dump"))
        return 2;
    int fd = open(argv[1], O_RDONLY);
    if (fd < 0) return 1;
    struct stat st;
    if (fstat(fd, &st) || st.st_size < 0) return 1;
    size_t size = static_cast<size_t>(st.st_size);
    char* input = static_cast<char*>(mmap(nullptr, size + fosu::kBufferPadding,
                                          PROT_READ, MAP_PRIVATE, fd, 0));
    if (reinterpret_cast<uintptr_t>(input) >= uintptr_t(-4095)) return 1;

    // The file's partial last page is zero-filled by Linux. When padding
    // crosses into a page wholly beyond EOF, replace that reserved page
    // with anonymous zero memory: touching the file mapping would SIGBUS.
    size_t boundary = (size + 4095) & ~size_t(4095);
    if (size + fosu::kBufferPadding > boundary &&
        mmap(input + boundary, 4096, PROT_READ,
             MAP_PRIVATE | MAP_ANONYMOUS | MAP_FIXED, -1,
             0) != input + boundary)
        return 1;
    auto bm = fosu::parse(input, size);
    // Keep the complete result observable even without serialization.
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
    // The kernel reclaims the mapping, descriptor, and arena on exit.
    return 0;
}
