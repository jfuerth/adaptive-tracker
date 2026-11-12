#pragma once

#include <cstdint>
#include <cstdio>
#include <vector>

namespace desktop {

/// Write mono float samples as a 16-bit PCM WAV file. Returns false on I/O error.
inline bool writeWavMono16(const char* path, const std::vector<float>& samples, uint32_t sampleRate) {
    std::FILE* f = std::fopen(path, "wb");
    if (!f) return false;

    auto u32 = [f](uint32_t v) {
        uint8_t b[4] = {uint8_t(v), uint8_t(v >> 8), uint8_t(v >> 16), uint8_t(v >> 24)};
        std::fwrite(b, 1, 4, f);
    };
    auto u16 = [f](uint16_t v) {
        uint8_t b[2] = {uint8_t(v), uint8_t(v >> 8)};
        std::fwrite(b, 1, 2, f);
    };

    const uint32_t dataBytes = static_cast<uint32_t>(samples.size() * 2);
    std::fwrite("RIFF", 1, 4, f);
    u32(36 + dataBytes);
    std::fwrite("WAVEfmt ", 1, 8, f);
    u32(16);            // fmt chunk size
    u16(1);             // PCM
    u16(1);             // mono
    u32(sampleRate);
    u32(sampleRate * 2);  // byte rate
    u16(2);             // block align
    u16(16);            // bits per sample
    std::fwrite("data", 1, 4, f);
    u32(dataBytes);

    for (float s : samples) {
        if (s > 1.0f) s = 1.0f;
        if (s < -1.0f) s = -1.0f;
        u16(static_cast<uint16_t>(static_cast<int16_t>(s * 32767.0f)));
    }
    bool ok = std::ferror(f) == 0;
    ok = (std::fclose(f) == 0) && ok;
    return ok;
}

} // namespace desktop
