// Fixed-seed synthetic smoke corpus; not representative performance data.
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>

uint64_t rng_state = 0x9E3779B97F4A7C15ull;
uint64_t rng() {
  rng_state += 0x9E3779B97F4A7C15ull;
  uint64_t z = rng_state;
  z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
  z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
  return z ^ (z >> 31);
}

std::string generate_map(int n_objects) {
  std::string out;
  out.reserve(static_cast<size_t>(n_objects) * 48 + 4096);
  out +=
      "osu file format v14\r\n\r\n[General]\r\nAudioFilename: audio.mp3\r\n"
      "AudioLeadIn: 0\r\nPreviewTime: 40000\r\nCountdown: 0\r\n"
      "SampleSet: Soft\r\nStackLeniency: 0.7\r\nMode: 0\r\n"
      "LetterboxInBreaks: 0\r\nWidescreenStoryboard: 1\r\n\r\n"
      "[Editor]\r\nDistanceSpacing: 1.1\r\nBeatDivisor: 4\r\nGridSize: 32\r\n"
      "TimelineZoom: 2\r\n\r\n"
      "[Metadata]\r\nTitle:Synthetic Benchmark\r\nTitleUnicode:Synthetic "
      "Benchmark\r\nArtist:fosu\r\nArtistUnicode:fosu\r\nCreator:bench\r\n"
      "Version:Expert\r\nSource:\r\nTags:benchmark corpus\r\n"
      "BeatmapID:1\r\nBeatmapSetID:1\r\n\r\n"
      "[Difficulty]\r\nHPDrainRate:5\r\nCircleSize:4\r\n"
      "OverallDifficulty:9\r\nApproachRate:9.4\r\nSliderMultiplier:1.8\r\n"
      "SliderTickRate:1\r\n\r\n"
      "[Events]\r\n0,0,\"bg.jpg\",0,0\r\n\r\n[TimingPoints]\r\n";

  char line[512];
  int t = 800 + static_cast<int>(rng() % 2000);
  const int tp_count = 20 + static_cast<int>(rng() % 60);
  int tp_time = t;
  for (int i = 0; i < tp_count; ++i) {
    if (i % 5 == 0) {
      snprintf(line, sizeof line, "%d,%.13f,4,2,1,%d,1,0\r\n", tp_time,
               280.0 + double(rng() % 2000) / 7.0, 40 + int(rng() % 60));
    } else {
      snprintf(line, sizeof line, "%d,-%.10f,4,2,1,%d,0,%d\r\n", tp_time,
               50.0 + double(rng() % 1500) / 10.0, 40 + int(rng() % 60), int(rng() % 2));
    }
    out += line;
    tp_time += 4000 + static_cast<int>(rng() % 20000);
  }

  out += "\r\n[HitObjects]\r\n";
  const char* samples[] = {"", ",0:0:0:0:", ",0:0:0:0:", ",2:0:0:0:"};
  for (int i = 0; i < n_objects; ++i) {
    const int x = static_cast<int>(rng() % 512);
    const int y = static_cast<int>(rng() % 384);
    const int hs_pool[] = {0, 0, 0, 2, 4, 8, 12, 6};
    const int hs = hs_pool[rng() % 8];
    const bool combo = rng() % 4 == 0;
    const uint64_t kind = rng() % 100;
    if (kind < 60) {
      snprintf(line, sizeof line, "%d,%d,%d,%d,%d%s\r\n", x, y, t, combo ? 5 : 1, hs,
               samples[rng() % 4]);
      out += line;
    } else if (kind < 96) {
      const char curves[] = {'B', 'P', 'L', 'B'};
      const int n_pts = 1 + static_cast<int>(rng() % 6);
      int off = snprintf(line, sizeof line, "%d,%d,%d,%d,%d,%c", x, y, t, combo ? 6 : 2,
                         hs, curves[rng() % 4]);
      for (int p = 0; p < n_pts; ++p)
        off += snprintf(line + off, sizeof line - off, "|%d:%d", int(rng() % 640),
                        int(rng() % 480));
      off +=
          snprintf(line + off, sizeof line - off, ",%d,%.2f,,%s\r\n", 1 + int(rng() % 3),
                   30.0 + double(rng() % 12000) / 20.0, samples[rng() % 4]);
      out += line;
    } else {
      snprintf(line, sizeof line, "256,192,%d,12,%d,%d%s\r\n", t, hs,
               t + 800 + int(rng() % 3000), samples[rng() % 4]);
      out += line;
    }
    t += 120 + static_cast<int>(rng() % 500);
  }
  return out;
}

int main(int argc, char** argv) {
  if (argc != 2) {
    fprintf(stderr, "usage: generate_corpus output-directory\n");
    return 2;
  }
  const std::filesystem::path dir(argv[1]);
  if (std::filesystem::exists(dir)) {
    fprintf(stderr, "output directory already exists\n");
    return 1;
  }
  std::filesystem::create_directories(dir);
  const int sizes[] = {400, 900, 1600, 2600, 5200};
  for (int i = 0; i < 40; ++i) {
    char name[20];
    snprintf(name, sizeof name, "%03d.osu", i);
    std::ofstream out(dir / name, std::ios::binary);
    out << generate_map(sizes[i % 5]);
    if (!out)
      return 1;
  }
}
