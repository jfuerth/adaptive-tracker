// Writes the adaptive demo as a song file.
//
//   export_demo [out.json]   (default: songs/adaptive-demo.json)
#include <demo/adaptive_demo_info.hpp>

#include <cstdio>
#include <fstream>
#include <memory>

int main(int argc, char** argv) {
    const char* outPath = argc > 1 ? argv[1] : "songs/adaptive-demo.json";
    auto demo = std::make_unique<adaptive::demo::AdaptiveDemo>();
    const std::string json = adaptive::songfile::save(demo->song(), adaptive::demo::adaptiveDemoInfo());
    std::ofstream out(outPath, std::ios::binary);
    out << json;
    if (!out) {
        std::fprintf(stderr, "Failed to write %s\n", outPath);
        return 1;
    }
    std::printf("Wrote %s (%zu bytes)\n", outPath, json.size());
    return 0;
}
