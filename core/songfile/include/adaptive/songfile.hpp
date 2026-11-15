#pragma once

#include <adaptive/song.hpp>

#include <string>
#include <string_view>
#include <vector>

/**
 * The song file: a Song plus what only people need (names and stable ids),
 * as JSON. See docs/song-format.md for the format.
 *
 * Host-side only (phone and desktop): it uses the heap and exceptions
 * internally. Microcontroller builds take a song converted ahead of time.
 */
namespace adaptive::songfile {

inline constexpr const char* kFormat = "adaptive-tracker-song";
/// The one version this app reads and writes; files of any other are refused.
/// (No compatibility with older files until the format settles.)
inline constexpr int kVersion = 2;

/// A part of the song as people see it. `id` is stable (references in the
/// file use it); `name` is free to change.
struct Label {
    std::string id;
    std::string name;
};

/// Names and ids for everything in a Song, index for index.
struct SongInfo {
    std::string title;
    std::vector<Label> channels, patterns, sections, layers, params, events, instruments;
};

struct LoadResult {
    bool ok = false;
    /// Why it failed, with where in the file (e.g. "sections[1].channels.lead[0]: no pattern 'x'").
    std::string error;
};

/// Read a song file into `song` and `info` (both replaced). On failure they
/// are left in an unspecified state.
LoadResult load(std::string_view json, Song& song, SongInfo& info);

/// Write a song file. Entries without a label in `info` get ids made from
/// their kind and index.
std::string save(const Song& song, const SongInfo& info);

/// An id made from `name` ("Explore lead" -> "explore-lead"), unique among `taken`.
std::string makeId(std::string_view name, const std::vector<Label>& taken);

} // namespace adaptive::songfile
