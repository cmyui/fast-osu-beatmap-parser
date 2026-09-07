#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>
#include <unistd.h>
#if defined(__linux__)
#include <sys/resource.h>
#include <sys/wait.h>
#endif
#include <fosu/parser.hpp>
#include "../bench/c_api_view.hpp"
#include "../oneshot/dump.hpp"

const std::string map =
    "osu file format v14\n[General]\nAudioFilename: song.mp3\n"
    "LetterboxInBreaks:1\nWidescreenStoryboard:1\nEpilepsyWarning:1\n"
    "SpecialStyle:1\nUseSkinSprites:1\nSamplesMatchPlaybackRate:1\n"
    "[Metadata]\nTitle:日本語\nArtist:artist\nBeatmapID:12345\n"
    "[Difficulty]\nOverallDifficulty:9\n"
    "[Events]\n0,0,\"bg.jpg\",0,0\n2,100,200\n"
    "[Colours]\nCombo1 : 12,34,56\n"
    "[TimingPoints]\n1.25,342.857142857142857142857,4,2,1,60,1,0\n"
    "[HitObjects]\n-48,192,1000,1,0,0:0:0:0:\n"
    "512,192,2000,2,14,B|-129088:1726|123:456,2,240,2|0,0:0|0:0,0:0:0:0:\n"
    "256,192,2147483647,12,0,2147483647,0:0:0:0:\n";

void check(fosu_handle* h, const std::string& input, uint32_t sections = FOSU_ALL) {
    assert(fosu_parse(h, input.data(), input.size(), sections) == FOSU_OK);
    const auto* v = fosu_get_view(h);
    assert(v && v->source_size == input.size());
    auto padded = fosu::make_padded(input);
    auto expected = fosu::parse(padded, {.sections = sections});
    std::string a, b;
    fosu_dump::dump(expected, a);
    fosu_dump::dump(CApiView(*v), b);
    assert(a == b);
    auto string = [&](fosu_string_ref ref) {
        assert(uint64_t(ref.offset) + ref.length <= v->text_size);
    };
    const auto& m = v->metadata;
    for (auto ref : {m.audio_filename, m.sample_set, m.overlay_position,
         m.skin_preference, m.bookmarks, m.title, m.title_unicode, m.artist,
         m.artist_unicode, m.creator, m.version, m.source, m.tags, m.background, m.video})
        string(ref);
    for (size_t i = 0; i < v->hit_object_count; ++i) {
        string(v->hit_objects[i].hit_sample);
        assert(v->hit_objects[i].reserved == 0);
    }
    for (size_t i = 0; i < v->slider_count; ++i) {
        const auto& slider = v->sliders[i];
        string(slider.edge_sounds); string(slider.edge_sets);
        for (auto byte : slider.reserved) assert(byte == 0);
    }
    for (size_t i = 0; i < v->timing_point_count; ++i)
        for (auto byte : v->timing_points[i].reserved) assert(byte == 0);
}

int main() {
    assert(fosu_abi_version() == FOSU_ABI_VERSION);
    auto* h = fosu_new();
    assert(h && !fosu_get_view(h));
    check(h, map);
    const auto* v = fosu_get_view(h);
    auto* objects = v->hit_objects;
    check(h, map);
    assert(fosu_get_view(h)->hit_objects == objects);  // capacity reuse
    check(h, "");
    v = fosu_get_view(h);
    assert(v->metadata.sample_set.length == 6);
    assert(memcmp(v->text + v->metadata.sample_set.offset, "Normal", 6) == 0);
    check(h, map, FOSU_HIT_OBJECTS);
    assert(fosu_get_view(h)->metadata.title.length == 0);
    check(h, map, FOSU_DIFFICULTY);
    assert(fosu_get_view(h)->hit_object_count == 0);
    check(h, map);
    v = fosu_get_view(h);
    assert(fosu_parse(h, v->text, v->source_size, FOSU_ALL) == FOSU_OK);
    assert(fosu_get_view(h)->hit_object_count == 3);
    assert(fosu_parse(h, nullptr, 1, FOSU_ALL) == FOSU_INVALID_ARGUMENT);
    assert(!fosu_get_view(h));
    assert(fosu_parse_file(h, "/fosu-file-does-not-exist.osu", FOSU_ALL) == FOSU_IO_ERROR);
    assert(errno == ENOENT);
    assert(!fosu_get_view(h));
    assert(fosu_parse(h, nullptr, 0, FOSU_ALL) == FOSU_OK);
    assert(fosu_parse(h, "", SIZE_MAX, FOSU_ALL) == FOSU_INVALID_ARGUMENT);
    char path[] = "/tmp/fosu-c-api-XXXXXX";
    int fd = mkstemp(path);
    assert(fd >= 0 && write(fd, map.data(), map.size()) == static_cast<ssize_t>(map.size()));
    close(fd);
    assert(fosu_parse_file(h, path, FOSU_ALL) == FOSU_OK);
    unlink(path);
    assert(fosu_get_view(h)->hit_object_count == 3);
    std::string from_file, from_bytes;
    fosu_dump::dump(CApiView(*fosu_get_view(h)), from_file);
    check(h, map);
    fosu_dump::dump(CApiView(*fosu_get_view(h)), from_bytes);
    assert(from_file == from_bytes);
    fosu_free(h);
    fosu_free(nullptr);
#if defined(__linux__)
    // Exercise exception translation with the private bundled C++ runtime.
    // A sparse file and a child-only address-space limit avoid touching RAM.
    char large_path[] = "/tmp/fosu-c-api-oom-XXXXXX";
    int large_fd = mkstemp(large_path);
    assert(large_fd >= 0 && ftruncate(large_fd, FOSU_MAX_INPUT_SIZE + 1ul) == 0);
    auto* limited = fosu_new();
    assert(limited);
    assert(fosu_parse_file(limited, large_path, FOSU_ALL) == FOSU_INVALID_ARGUMENT);
    assert(!fosu_get_view(limited));
    fosu_free(limited);
    assert(ftruncate(large_fd, FOSU_MAX_INPUT_SIZE) == 0);
    close(large_fd);
    pid_t child = fork();
    assert(child >= 0);
    if (child == 0) {
        rlimit limit{64ul << 20, 64ul << 20};
        if (setrlimit(RLIMIT_AS, &limit)) _exit(2);
        auto* own = fosu_new();
        if (!own || fosu_parse(own, map.data(), map.size(), FOSU_ALL) != FOSU_OK) _exit(3);
        int status = fosu_parse_file(own, large_path, FOSU_ALL);
        bool invalid = !fosu_get_view(own);
        bool recovered = fosu_parse(own, nullptr, 0, FOSU_ALL) == FOSU_OK;
        fosu_free(own);
        _exit(status == FOSU_OUT_OF_MEMORY && invalid && recovered ? 0 : 4);
    }
    int child_status = 0;
    assert(waitpid(child, &child_status, 0) == child);
    unlink(large_path);
    assert(WIFEXITED(child_status) && WEXITSTATUS(child_status) == 0);
#endif
    auto work = [] {
        auto* own = fosu_new();
        for (int i = 0; i < 20; ++i) check(own, map);
        fosu_free(own);
    };
    std::thread a(work), b(work);
    a.join(); b.join();
    puts("C API: exact values, defaults, reuse, lifetime, errors and independent handles passed");
}
