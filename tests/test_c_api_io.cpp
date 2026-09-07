// Linux-only fault injection: errno must survive cleanup, including early EOF.
#include <fosu/c_api.h>
#include <cassert>
#include <cerrno>
#include <cstdlib>
#include <initializer_list>
#include <sys/stat.h>
#include <sys/syscall.h>
#include <unistd.h>

enum class Failure { none, stat, read, eof };
static Failure failure = Failure::none;

extern "C" int fstat(int fd, struct stat* st) noexcept {
    if (failure == Failure::stat) { errno = EBADF; return -1; }
    return static_cast<int>(syscall(SYS_fstat, fd, st));
}
extern "C" ssize_t read(int fd, void* data, size_t size) {
    if (failure == Failure::read) { errno = EACCES; return -1; }
    if (failure == Failure::eof) return 0;
    return syscall(SYS_read, fd, data, size);
}
extern "C" int close(int fd) {
    int result = static_cast<int>(syscall(SYS_close, fd));
    errno = ENOTTY;  // A successful cleanup call may change errno.
    return result;
}

int main() {
    char path[] = "/tmp/fosu-io-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && write(fd, "abcd", 4) == 4);
    close(fd);
    auto* h = fosu_new();
    assert(h);
    for (auto mode : {Failure::stat, Failure::read, Failure::eof}) {
        failure = mode;
        assert(fosu_parse_file(h, path, FOSU_ALL) == FOSU_IO_ERROR);
        const int expected = mode == Failure::stat ? EBADF : mode == Failure::read ? EACCES : EIO;
        assert(errno == expected);
        assert(fosu_get_view(h) == nullptr);
    }
    failure = Failure::none;
    assert(fosu_parse_file(h, path, FOSU_ALL) == FOSU_OK);
    fosu_free(h);
    unlink(path);
}
