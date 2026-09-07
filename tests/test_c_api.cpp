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
#include <fosu/internal/arena.hpp>
#include <fosu/parser.hpp>
#include "support/c_api_view.hpp"
#include "support/canonical_dump.hpp"


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

// Arena growth and recycling: short lines exceed the initial estimates so
// every array grows (and may fall back to the heap); a freed handle's arena
// is recycled by the next handle, and results stay exact throughout.
void check_growth() {
    std::string many = "[TimingPoints]\n";
    for (int i = 0; i < 3000; ++i) many += std::to_string(i) + ",500\n";
    many += "[HitObjects]\n";
    for (int i = 0; i < 20000; ++i) many += "1,2,3,1,0\n";
    for (int i = 0; i < 5000; ++i) many += "1,2,3,2,0,B|1:2|3:4|5:6|7:8,1,10\n";
    const std::string smaller = "[HitObjects]\n1,2,4,1,2\n";
    for (int round = 0; round < 3; ++round) {
        auto* h = fosu_new();
        assert(h);
        check(h, many);
        const auto* v = fosu_get_view(h);
        assert(v->hit_object_count == 25000 && v->slider_count == 5000 &&
               v->point_count == 20000 && v->timing_point_count == 3000);
        assert(v->hit_objects[24999].slider == 4999 && v->sliders[4999].point_begin == 19996);
        check(h, smaller);  // smaller input on the same handle
        check(h, many);  // and back
        fosu_free(h);
    }
}

void check_arena_vector() {
    fosu::internal::ArenaVector<uint64_t> values;
    assert(values.empty() && values.size() == 0 && values.capacity() == 0);
    values.resize(0);
    values.set_size(0);
    values.push_back(42);
    bool rejected = false;
    try {
        values.reserve(SIZE_MAX / sizeof(uint64_t) + 1);
    } catch (const std::bad_alloc&) {
        rejected = true;
    }
    assert(rejected && values.size() == 1 && values[0] == 42);
    auto moved = std::move(values);
    assert(values.empty() && values.size() == 0 && values.capacity() == 0);
    assert(moved.size() == 1 && moved[0] == 42);
    moved.release();
    moved.resize(0);
    assert(moved.empty() && moved.size() == 0 && moved.capacity() == 0);
}

void check_all_fields() {
    const std::string input =
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
    auto* handle = fosu_new();
    assert(handle && !fosu_get_view(handle));
    check(handle, input);
    fosu_free(handle);
}

void check_same_input_reuses_capacity() {
    const std::string input = "[HitObjects]\n32,64,100,1,0\n64,128,200,1,2\n";
    auto* handle = fosu_new();
    check(handle, input);
    const auto* allocation = fosu_get_view(handle)->hit_objects;
    check(handle, input);
    assert(fosu_get_view(handle)->hit_objects == allocation);
    fosu_free(handle);
}

void check_empty_input_resets_defaults() {
    const std::string initial = "[General]\nSampleSet:Soft\n[Metadata]\nTitle:Before reset\n";
    auto* handle = fosu_new();
    check(handle, initial);
    check(handle, "");
    const auto* view = fosu_get_view(handle);
    assert(view->metadata.title.length == 0);
    assert(view->metadata.sample_set.length == 6);
    assert(memcmp(view->text + view->metadata.sample_set.offset, "Normal", 6) == 0);
    fosu_free(handle);
}

void check_hitobject_section_selection() {
    const std::string input =
        "[Metadata]\nTitle:Skipped title\n[HitObjects]\n96,192,300,1,4\n";
    auto* handle = fosu_new();
    check(handle, input, FOSU_HIT_OBJECTS);
    assert(fosu_get_view(handle)->metadata.title.length == 0);
    assert(fosu_get_view(handle)->hit_object_count == 1);
    fosu_free(handle);
}

