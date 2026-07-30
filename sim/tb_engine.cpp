// Verilator testbench for the parser engine.
//
// Runs whole .osu files through the RTL and compares the emitted records
// against what fosu::parse() produced for the same file. Currently checks
// [TimingPoints], where the doubles are the interesting part: the RTL emits
// (mant, frac, neg) and this testbench finishes them with the same two
// operations parse_double uses, then compares RAW 64-BIT PATTERNS. Nothing is
// approximated.
//
// A line the RTL declines (over 18 significant digits, or an exponent) arrives
// as a PUNT record carrying its span; the testbench parses that line with the
// C++ and counts it, which is exactly the host's job in the real system.

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "Vfosu_engine.h"
#include "verilated.h"

#include <fosu/parser.hpp>

namespace {

constexpr int kWin = 64;

constexpr int TAG_TIMING = 1;
constexpr int TAG_MALFORMED = 2;
constexpr int TAG_PUNT = 3;
constexpr int TAG_HITOBJ = 4;
constexpr int TAG_SLIDER = 5;
constexpr int TAG_POINT = 6;
constexpr int TAG_KV = 7;

const std::string* g_file = nullptr;
uint64_t g_cycles = 0;
int g_fail = 0;
uint64_t g_tp = 0, g_punt = 0, g_malformed = 0;
uint64_t g_ho = 0, g_sl = 0, g_pt = 0, g_ho_punt = 0, g_kv = 0;

void serve_mem(Vfosu_engine* dut) {
    uint8_t w[kWin];
    std::memset(w, 0, sizeof w);
    const uint32_t a = dut->mem_addr;
    if (g_file && a < g_file->size()) {
        const size_t n = std::min(static_cast<size_t>(kWin), g_file->size() - a);
        std::memcpy(w, g_file->data() + a, n);
    }
    for (int i = 0; i < kWin / 4; ++i)
        dut->mem_data[i] = static_cast<uint32_t>(w[4 * i + 0]) |
                           static_cast<uint32_t>(w[4 * i + 1]) << 8 |
                           static_cast<uint32_t>(w[4 * i + 2]) << 16 |
                           static_cast<uint32_t>(w[4 * i + 3]) << 24;
}

void cycle(Vfosu_engine* dut) {
    serve_mem(dut);
    dut->eval();
    dut->clk = 1;
    dut->eval();
    dut->clk = 0;
    serve_mem(dut);
    dut->eval();
}

uint64_t d_to_bits(double d) {
    uint64_t u;
    std::memcpy(&u, &d, 8);
    return u;
}

// Rebuild a double from what the RTL emitted, using parse_double's own
// arithmetic so the comparison can be exact.
double rebuild(uint64_t mant, unsigned frac, bool neg) {
    double v = static_cast<double>(mant);
    if (frac) v /= fosu::detail::kPow10[frac];
    return neg ? -v : v;
}

struct HO {
    int32_t x, y, time, end_time;
    uint32_t type, hitsound;
    bool has_slider;
    std::string sample;
};

struct SL {
    char curve_type;
    int32_t slides;
    double length;
    std::string edge_sounds, edge_sets;
    std::vector<std::pair<int32_t,int32_t>> points;
};

struct TP {
    double time, beat_length;
    int32_t meter, sample_set, sample_index, volume;
    bool uninherited;
    uint32_t effects;
};

bool tp_equal(const TP& a, const fosu::TimingPoint& b) {
    return d_to_bits(a.time) == d_to_bits(b.time) &&
           d_to_bits(a.beat_length) == d_to_bits(b.beat_length) &&
           a.meter == b.meter && a.sample_set == b.sample_set &&
           a.sample_index == b.sample_index && a.volume == b.volume &&
           a.uninherited == b.uninherited && a.effects == b.effects;
}

}  // namespace

