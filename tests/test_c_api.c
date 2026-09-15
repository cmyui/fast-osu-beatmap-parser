#include <fosu/c_api.h>

int main(void) {
  const char data[] = "[HitObjects]\n1,2,3,1,0\n";
  fosu_c_handle* handle = fosu_c_new();
  if (!handle)
    return 1;
  if (fosu_c_parse(handle, data, sizeof(data) - 1, NULL) != FOSU_C_OK)
    return 2;
  const fosu_c_view* view = fosu_c_get_view(handle);
  if (!view || view->hit_object_count != 1 || view->hit_objects[0].x != 1)
    return 3;
  fosu_c_free(handle);
  return 0;
}
