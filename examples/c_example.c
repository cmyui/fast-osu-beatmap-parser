#include <fosu/c_api.h>
#include <stdio.h>

int main(int argc, char **argv) {
    if (argc != 2 || fosu_abi_version() != FOSU_ABI_VERSION) return 2;
    fosu_handle *handle = fosu_new();
    if (!handle) return 1;
    int status = fosu_parse_file(handle, argv[1], FOSU_ALL);
    if (status == FOSU_OK) {
        const fosu_view *view = fosu_get_view(handle);
        fwrite(view->text + view->metadata.title.offset,
               1, view->metadata.title.length, stdout);
        printf("\n%zu objects\n", view->hit_object_count);
    }
    fosu_free(handle);
    return status;
}
