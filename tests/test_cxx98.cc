#include <fosu/c_api.h>
#include <cstring>

int main(int argc, char** argv) {
  if (argc != 2 || fosu_c_abi_version() != FOSU_C_ABI_VERSION || !fosu_c_backend_name())
    return 1;
  fosu_c_handle* handle = fosu_c_new();
  if (!handle)
    return 2;

  char data[] =
      "osu file format v14\n"
      "[General]\nMode:0\n"
      "[Metadata]\nTitle:C++98 caller\n"
      "[Difficulty]\nSliderMultiplier:1.4\n"
      "[TimingPoints]\n0,500,4,1,0,100,1,0\n"
      "[HitObjects]\n100,200,1000,1,0\n"
      "100,200,1100,2,0,B|120:220|130:230,2,100\n";
  if (fosu_c_parse(handle, data, sizeof(data) - 1, NULL) != FOSU_C_OK)
    return 3;
  const fosu_c_view* view = fosu_c_get_view(handle);
  if (!view || view->header.mode != 0 || view->hit_object_count != 2 ||
      view->timing_point_count != 1 || view->slider_count != 1 ||
      view->slider_path_count != 0 || view->slider_event_group_count != 0 ||
      view->hit_objects[1].slider != 0 || view->sliders[0].point_count != 2 ||
      view->slider_point_count != 2 || view->slider_points[0].x != 120 ||
      view->header.title.size != sizeof("C++98 caller") - 1 ||
      std::memcmp(view->header.title.data, "C++98 caller", view->header.title.size) != 0)
    return 4;
  std::memset(data, '?', sizeof(data) - 1);
  if (std::memcmp(view->header.title.data, "C++98 caller", view->header.title.size) != 0)
    return 5;

  fosu_c_options options = {};
  options.sections = FOSU_C_ALL_SECTIONS;
  options.calculate_slider_end_times = 1;
  options.calculate_slider_paths = 1;
  options.calculate_slider_events = 1;
  options.apply_stacking = 1;
  const char* file =
      "osu file format v14\n[General]\nMode:0\n"
      "[Difficulty]\nSliderMultiplier:1.4\n"
      "[TimingPoints]\n0,500,4,1,0,100,1,0\n"
      "[HitObjects]\n100,200,1000,1,0\n"
      "100,200,1100,2,0,B|120:220|130:230,2,100\n";
  if (fosu_c_parse(handle, file, std::strlen(file), &options) != FOSU_C_OK)
    return 6;
  view = fosu_c_get_view(handle);
  if (!view || view->slider_path_count != 1 || view->slider_paths[0].point_count < 2 ||
      view->slider_paths[0].cumulative_lengths == NULL ||
      view->slider_event_group_count != 1 || view->slider_events[0].count < 2 ||
      view->stacking_count != 2 ||
      view->hit_objects[1].end_time <= view->hit_objects[1].time)
    return 7;

  if (fosu_c_parse_file(handle, argv[1], NULL) != FOSU_C_OK)
    return 8;
  view = fosu_c_get_view(handle);
  if (!view || view->header.format_version != 128 || view->slider_count != 7 ||
      view->slider_segment_count == 0 || view->velocity_preset_count == 0)
    return 9;
  bool has_degree = false;
  bool has_fractional_point = false;
  for (size_t i = 0; i < view->slider_segment_count; ++i)
    has_degree |=
        view->slider_segments[i].has_degree && view->slider_segments[i].degree == 2;
  for (size_t i = 0; i < view->slider_point_count; ++i)
    has_fractional_point |=
        view->slider_points[i].x == 100.25f || view->slider_points[i].y == 100.5f;
  if (!has_degree || !has_fractional_point)
    return 10;

  options = fosu_c_options();
  options.sections = FOSU_C_METADATA;
  const char partial[] = "[Metadata]\nTitle:partial\n[HitObjects]\n1,2,3,1,0\n";
  if (fosu_c_parse(handle, partial, sizeof(partial) - 1, &options) != FOSU_C_OK)
    return 11;
  view = fosu_c_get_view(handle);
  if (!view || view->header.title.size != 7 || view->hit_object_count != 0)
    return 12;

  options.mods = FOSU_C_EASY | FOSU_C_HARD_ROCK;
  if (fosu_c_parse(handle, partial, sizeof(partial) - 1, &options) !=
          FOSU_C_INVALID_INPUT ||
      fosu_c_get_view(handle) != NULL)
    return 13;

  if (fosu_c_parse(handle, NULL, 1, NULL) != FOSU_C_INVALID_INPUT ||
      fosu_c_get_view(handle) != NULL ||
      fosu_c_last_error(handle).status != FOSU_C_INVALID_INPUT)
    return 14;
  if (fosu_c_parse_file(handle, "/not/a/real/beatmap.osu", NULL) != FOSU_C_IO_FAILURE ||
      fosu_c_last_error(handle).os_code == 0)
    return 15;
  fosu_c_free(handle);
  return 0;
}
