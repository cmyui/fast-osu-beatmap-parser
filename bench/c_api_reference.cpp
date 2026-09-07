#include <cstdio>
#include "c_api_view.hpp"
#include "../oneshot/dump.hpp"
int main(int argc, char** argv) {
    if (argc < 2) return 2;
    fosu_handle* h = fosu_new();
    if (!h) return 1;
    if (fosu_parse_file(h, argv[1], FOSU_ALL)) { fosu_free(h); return 1; }
    std::string out;
    fosu_dump::dump(CApiView(*fosu_get_view(h)), out);
    const bool ok = fwrite(out.data(), 1, out.size(), stdout) == out.size();
    fosu_free(h);
    return ok ? 0 : 1;
}
