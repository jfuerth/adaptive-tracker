// The song file: round trips, the demo as a file, and helpful errors.
#include <doctest.h>

#include <adaptive/songfile.hpp>
#include <demo/adaptive_demo_info.hpp>
#include <json.hpp>

#include <fstream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

using namespace adaptive;
using Demo = demo::AdaptiveDemo;

namespace {

std::string demoJson() {
    auto d = std::make_unique<Demo>();
    return songfile::save(d->song(), demo::adaptiveDemoInfo());
}

/// Render `seconds` of `song` from its start section with the same cues.
std::vector<float> render(Song& song, double seconds) {
    auto engine = std::make_unique<Engine>(48000);
    engine->setSong(&song);
    engine->post(Command::gotoSection(song.startSection, Quantize::Now));
    std::vector<float> out(size_t(seconds * 48000));
    for (size_t pos = 0; pos < out.size(); pos += 256) {
        if (pos == 48000 * 2) engine->post(Command::setParam(1, 1.0f));  // danger: into combat
        if (pos == 48000 * 6) engine->post(Command::fireEvent(0));       // a stinger
        engine->render(out.data() + pos, uint32_t(std::min<size_t>(256, out.size() - pos)));
    }
    return out;
}

/// Load `json`, expecting failure; returns the error.
std::string loadError(const std::string& json) {
    auto song = std::make_unique<Song>();
    songfile::SongInfo info;
    const auto r = songfile::load(json, *song, info);
    CHECK_FALSE(r.ok);
    return r.error;
}

/// The demo's file with one substring replaced (which must be there).
std::string demoWith(const std::string& from, const std::string& to) {
    std::string j = demoJson();
    const size_t at = j.find(from);
    REQUIRE(at != std::string::npos);
    return j.replace(at, from.size(), to);
}

} // namespace

TEST_CASE("Song file: saving, loading and saving again gives the same file") {
    const std::string first = demoJson();
    auto song = std::make_unique<Song>();
    songfile::SongInfo info;
    const auto r = songfile::load(first, *song, info);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(songfile::save(*song, info) == first);
    // Patterns are listed under their channels, so a loaded song numbers them
    // channel by channel; the demo builds them in that order too.
    CHECK(info.title == "Adaptive demo");
    CHECK(info.patterns[Demo::kExplorePad].id == "explore-pad");
    CHECK(info.patterns[Demo::kExplorePad].name == "Explore pad");
}

TEST_CASE("Song file: the demo plays the same from its file as from code") {
    auto built = std::make_unique<Demo>();
    auto loaded = std::make_unique<Song>();
    songfile::SongInfo info;
    REQUIRE(songfile::load(demoJson(), *loaded, info).ok);
    CHECK(render(built->song(), 9.0) == render(*loaded, 9.0));
}

TEST_CASE("Song file: missing optional fields take defaults; unknown fields are ignored") {
    const char* json = R"({
        "format": "adaptive-tracker-song", "version": 2, "someFutureField": {"x": 1},
        "instruments": [{"id": "lead", "params": {"cutoff": 500}}],
        "channels": [{"id": "lead", "columns": [{"instrument": "lead", "pitch": "absolute"}, {"pitch": "absolute"}],
                      "patterns": [{"id": "a", "rows": 4, "ticksPerRow": 5,
                                    "cells": [{"row": 1, "column": 1, "note": "Bb3", "effect": "F80"}]}]}],
        "sections": [{"id": "main", "channels": {"lead": ["a", "a"]}}]
    })";
    auto song = std::make_unique<Song>();
    songfile::SongInfo info;
    const auto r = songfile::load(json, *song, info);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(song->tempoMilliBpm == 120000);
    CHECK(song->homeKey.scale == scales::kMajor);
    CHECK(info.instruments[0].name == "lead");  // the id stands in for a missing name
    CHECK(song->instruments[0].get(Instrument::Param::Cutoff) == 500.0f);
    CHECK(song->instruments[0].get(Instrument::Param::Shape) == Instrument().get(Instrument::Param::Shape));
    CHECK(song->channels[0].numColumns == 2);
    CHECK(song->channels[0].instruments[1] == kNoIndex);
    CHECK(song->patterns[0].ticksPerRow == 5);
    CHECK(song->patterns[0].cells[1][1].note == 58);
    CHECK(song->patterns[0].cells[1][1].effect == 'F');
    CHECK(song->patterns[0].cells[1][1].effectParam == 0x80);
    CHECK(song->sections[0].sequences[0].length == 2);
    CHECK(song->sections[0].beatsPerBar == 4);
}