int main(int argc, char** argv) {
    Verilated::commandArgs(argc, argv);
    const std::string dir = argc > 1 ? argv[1] : "bench/corpus-large";
    const size_t limit = argc > 2 ? std::stoul(argv[2]) : 0;

    auto* dut = new Vfosu_engine;
    dut->clk = 0;
    dut->rec_ready = 1;

    std::printf("engine: key/value + [TimingPoints] + [HitObjects] vs fosu::parse\n");

    namespace fs = std::filesystem;
    std::vector<fs::path> files;
    std::error_code ec;
    for (const auto& e : fs::directory_iterator(dir, ec)) {
        if (!e.is_regular_file()) continue;
        if (e.path().filename().string().rfind("._", 0) == 0) continue;
        if (e.path().extension() == ".osu") files.push_back(e.path());
    }
    std::sort(files.begin(), files.end());
    if (limit && files.size() > limit) files.resize(limit);

    uint64_t bytes = 0;
    size_t nfiles = 0;

    for (const auto& f : files) {
        std::ifstream in(f, std::ios::binary);
        std::string raw((std::istreambuf_iterator<char>(in)),
                        std::istreambuf_iterator<char>());
        if (raw.empty()) continue;

        // The C++ reference over the same bytes.
        auto padded = fosu::make_padded(raw);
        fosu::Beatmap ref = fosu::parse(padded);

        g_file = &raw;
        // Start from the C++ defaults, then apply what the RTL emits: any field
        // the RTL never mentions must therefore still match the reference.
        fosu::Beatmap acc;
        bool ar_seen = false;
        // String fields are held as owned strings: assigning a string_view from
        // a temporary into `acc` would dangle.
        std::string sfield[40];
        bool sset[40] = {false};
        std::vector<TP> got;
        std::vector<HO> got_ho;
        std::vector<SL> got_sl;
        // Points accumulate per object and only commit with their slider: a
        // mid-walk failure makes the C++ resize the pool back and drop them.
        std::vector<std::pair<int32_t,int32_t>> pending_pts;
        SL pending_sl{};
        bool have_pending_sl = false;

        dut->rst_n = 0;
        dut->start = 0;
        dut->file_len = 0;
        for (int i = 0; i < 3; ++i) cycle(dut);
        dut->rst_n = 1;
        dut->file_len = static_cast<uint32_t>(raw.size());
        dut->start = 1;
        cycle(dut);
        dut->start = 0;

        // A slider control point costs ~5 cycles for as few as 4 bytes
        // ("|1:1"), so the budget has to be several cycles per byte -- a file
        // with a 22k-point slider is legitimately slow, not hung.
        const uint64_t budget = raw.size() * 4 + 500000;
        uint64_t spent = 0;
        while (!dut->done && spent < budget) {
            serve_mem(dut);
            dut->eval();
            if (dut->rec_valid && dut->rec_ready) {
                if (dut->rec_tag == TAG_TIMING) {
                    TP t{};
                    t.time = rebuild(dut->tp_time_mant, dut->tp_time_frac,
                                     dut->tp_time_neg);
                    t.beat_length = rebuild(dut->tp_bl_mant, dut->tp_bl_frac,
                                            dut->tp_bl_neg);
                    t.meter = static_cast<int32_t>(dut->tp_meter);
                    t.sample_set = static_cast<int32_t>(dut->tp_sample_set);
                    t.sample_index = static_cast<int32_t>(dut->tp_sample_index);
                    t.volume = static_cast<int32_t>(dut->tp_volume);
                    t.uninherited = dut->tp_uninherited != 0;
                    t.effects = dut->tp_effects;
                    got.push_back(t);
                    ++g_tp;
                } else if (dut->rec_tag == TAG_PUNT) {
                    // The host finishes this line, exactly as it would in
                    // hardware. Its result still has to land in order.
                    const std::string body =
                        raw.substr(dut->rec_start, dut->rec_len);
                    auto pb = fosu::make_padded(body);
                    fosu::Beatmap one;
                    if (dut->rec_section == 6) {
                        fosu::detail::parse_timing_point_line(one, pb.data.get(),
                                                              body.size());
                        if (one.timing_points.size() == 1) {
                            const auto& t = one.timing_points[0];
                            got.push_back(TP{t.time, t.beat_length, t.meter,
                                             t.sample_set, t.sample_index,
                                             t.volume, t.uninherited,
                                             t.effects});
                        }
                        ++g_punt;
                    } else {
                        fosu::detail::parse_hitobject_line(one, pb.data.get(),
                                                           body.size(),
                                                           body.size());
                        if (one.hit_objects.size() == 1) {
                            const auto& o = one.hit_objects[0];
                            HO h{};
                            h.x = o.x; h.y = o.y; h.time = o.time;
                            h.end_time = o.end_time; h.type = o.type;
                            h.hitsound = o.hitsound;
                            h.has_slider = o.slider != fosu::HitObject::kNoSlider;
                            h.sample = std::string(o.hit_sample);
                            got_ho.push_back(h);
                            if (h.has_slider && !one.sliders.empty()) {
                                const auto& sr = one.sliders[o.slider];
                                SL sl{};
                                sl.curve_type = sr.curve_type;
                                sl.slides = sr.slides;
                                sl.length = sr.length;
                                sl.edge_sounds = std::string(sr.edge_sounds);
                                sl.edge_sets = std::string(sr.edge_sets);
                                for (uint32_t k = 0; k < sr.point_count; ++k)
                                    sl.points.emplace_back(
                                        one.slider_points[sr.point_begin + k].x,
                                        one.slider_points[sr.point_begin + k].y);
                                got_sl.push_back(sl);
                            }
                            ++g_ho;
                        }
                        ++g_ho_punt;
                    }
                    pending_pts.clear();
                    have_pending_sl = false;
                } else if (dut->rec_tag == TAG_POINT) {
                    pending_pts.emplace_back(static_cast<int32_t>(dut->pt_x),
                                             static_cast<int32_t>(dut->pt_y));
                    ++g_pt;
                } else if (dut->rec_tag == TAG_SLIDER) {
                    pending_sl = SL{};
                    pending_sl.curve_type = static_cast<char>(dut->sl_curve_type);
                    pending_sl.slides = static_cast<int32_t>(dut->sl_slides);
                    pending_sl.length = rebuild(dut->sl_len_mant,
                                                dut->sl_len_frac,
                                                dut->sl_len_neg);
                    pending_sl.edge_sounds =
                        raw.substr(dut->sl_es_start, dut->sl_es_len);
                    pending_sl.edge_sets =
                        raw.substr(dut->sl_esets_start, dut->sl_esets_len);
                    pending_sl.points = pending_pts;
                    have_pending_sl = true;
                    ++g_sl;
                } else if (dut->rec_tag == TAG_HITOBJ) {
                    HO h{};
                    h.x = static_cast<int32_t>(dut->ho_x);
                    h.y = static_cast<int32_t>(dut->ho_y);
                    h.time = static_cast<int32_t>(dut->ho_time);
                    h.end_time = static_cast<int32_t>(dut->ho_end_time);
                    h.type = dut->ho_type;
                    h.hitsound = dut->ho_hitsound;
                    h.has_slider = dut->ho_has_slider != 0;
                    h.sample = raw.substr(dut->ho_sample_start,
                                          dut->ho_sample_len);
                    got_ho.push_back(h);
                    if (have_pending_sl) got_sl.push_back(pending_sl);
                    pending_pts.clear();
                    have_pending_sl = false;
                    ++g_ho;
                } else if (dut->rec_tag == TAG_KV) {
                    const std::string sv =
                        raw.substr(dut->kv_str_start, dut->kv_str_len);
                    const int64_t iv = dut->kv_i64_neg
                        ? -static_cast<int64_t>(dut->kv_i64)
                        : static_cast<int64_t>(dut->kv_i64);
                    const int32_t i32 = fosu::detail::clamp_i32(iv);
                    const double dv = rebuild(dut->kv_mant, dut->kv_frac,
                                              dut->kv_neg);
                    const bool bv = dut->kv_bool != 0;
                    switch (dut->kv_field) {
                        case 1:  sfield[1] = sv; sset[1] = true; break;
                        case 2:  acc.audio_lead_in = i32; break;
                        case 3:  acc.preview_time = i32; break;
                        case 4:  acc.countdown_offset = i32; break;
                        case 5:  acc.countdown = i32; break;
                        case 6:  sfield[6] = sv; sset[6] = true; break;
                        case 7:  acc.samples_match_playback_rate = bv; break;
                        case 8:  acc.stack_leniency = dv; break;
                        case 9:  acc.mode = i32; break;
                        case 10: acc.letterbox_in_breaks = bv; break;
                        case 11: acc.widescreen_storyboard = bv; break;
                        case 12: acc.epilepsy_warning = bv; break;
                        case 13: acc.special_style = bv; break;
                        case 14: acc.use_skin_sprites = bv; break;
                        case 15: sfield[15] = sv; sset[15] = true; break;
                        case 16: sfield[16] = sv; sset[16] = true; break;
                        case 17: sfield[17] = sv; sset[17] = true; break;
                        case 18: acc.distance_spacing = dv; break;
                        case 19: acc.beat_divisor = i32; break;
                        case 20: acc.grid_size = i32; break;
                        case 21: acc.timeline_zoom = dv; break;
                        case 22: sfield[22] = sv; sset[22] = true; break;
                        case 23: sfield[23] = sv; sset[23] = true; break;
                        case 24: sfield[24] = sv; sset[24] = true; break;
                        case 25: sfield[25] = sv; sset[25] = true; break;
                        case 26: sfield[26] = sv; sset[26] = true; break;
                        case 27: sfield[27] = sv; sset[27] = true; break;
                        case 28: sfield[28] = sv; sset[28] = true; break;
                        case 29: sfield[29] = sv; sset[29] = true; break;
                        case 30: acc.beatmap_id = iv; break;
                        case 31: acc.beatmap_set_id = iv; break;
                        case 32: acc.hp = dv; break;
                        case 33: acc.cs = dv; break;
                        case 34: acc.od = dv; break;
                        case 35: acc.ar = dv; ar_seen = true; break;
                        case 36: acc.slider_multiplier = dv; break;
                        case 37: acc.slider_tick_rate = dv; break;
                        default: break;
                    }
                    if (getenv("KVDBG"))
                        std::printf("  KV field=%u vtype=%u str=\"%s\" i64=%lld "
                                    "mant=%llu frac=%u\n",
                                    dut->kv_field, dut->kv_vtype, sv.c_str(),
                                    (long long)iv,
                                    (unsigned long long)dut->kv_mant,
                                    dut->kv_frac);
                    ++g_kv;
                } else if (dut->rec_tag == TAG_MALFORMED) {
                    // The C++ pops the object and discards any slider points it
                    // had already written.
                    pending_pts.clear();
                    have_pending_sl = false;
                    ++g_malformed;
                }
            }
            cycle(dut);
            ++spent;
            ++g_cycles;
        }
        if (spent >= budget) {
            std::printf("TIMEOUT on %s after %llu cycles\n",
                        f.filename().string().c_str(),
                        static_cast<unsigned long long>(spent));
            ++g_fail;
            break;
        }

        if (got.size() != ref.timing_points.size()) {
            if (g_fail < 8)
                std::printf("COUNT MISMATCH %s: rtl %zu timing points, ref %zu\n",
                            f.filename().string().c_str(), got.size(),
                            ref.timing_points.size());
            ++g_fail;
        }
        const size_t n = std::min(got.size(), ref.timing_points.size());
        for (size_t i = 0; i < n; ++i) {
            if (!tp_equal(got[i], ref.timing_points[i])) {
                if (g_fail < 8) {
                    const auto& a = got[i];
                    const auto& b = ref.timing_points[i];
                    std::printf("TP MISMATCH %s index %zu\n"
                                "  rtl time=%.17g (%016llx) bl=%.17g (%016llx) "
                                "meter=%d ss=%d si=%d vol=%d unin=%d eff=%u\n"
                                "  ref time=%.17g (%016llx) bl=%.17g (%016llx) "
                                "meter=%d ss=%d si=%d vol=%d unin=%d eff=%u\n",
                                f.filename().string().c_str(), i, a.time,
                                (unsigned long long)d_to_bits(a.time),
                                a.beat_length,
                                (unsigned long long)d_to_bits(a.beat_length),
                                a.meter, a.sample_set, a.sample_index, a.volume,
                                a.uninherited, a.effects, b.time,
                                (unsigned long long)d_to_bits(b.time),
                                b.beat_length,
                                (unsigned long long)d_to_bits(b.beat_length),
                                b.meter, b.sample_set, b.sample_index, b.volume,
                                b.uninherited, b.effects);
                }
                ++g_fail;
                break;
            }
        }
        // --- hit objects ---
        if (got_ho.size() != ref.hit_objects.size()) {
            if (g_fail < 8)
                std::printf("HO COUNT MISMATCH %s: rtl %zu, ref %zu\n",
                            f.filename().string().c_str(), got_ho.size(),
                            ref.hit_objects.size());
            ++g_fail;
        }
        const size_t hn = std::min(got_ho.size(), ref.hit_objects.size());
        for (size_t i = 0; i < hn; ++i) {
            const auto& a = got_ho[i];
            const auto& b = ref.hit_objects[i];
            const bool bslider = b.slider != fosu::HitObject::kNoSlider;
            if (a.x != b.x || a.y != b.y || a.time != b.time ||
                a.type != b.type || a.hitsound != b.hitsound ||
                a.end_time != b.end_time || a.has_slider != bslider ||
                a.sample != std::string(b.hit_sample)) {
                if (g_fail < 8)
                    std::printf("HO MISMATCH %s index %zu\n"
                                "  rtl x=%d y=%d t=%d type=%u hs=%u end=%d "
                                "slider=%d sample=\"%s\"\n"
                                "  ref x=%d y=%d t=%d type=%u hs=%u end=%d "
                                "slider=%d sample=\"%s\"\n",
                                f.filename().string().c_str(), i, a.x, a.y,
                                a.time, a.type, a.hitsound, a.end_time,
                                a.has_slider, a.sample.c_str(), b.x, b.y, b.time,
                                b.type, b.hitsound, b.end_time, bslider,
                                std::string(b.hit_sample).c_str());
                ++g_fail;
                break;
            }
        }

        // --- sliders (and their control points) ---
        if (got_sl.size() != ref.sliders.size()) {
            if (g_fail < 8)
                std::printf("SLIDER COUNT MISMATCH %s: rtl %zu, ref %zu\n",
                            f.filename().string().c_str(), got_sl.size(),
                            ref.sliders.size());
            ++g_fail;
        }
        const size_t sn = std::min(got_sl.size(), ref.sliders.size());
        for (size_t i = 0; i < sn; ++i) {
            const auto& a = got_sl[i];
            const auto& b = ref.sliders[i];
            bool bad = a.curve_type != b.curve_type || a.slides != b.slides ||
                       d_to_bits(a.length) != d_to_bits(b.length) ||
                       a.edge_sounds != std::string(b.edge_sounds) ||
                       a.edge_sets != std::string(b.edge_sets) ||
                       a.points.size() != b.point_count;
            if (!bad)
                for (uint32_t k = 0; k < b.point_count; ++k)
                    if (a.points[k].first != ref.slider_points[b.point_begin+k].x ||
                        a.points[k].second != ref.slider_points[b.point_begin+k].y) {
                        bad = true;
                        break;
                    }
            if (bad) {
                if (g_fail < 8)
                    std::printf("SLIDER MISMATCH %s index %zu\n"
                                "  rtl curve=%c slides=%d len=%.17g pts=%zu "
                                "es=\"%s\" esets=\"%s\"\n"
                                "  ref curve=%c slides=%d len=%.17g pts=%u "
                                "es=\"%s\" esets=\"%s\"\n",
                                f.filename().string().c_str(), i, a.curve_type,
                                a.slides, a.length, a.points.size(),
                                a.edge_sounds.c_str(), a.edge_sets.c_str(),
                                b.curve_type, b.slides, b.length, b.point_count,
                                std::string(b.edge_sounds).c_str(),
                                std::string(b.edge_sets).c_str());
                ++g_fail;
                break;
            }
        }

        // --- scalar Beatmap fields from the key/value sections ---
        // The C++ mirrors OD into AR when no ApproachRate line appeared.
        if (!ar_seen) acc.ar = acc.od;
        {
            auto sfail = [&](const char* what) {
                if (g_fail < 8)
                    std::printf("KV MISMATCH %s: %s\n",
                                f.filename().string().c_str(), what);
                ++g_fail;
            };
            #define CHK_S(field) if (acc.field != ref.field) sfail(#field);
            #define CHK_D(field) if (d_to_bits(acc.field) != \
                                    d_to_bits(ref.field)) sfail(#field);
            auto chk_str = [&](int fid, std::string_view refv,
                               const char* dflt, const char* what) {
                const std::string want(refv);
                const std::string got = sset[fid] ? sfield[fid]
                                                  : std::string(dflt);
                if (got != want) sfail(what);
            };
            chk_str(1,  ref.audio_filename,  "",       "audio_filename");
            chk_str(6,  ref.sample_set,      "Normal", "sample_set");
            chk_str(15, ref.overlay_position, "",      "overlay_position");
            chk_str(16, ref.skin_preference, "",       "skin_preference");
            chk_str(17, ref.bookmarks,       "",       "bookmarks");
            chk_str(22, ref.title,           "",       "title");
            chk_str(23, ref.title_unicode,   "",       "title_unicode");
            chk_str(24, ref.artist,          "",       "artist");
            chk_str(25, ref.artist_unicode,  "",       "artist_unicode");
            chk_str(26, ref.creator,         "",       "creator");
            chk_str(27, ref.version,         "",       "version");
            chk_str(28, ref.source,          "",       "source");
            chk_str(29, ref.tags,            "",       "tags");
            CHK_S(audio_lead_in) CHK_S(preview_time)
            CHK_S(countdown) CHK_S(mode)
            CHK_S(letterbox_in_breaks) CHK_S(widescreen_storyboard)
            CHK_S(epilepsy_warning) CHK_S(special_style) CHK_S(use_skin_sprites)
            CHK_S(samples_match_playback_rate) CHK_S(countdown_offset)
            CHK_S(beat_divisor) CHK_S(grid_size)
            CHK_S(beatmap_id) CHK_S(beatmap_set_id)
            CHK_D(stack_leniency) CHK_D(distance_spacing) CHK_D(timeline_zoom)
            CHK_D(hp) CHK_D(cs) CHK_D(od) CHK_D(ar)
            CHK_D(slider_multiplier) CHK_D(slider_tick_rate)
            #undef CHK_S
            #undef CHK_D
        }

        bytes += raw.size();
        ++nfiles;
        if (g_fail > 8) break;
    }

    std::printf("  corpus     : %zu files, %llu bytes\n", nfiles,
                static_cast<unsigned long long>(bytes));
    std::printf("  records    : %llu timing points (%llu punted), "
                "%llu hit objects (%llu punted), %llu sliders, %llu points, "
                "%llu kv fields, %llu malformed\n",
                static_cast<unsigned long long>(g_tp),
                static_cast<unsigned long long>(g_punt),
                static_cast<unsigned long long>(g_ho),
                static_cast<unsigned long long>(g_ho_punt),
                static_cast<unsigned long long>(g_sl),
                static_cast<unsigned long long>(g_pt),
                static_cast<unsigned long long>(g_kv),
                static_cast<unsigned long long>(g_malformed));
    if (bytes)
        std::printf("  throughput : %.2f bytes/cycle simulated\n",
                    double(bytes) / double(g_cycles));

    dut->final();
    delete dut;
    if (g_fail) {
        std::printf("FAIL: %d mismatches\n", g_fail);
        return 1;
    }
    std::printf("PASS: timing points and hit objects bit-identical to fosu::parse\n");
    return 0;
}