void check_difficulty_section_selection() {
    const std::string input =
        "[Difficulty]\nOverallDifficulty:6\n[HitObjects]\n128,64,400,1,0\n";
    auto* handle = fosu_new();
    check(handle, input, FOSU_DIFFICULTY);
    assert(fosu_get_view(handle)->metadata.od == 6);
    assert(fosu_get_view(handle)->hit_object_count == 0);
    fosu_free(handle);
}

void check_reparse_owned_input() {
    const std::string input = "[HitObjects]\n192,96,500,2,0,B|256:192,1,100\n";
    auto* handle = fosu_new();
    check(handle, input);
    const auto* view = fosu_get_view(handle);
    std::string before, after;
    fosu_dump::dump(CApiView(*view), before);
    assert(fosu_parse(handle, view->text, view->source_size, FOSU_ALL) == FOSU_OK);
    fosu_dump::dump(CApiView(*fosu_get_view(handle)), after);
    assert(before == after);
    fosu_free(handle);
}

void check_invalid_input_clears_view() {
    auto* handle = fosu_new();
    check(handle, "[Metadata]\nTitle:Before invalid input\n");
    assert(fosu_parse(handle, nullptr, 1, FOSU_ALL) == FOSU_INVALID_ARGUMENT);
    assert(!fosu_get_view(handle));
    assert(fosu_parse_file(handle, "/fosu-file-does-not-exist.osu", FOSU_ALL) == FOSU_IO_ERROR);
    assert(errno == ENOENT);
    assert(!fosu_get_view(handle));
    assert(fosu_parse(handle, nullptr, 0, FOSU_ALL) == FOSU_OK);
    assert(fosu_parse(handle, "", SIZE_MAX, FOSU_ALL) == FOSU_INVALID_ARGUMENT);
    fosu_free(handle);
    fosu_free(nullptr);
}

void check_file_matches_bytes() {
    const std::string input =
        "[Metadata]\nTitle:File and bytes\n[HitObjects]\n256,192,600,8,0,750\n";
    char path[] = "/tmp/fosu-c-api-XXXXXX";
    const int fd = mkstemp(path);
    assert(fd >= 0 && write(fd, input.data(), input.size()) == static_cast<ssize_t>(input.size()));
    close(fd);
    auto* handle = fosu_new();
    assert(fosu_parse_file(handle, path, FOSU_ALL) == FOSU_OK);
    unlink(path);
    std::string from_file, from_bytes;
    fosu_dump::dump(CApiView(*fosu_get_view(handle)), from_file);
    check(handle, input);
    fosu_dump::dump(CApiView(*fosu_get_view(handle)), from_bytes);
    assert(from_file == from_bytes);
    fosu_free(handle);
}

#if defined(__linux__)
void check_allocation_failure_recovery() {
    const std::string input = "[Metadata]\nTitle:Before allocation failure\n";
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
        if (!own || fosu_parse(own, input.data(), input.size(), FOSU_ALL) != FOSU_OK) _exit(3);
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
}
#endif

void check_independent_threads() {
    const std::string input =
        "[Metadata]\nTitle:Concurrent handles\n[HitObjects]\n320,192,900,1,0\n";
    auto work = [&input] {
        auto* handle = fosu_new();
        for (int i = 0; i < 20; ++i) check(handle, input);
        fosu_free(handle);
    };
    std::thread a(work), b(work);
    a.join();
    b.join();
}

int main() {
    assert(fosu_abi_version() == FOSU_ABI_VERSION);
    check_arena_vector();
    check_growth();
    check_all_fields();
    check_same_input_reuses_capacity();
    check_empty_input_resets_defaults();
    check_hitobject_section_selection();
    check_difficulty_section_selection();
    check_reparse_owned_input();
    check_invalid_input_clears_view();
    check_file_matches_bytes();
#if defined(__linux__)
    check_allocation_failure_recovery();
#endif
    check_independent_threads();
    puts("C API: exact values, defaults, reuse, lifetime, errors and independent handles passed");
}