TEST_CASE("Song file: degree cells and custom scales") {
    const char* json = R"({
        "format": "adaptive-tracker-song", "version": 2,
        "key": {"root": "F#", "scale": [0, 2, 3, 7, 8]},
        "channels": [{"id": "pad", "columns": [{"pitch": "degree"}],
                      "patterns": [{"id": "p", "rows": 2, "ticksPerRow": 24,
                                    "cells": [{"row": 0, "column": 0, "degree": 3, "octave": 4, "accidental": -1},
                                              {"row": 1, "column": 0, "off": true}]}]}]
    })";
    auto song = std::make_unique<Song>();
    songfile::SongInfo info;
    const auto r = songfile::load(json, *song, info);
    REQUIRE_MESSAGE(r.ok, r.error);
    CHECK(song->homeKey.root == 6);
    CHECK(song->homeKey.scale == scales::mask(0, 2, 3, 7, 8));
    CHECK(song->patterns[0].cells[0][0].note == 4 * 12 + 2);
    CHECK(song->patterns[0].cells[0][0].accidental == -1);
    CHECK(song->patterns[0].cells[1][0].isOff());
    // ... and they are written back the same way.
    const std::string again = songfile::save(*song, info);
    CHECK(again.find(R"({"row": 0, "column": 0, "degree": 3, "octave": 4, "accidental": -1})") != std::string::npos);
    CHECK(again.find(R"("scale": [0, 2, 3, 7, 8])") != std::string::npos);
}

TEST_CASE("Song file: errors say what is wrong and where") {
    CHECK(loadError("{") .find("parse error") != std::string::npos);
    CHECK(loadError(R"({"format": "something-else", "version": 2})") == "format: expected \"adaptive-tracker-song\"");
    CHECK(loadError(demoWith(R"("version": 2)", R"("version": 3)")).find("newer version of the app") !=
          std::string::npos);
    CHECK(loadError(demoWith(R"("version": 2)", R"("version": 1)")).find("older version of the app") !=
          std::string::npos);
    CHECK(loadError(demoWith(R"("lead": ["explore-lead"])", R"("lead": ["explore-bass"])")) ==
          "sections[0].channels.lead[0]: pattern 'explore-bass' belongs to channel 'bass'");
    CHECK(loadError(demoWith(R"("lead": ["explore-lead"])", R"("lead": ["nope"])")) ==
          "sections[0].channels.lead[0]: no pattern 'nope'");
    CHECK(loadError(demoWith(R"("pitch": "degree")", R"("pitch": "sideways")")) ==
          "channels[0].columns[0].pitch: 'sideways' is not one of absolute, transpose, degree");
    CHECK(loadError(demoWith(R"("ticksPerRow": 12)", R"("ticksPerRow": 0)")) ==
          "channels[0].patterns[0].ticksPerRow: must be from 1 to 255");
    CHECK(loadError(demoWith(R"("effect": "024")", R"("effect": "0x24")")).find("is not an effect like 024") !=
          std::string::npos);
    CHECK(loadError(demoWith(R"("id": "bass")", R"("id": "lead")")) ==
          "instruments[1].id: another instrument has id 'lead'");
    CHECK(loadError(demoWith(R"("voice": "subtractive")", R"("voice": "noise")")) ==
          "instruments[0].voice: 'noise' is a voice this version doesn't have; the song needs a newer version");
}

TEST_CASE("Song file: an instrument without a voice is subtractive") {
    auto song = std::make_unique<Song>();
    songfile::SongInfo info;
    // Written before voices had types: every "voice" renamed to a field the reader ignores.
    std::string json = demoJson();
    for (size_t at; (at = json.find(R"("voice")")) != std::string::npos;) json.replace(at, 7, R"("old")");
    const auto result = songfile::load(json, *song, info);
    REQUIRE_MESSAGE(result.ok, result.error);
    CHECK(songfile::save(*song, info) == demoJson());
}

TEST_CASE("Song file: ids from names") {
    std::vector<songfile::Label> taken = {{"explore-lead", "x"}};
    CHECK(songfile::makeId("Explore lead", {}) == "explore-lead");
    CHECK(songfile::makeId("Explore lead", taken) == "explore-lead-2");
    CHECK(songfile::makeId("  Hit! (big) ", {}) == "hit-big");
    CHECK(songfile::makeId("!!!", {}) == "item");
}

TEST_CASE("Song file: songs/adaptive-demo.json is up to date with the demo's code") {
    std::ifstream in(ADAPTIVE_SONGS_DIR "/adaptive-demo.json", std::ios::binary);
    REQUIRE(in);
    std::stringstream file;
    file << in.rdbuf();
    // If this fails after changing the demo or the format, run build/export_demo.
    CHECK(file.str() == demoJson());
}

TEST_CASE("Song file: songs/new-song.json (what New song starts from) loads and saves as written") {
    std::ifstream in(ADAPTIVE_SONGS_DIR "/new-song.json", std::ios::binary);
    REQUIRE(in);
    std::stringstream file;
    file << in.rdbuf();
    auto song = std::make_unique<Song>();
    songfile::SongInfo info;
    const auto result = songfile::load(file.str(), *song, info);
    INFO(result.error);
    REQUIRE(result.ok);
    CHECK(song->numChannels == 1);
    CHECK(song->numPatterns == 1);
    CHECK(song->numSections == 1);
    CHECK(song->sections[0].sequences[0].length == 1);
    // Everything in the file survives a save (key order and spacing aside).
    CHECK(nlohmann::json::parse(songfile::save(*song, info)) == nlohmann::json::parse(file.str()));
}
