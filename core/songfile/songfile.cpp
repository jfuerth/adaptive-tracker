#include <adaptive/songfile.hpp>

#include <json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstdlib>
#include <cmath>
#include <map>
#include <stdexcept>

namespace adaptive::songfile {

using nlohmann::ordered_json;
using Json = ordered_json;

namespace {

// ---------------------------------------------------------------- errors

/// A problem in the file, with where it is ("channels[2].patterns[0].rows").
struct FormatError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

[[noreturn]] void fail(const std::string& where, const std::string& what) {
    throw FormatError(where.empty() ? what : where + ": " + what);
}

std::string at(const std::string& where, const std::string& key) { return where.empty() ? key : where + "." + key; }
std::string at(const std::string& where, size_t index) { return where + "[" + std::to_string(index) + "]"; }

// ---------------------------------------------------------------- reading helpers

const Json* field(const Json& obj, const char* key) {
    auto it = obj.find(key);
    return it == obj.end() || it->is_null() ? nullptr : &*it;
}

const Json& object(const Json& j, const std::string& where) {
    if (!j.is_object()) fail(where, "expected an object");
    return j;
}

const Json& array(const Json& obj, const char* key, const std::string& where) {
    static const Json empty = Json::array();
    const Json* j = field(obj, key);
    if (j == nullptr) return empty;
    if (!j->is_array()) fail(at(where, key), "expected a list");
    return *j;
}

std::string string(const Json& obj, const char* key, const std::string& where, const char* fallback = nullptr) {
    const Json* j = field(obj, key);
    if (j == nullptr) {
        if (fallback != nullptr) return fallback;
        fail(at(where, key), "missing");
    }
    if (!j->is_string()) fail(at(where, key), "expected text");
    return j->get<std::string>();
}

double number(const Json& obj, const char* key, const std::string& where, double fallback, double min, double max) {
    const Json* j = field(obj, key);
    if (j == nullptr) {
        if (std::isnan(fallback)) fail(at(where, key), "missing");
        return fallback;
    }
    if (!j->is_number()) fail(at(where, key), "expected a number");
    const double v = j->get<double>();
    // Compared as floats: the song stores floats, and files hold the
    // shortest decimals that read back as those floats (0.0005, not 0.0005000000237).
    if (!(float(v) >= float(min) && float(v) <= float(max))) {
        char range[64];
        std::snprintf(range, sizeof range, "must be from %g to %g", min, max);
        fail(at(where, key), range);
    }
    return v;
}

constexpr double kRequired = NAN;

long integer(const Json& obj, const char* key, const std::string& where, double fallback, long min, long max) {
    const Json* j = field(obj, key);
    if (j != nullptr && !j->is_number_integer()) fail(at(where, key), "expected a whole number");
    return static_cast<long>(number(obj, key, where, fallback, double(min), double(max)));
}

bool boolean(const Json& obj, const char* key, const std::string& where, bool fallback) {
    const Json* j = field(obj, key);
    if (j == nullptr) return fallback;
    if (!j->is_boolean()) fail(at(where, key), "expected true or false");
    return j->get<bool>();
}

/// One of `names` (by position); `fallback` if absent.
template<size_t N>
int choice(const Json& obj, const char* key, const std::string& where, const char* const (&names)[N],
           int fallback = -1) {
    if (field(obj, key) == nullptr && fallback >= 0) return fallback;
    const std::string s = string(obj, key, where);
    for (size_t i = 0; i < N; ++i) {
        if (s == names[i]) return int(i);
    }
    std::string list;
    for (size_t i = 0; i < N; ++i) list += (i ? ", " : "") + std::string(names[i]);
    fail(at(where, key), "'" + s + "' is not one of " + list);
}

/// Ids of one kind of thing, for references.
struct IdTable {
    const char* kind;
    std::map<std::string, uint8_t> index;

    void add(const std::string& id, uint8_t i, const std::string& where) {
        if (id.empty()) fail(at(where, "id"), "must not be empty");
        if (!index.emplace(id, i).second) fail(at(where, "id"), std::string("another ") + kind + " has id '" + id + "'");
    }
    uint8_t find(const std::string& id, const std::string& where) const {
        auto it = index.find(id);
        if (it == index.end()) fail(where, std::string("no ") + kind + " '" + id + "'");
        return it->second;
    }
    uint8_t ref(const Json& obj, const char* key, const std::string& where) const {
        return find(string(obj, key, where), at(where, key));
    }
};

Label label(const Json& obj, const std::string& where) {
    Label l;
    l.id = string(obj, "id", where);
    l.name = string(obj, "name", where, l.id.c_str());
    return l;
}

void checkCapacity(size_t n, uint32_t max, const std::string& where, const char* what) {
    if (n > max) fail(where, "too many " + std::string(what) + " (" + std::to_string(n) + "; the most is " +
                                 std::to_string(max) + ")");
}

// ---------------------------------------------------------------- vocabulary

const char* const kPitchModes[] = {"absolute", "transpose", "degree"};
/// Voice types this version knows. A file naming any other is refused rather
/// than played with the wrong voice.
const char* const kVoices[] = {"subtractive"};
const char* const kQuantize[] = {"now", "beat", "bar"};
const char* const kEntries[] = {"restart", "keepPhase"};
const char* const kCurves[] = {"linear", "exponential"};
// Mapping::Target order, without None.
const char* const kTargets[] = {"channelGain", "layerGain", "channelCutoff", "masterCutoff",
                                "tempoScale",  "transpose", "threshold"};

const char* const kNoteNames[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};

struct NamedScale {
    const char* name;
    uint16_t mask;
};
const NamedScale kScales[] = {
    {"major", scales::kMajor},
    {"minor", scales::kNaturalMinor},
    {"harmonicMinor", scales::kHarmonicMinor},
    {"dorian", scales::kDorian},
    {"phrygian", scales::kPhrygian},
    {"lydian", scales::kLydian},
    {"mixolydian", scales::kMixolydian},
    {"locrian", scales::kLocrian},
    {"majorPentatonic", scales::kMajorPentatonic},
    {"minorPentatonic", scales::kMinorPentatonic},
    {"chromatic", scales::kChromatic},
};

/// "C", "F#", "Bb" -> pitch class; -1 if not a note name.
int pitchClass(const std::string& s, size_t* used = nullptr) {
    if (s.empty()) return -1;
    static const int base[] = {9, 11, 0, 2, 4, 5, 7};  // A..G
    const char letter = static_cast<char>(std::toupper(static_cast<unsigned char>(s[0])));
    if (letter < 'A' || letter > 'G') return -1;
    int pc = base[letter - 'A'];
    size_t n = 1;
    if (n < s.size() && s[n] == '#') { ++pc; ++n; }
    else if (n < s.size() && s[n] == 'b') { --pc; ++n; }
    if (used != nullptr) *used = n;
    return (pc + 12) % 12;
}

/// "C4" (middle C, MIDI 60), "F#2", "Bb-1".
int parseNote(const std::string& s, const std::string& where) {
    size_t n = 0;
    const int pc = pitchClass(s, &n);
    if (pc < 0 || n >= s.size()) fail(where, "'" + s + "' is not a note like C4, F#3 or Bb2");
    int octave = 0;
    try {
        size_t end = 0;
        octave = std::stoi(s.substr(n), &end);
        if (n + end != s.size()) throw std::invalid_argument("trailing");
    } catch (const std::exception&) {
        fail(where, "'" + s + "' is not a note like C4, F#3 or Bb2");
    }
    const int midi = (octave + 1) * 12 + pc;
    if (midi < 0 || midi > 127) fail(where, "'" + s + "' is outside the MIDI range (C-1 to G9)");
    return midi;
}

std::string noteName(int midi) { return kNoteNames[midi % 12] + std::to_string(midi / 12 - 1); }

Json keyJson(const Key& k) {
    Json scale;
    for (const auto& s : kScales) {
        if (s.mask == k.scale) scale = s.name;
    }
    if (scale.is_null()) {
        scale = Json::array();
        for (int i = 0; i < 12; ++i) {
            if (k.scale & (1u << i)) scale.push_back(i);
        }
    }
    return Json{{"root", kNoteNames[k.root % 12]}, {"scale", scale}};
}

Key parseKey(const Json& j, const std::string& where) {
    object(j, where);
    Key k;
    const std::string root = string(j, "root", where);
    const int pc = pitchClass(root);
    if (pc < 0 || root.size() > 2) fail(at(where, "root"), "'" + root + "' is not a note name like C, F# or Bb");
    k.root = uint8_t(pc);
    const Json* scale = field(j, "scale");
    if (scale == nullptr) fail(at(where, "scale"), "missing");
    if (scale->is_string()) {
        const std::string name = scale->get<std::string>();
        bool found = false;
        for (const auto& s : kScales) {
            if (name == s.name) {
                k.scale = s.mask;
                found = true;
            }
        }
        if (!found) fail(at(where, "scale"), "unknown scale '" + name + "'; use a name or a list of semitones");
    } else if (scale->is_array()) {
        uint16_t mask = 0;
        for (size_t i = 0; i < scale->size(); ++i) {
            const Json& v = (*scale)[i];
            if (!v.is_number_integer() || v.get<int>() < 0 || v.get<int>() > 11) {
                fail(at(at(where, "scale"), i), "expected a semitone from 0 to 11");
            }
            mask |= uint16_t(1u << v.get<int>());
        }
        if ((mask & 1u) == 0) fail(at(where, "scale"), "must include 0 (the root)");
        k.scale = mask;
    } else {
        fail(at(where, "scale"), "expected a scale name or a list of semitones");
    }
    return k;
}

std::string hex2(uint8_t v) {
    static const char* d = "0123456789ABCDEF";
    return {d[v >> 4], d[v & 15]};
}

int hexDigit(char c) {
    if (c >= '0' && c <= '9') return c - '0';
    c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    return c >= 'A' && c <= 'F' ? c - 'A' + 10 : -1;
}

// ---------------------------------------------------------------- cells

Json cellJson(const Cell& c, uint32_t row, uint32_t column, PitchMode mode) {
    Json j{{"row", row}, {"column", column}};
    if (c.isOff()) {
        j["off"] = true;
    } else if (c.isNote()) {
        if (mode == PitchMode::Degree) {
            j["degree"] = c.note % 12 + 1;
            j["octave"] = c.note / 12;
        } else {
            j["note"] = noteName(c.note);
        }
        if (c.accidental != 0) j["accidental"] = c.accidental;
    }
    if (c.volume != Cell::kVolumeDefault) j["volume"] = c.volume;
    if (c.effect != fx::kNone) {
        j["effect"] = std::string(1, char(c.effect)) + hex2(c.effectParam);
    } else if (c.effectParam != 0) {
        j["effect"] = "." + hex2(c.effectParam);  // a parameter typed before its command
    }
    return j;
}

Cell parseCell(const Json& j, PitchMode mode, const std::string& where) {
    object(j, where);
    Cell c;
    const bool off = boolean(j, "off", where, false);
    const bool hasNote = field(j, "note") != nullptr;
    const bool hasDegree = field(j, "degree") != nullptr;
    if (int(off) + int(hasNote) + int(hasDegree) > 1) fail(where, "use only one of off, note and degree");
    if (off) {
        c.note = Cell::kNoteOff;
    } else if (hasNote) {
        if (mode == PitchMode::Degree) fail(at(where, "note"), "this channel is in degree mode: use degree and octave");
        c.note = uint8_t(parseNote(string(j, "note", where), at(where, "note")));
    } else if (hasDegree) {
        if (mode != PitchMode::Degree) fail(at(where, "degree"), "this channel plays notes, not scale degrees");
        const long degree = integer(j, "degree", where, kRequired, 1, 12);
        const long octave = integer(j, "octave", where, kRequired, 0, 9);
        c.note = uint8_t(octave * 12 + degree - 1);
        if (c.note > 127) fail(where, "degree and octave are too high");
    }
    if (field(j, "accidental") != nullptr) {
        if (!c.isNote()) fail(at(where, "accidental"), "only applies to a note");
        c.accidental = int8_t(integer(j, "accidental", where, 0, -2, 2));
    }
    if (field(j, "volume") != nullptr) c.volume = uint8_t(integer(j, "volume", where, 0, 0, 127));
    if (field(j, "effect") != nullptr) {
        const std::string e = string(j, "effect", where);
        const int hi = e.size() == 3 ? hexDigit(e[1]) : -1;
        const int lo = e.size() == 3 ? hexDigit(e[2]) : -1;
        const int command = e.size() == 3 ? (e[0] == '.' ? 0 : hexDigit(e[0])) : -1;
        if (command < 0 || hi < 0 || lo < 0) {
            fail(at(where, "effect"), "'" + e + "' is not an effect like 024 or F80");
        }
        c.effect = e[0] == '.' ? fx::kNone : fx::fromDigit(command);
        c.effectParam = uint8_t(hi * 16 + lo);
    }
    return c;
}

// ---------------------------------------------------------------- the song

/// Everything referenced by id, filled as the file is read.
struct Ids {
    IdTable channels{"channel", {}};
    IdTable patterns{"pattern", {}};
    IdTable sections{"section", {}};
    IdTable layers{"layer", {}};
    IdTable params{"parameter", {}};
    IdTable events{"event", {}};
    IdTable instruments{"instrument", {}};
};

void readInstruments(const Json& root, Song& song, SongInfo& info, Ids& ids) {
    const Json& list = array(root, "instruments", "");
    checkCapacity(list.size(), config::kMaxInstruments, "instruments", "instruments");
    for (size_t i = 0; i < list.size(); ++i) {
        const std::string where = at("instruments", i);
        const Json& j = object(list[i], where);
        info.instruments.push_back(label(j, where));
        ids.instruments.add(info.instruments.back().id, uint8_t(i), where);
        if (field(j, "voice") != nullptr) {
            const std::string v = string(j, "voice", where);
            bool known = false;
            for (const char* name : kVoices) known = known || v == name;
            if (!known) {
                fail(at(where, "voice"), "'" + v + "' is a voice this version doesn't have; the song needs a newer version");
            }
        }
        Instrument inst;  // parameters left out keep the synth's defaults
        if (const Json* params = field(j, "params")) {
            object(*params, at(where, "params"));
            for (uint8_t p = 0; p < Instrument::kNumParams; ++p) {
                const auto param = static_cast<Instrument::Param>(p);
                const auto range = Instrument::range(param);
                const double v = number(*params, Instrument::key(param), at(where, "params"), inst.get(param),
                                        range.min, range.max);
                inst.set(param, float(v));
            }
        }
        const uint8_t index = song.addInstrument(inst);
        if (field(j, "pitch") != nullptr) song.instrumentPitch[index] = uint8_t(choice(j, "pitch", where, kPitchModes));
    }
}

void readChannels(const Json& root, Song& song, SongInfo& info, Ids& ids) {
    const Json& list = array(root, "channels", "");
    checkCapacity(list.size(), config::kMaxChannels, "channels", "channels");
    // Ids first, so the patterns' order doesn't matter.
    for (size_t ch = 0; ch < list.size(); ++ch) {
        const std::string where = at("channels", ch);
        info.channels.push_back(label(object(list[ch], where), where));
        ids.channels.add(info.channels.back().id, uint8_t(ch), where);
    }
    for (size_t ch = 0; ch < list.size(); ++ch) {
        const std::string where = at("channels", ch);
        const Json& j = list[ch];
        const Json& columns = array(j, "columns", where);
        if (columns.empty()) fail(at(where, "columns"), "a channel needs at least one column");
        checkCapacity(columns.size(), config::kMaxColumns, at(where, "columns"), "columns");
        song.addChannel(PitchMode::Absolute, uint8_t(columns.size()));
        ChannelSetup& setup = song.channels[ch];
        setup.volume = float(number(j, "volume", where, 1, 0, 1));
        for (size_t c = 0; c < columns.size(); ++c) {
            const std::string cw = at(at(where, "columns"), c);
            const Json& col = object(columns[c], cw);
            setup.instruments[c] = field(col, "instrument") != nullptr ? ids.instruments.ref(col, "instrument", cw) : kNoIndex;
            setup.pitchModes[c] = static_cast<PitchMode>(choice(col, "pitch", cw, kPitchModes));
            if (const Json* when = field(col, "playsWhen")) {
                const std::string ww = at(cw, "playsWhen");
                object(*when, ww);
                setup.gateParams[c] = ids.params.ref(*when, "param", ww);
                setup.gateThresholds[c] = float(number(*when, "atLeast", ww, kRequired, 0, 1));
            }
            if (field(col, "layers") != nullptr) {
                const Json& layers = array(col, "layers", cw);
                for (size_t k = 0; k < layers.size(); ++k) {
                    const std::string lw = at(at(cw, "layers"), k);
                    if (!layers[k].is_string()) fail(lw, "expected a layer id");
                    song.layers[ids.layers.find(layers[k].get<std::string>(), lw)] |= layerBit(uint32_t(ch), uint32_t(c));
                }
            }
        }

        const Json& patterns = array(j, "patterns", where);
        for (size_t p = 0; p < patterns.size(); ++p) {
            const std::string pw = at(at(where, "patterns"), p);
            const Json& pj = object(patterns[p], pw);
            if (song.numPatterns >= config::kMaxPatterns) {
                fail(pw, "too many patterns (the most is " + std::to_string(config::kMaxPatterns) + ")");
            }
            info.patterns.push_back(label(pj, pw));
            const uint8_t index = song.addPattern(uint8_t(ch),
                                                  uint16_t(integer(pj, "rows", pw, kRequired, 1, config::kMaxRows)),
                                                  uint8_t(integer(pj, "ticksPerRow", pw, kRequired, 1, 255)));
            ids.patterns.add(info.patterns.back().id, index, pw);
            Pattern& pattern = song.patterns[index];
            const Json& cells = array(pj, "cells", pw);
            for (size_t k = 0; k < cells.size(); ++k) {
                const std::string kw = at(at(pw, "cells"), k);
                const Json& cj = object(cells[k], kw);
                const long row = integer(cj, "row", kw, kRequired, 0, pattern.numRows - 1);
                const long column = integer(cj, "column", kw, kRequired, 0, long(config::kMaxColumns) - 1);
                pattern.cells[row][column] = parseCell(cj, setup.pitchModes[column], kw);
            }
        }
    }
}

void readLabels(const Json& root, const char* key, std::vector<Label>& out, IdTable& ids, uint32_t max) {
    const Json& list = array(root, key, "");
    checkCapacity(list.size(), max, key, key);
    for (size_t i = 0; i < list.size(); ++i) {
        const std::string where = at(key, i);
        out.push_back(label(object(list[i], where), where));
        ids.add(out.back().id, uint8_t(i), where);
    }
}

void readSections(const Json& root, Song& song, SongInfo& info, Ids& ids) {
    readLabels(root, "sections", info.sections, ids.sections, config::kMaxSections);
    const Json& list = array(root, "sections", "");
    song.numSections = uint8_t(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const std::string where = at("sections", i);
        const Json& j = list[i];
        Section& s = song.sections[i];
        s.tempoMilliBpm = uint32_t(std::lround(number(j, "tempo", where, 0, 0, 999) * 1000.0));
        s.beatsPerBar = uint8_t(integer(j, "beatsPerBar", where, 4, 1, 32));
        s.bridgeBeats = uint16_t(integer(j, "bridgeBeats", where, 0, 0, 1024));
        const Json* channels = field(j, "channels");
        if (channels == nullptr) continue;
        const std::string cw = at(where, "channels");
        object(*channels, cw);
        for (const auto& [channelId, sequence] : channels->items()) {
            const uint8_t ch = ids.channels.find(channelId, cw);
            const std::string sw = at(cw, channelId);
            if (!sequence.is_array()) fail(sw, "expected a list of pattern ids");
            checkCapacity(sequence.size(), config::kMaxSequenceLength, sw, "patterns in a sequence");
            for (size_t k = 0; k < sequence.size(); ++k) {
                if (!sequence[k].is_string()) fail(at(sw, k), "expected a pattern id");
                const uint8_t p = ids.patterns.find(sequence[k].get<std::string>(), at(sw, k));
                if (song.patterns[p].channel != ch) {
                    fail(at(sw, k), "pattern '" + info.patterns[p].id + "' belongs to channel '" +
                                        info.channels[song.patterns[p].channel].id + "'");
                }
                s.sequences[ch].append(p);
            }
        }
    }
}

const char* const kActions[] = {"gotoSection",    "playStinger",    "setLayerMute",    "setParam",
                                "setKey",         "setTempo",       "stop",            "setChannelMute",
                                "setChannelSolo", "setChannelPause", "restartChannel"};

Command parseAction(const Json& j, const Song& song, const Ids& ids, const std::string& where) {
    object(j, where);
    const auto when = [&](int fallback) { return static_cast<Quantize>(choice(j, "when", where, kQuantize, fallback)); };
    switch (choice(j, "type", where, kActions)) {
    case 0: {
        const uint8_t via = field(j, "via") != nullptr ? ids.sections.ref(j, "via", where) : kNoIndex;
        return Command::gotoSection(ids.sections.ref(j, "section", where), when(2),
                                    static_cast<Entry>(choice(j, "entry", where, kEntries, 0)), via);
    }
    case 1: {
        const uint8_t p = ids.patterns.ref(j, "pattern", where);
        return Command::playStinger(song.patterns[p].channel, p, when(0));
    }
    case 2:
        return Command::setLayerMute(ids.layers.ref(j, "layer", where), boolean(j, "muted", where, true), when(2));
    case 3:
        return Command::setParam(ids.params.ref(j, "param", where), float(number(j, "value", where, kRequired, 0, 1)));
    case 4: {
        const Key k = parseKey(j, where);
        return Command::setKey(k.root, k.scale, when(2));
    }
    case 5:
        return Command::setTempo(float(number(j, "tempo", where, kRequired, 1, 999)), when(1));
    case 7: {
        Command c = Command::setChannelMute(ids.channels.ref(j, "channel", where), boolean(j, "muted", where, true));
        c.when = when(0);
        return c;
    }
    case 8: {
        Command c = Command::setChannelSolo(ids.channels.ref(j, "channel", where), boolean(j, "soloed", where, true));
        c.when = when(0);
        return c;
    }
    case 9:
        return Command::setChannelPause(ids.channels.ref(j, "channel", where), boolean(j, "paused", where, true), when(2));
    case 10:
        return Command::restartChannel(ids.channels.ref(j, "channel", where), when(2));
    default:
        return Command::stop();
    }
}

void readEvents(const Json& root, Song& song, SongInfo& info, Ids& ids) {
    const Json& list = array(root, "events", "");
    song.numEvents = uint8_t(list.size());
    for (size_t i = 0; i < list.size(); ++i) {
        const std::string where = at("events", i);
        const Json& actions = array(list[i], "actions", where);
        checkCapacity(actions.size(), config::kMaxEventActions, at(where, "actions"), "actions");
        for (size_t k = 0; k < actions.size(); ++k) {
            song.events[i].add(parseAction(actions[k], song, ids, at(at(where, "actions"), k)));
        }
    }
    (void)info;
}

void readMappings(const Json& root, Song& song, const Ids& ids) {
    const Json& list = array(root, "mappings", "");
    checkCapacity(list.size(), config::kMaxMappings, "mappings", "mappings");
    for (size_t i = 0; i < list.size(); ++i) {
        const std::string where = at("mappings", i);
        const Json& j = object(list[i], where);
        Mapping m;
        m.param = ids.params.ref(j, "param", where);
        m.target = static_cast<Mapping::Target>(choice(j, "target", where, kTargets) + 1);
        switch (m.target) {
        case Mapping::Target::ChannelGain:
        case Mapping::Target::ChannelCutoff: m.index = ids.channels.ref(j, "channel", where); break;
        case Mapping::Target::LayerGain: m.index = ids.layers.ref(j, "layer", where); break;
        case Mapping::Target::Threshold:
            m.threshold = float(number(j, "threshold", where, kRequired, 0, 1));
            m.index = field(j, "above") != nullptr ? ids.events.ref(j, "above", where) : kNoIndex;
            m.indexBelow = field(j, "below") != nullptr ? ids.events.ref(j, "below", where) : kNoIndex;
            break;
        default: break;
        }
        if (m.target != Mapping::Target::Threshold) {
            m.curve = static_cast<Mapping::Curve>(choice(j, "curve", where, kCurves, 0));
            m.outMin = float(number(j, "from", where, kRequired, -1e6, 1e6));
            m.outMax = float(number(j, "to", where, kRequired, -1e6, 1e6));
            m.rampMs = uint16_t(integer(j, "rampMs", where, 30, 0, 60000));
            if (m.curve == Mapping::Curve::Exponential && !(m.outMin > 0 && m.outMax > 0)) {
                fail(where, "an exponential mapping needs from and to above 0");
            }
        }
        song.addMapping(m);
    }
}

// ---------------------------------------------------------------- writing

std::string idOf(const std::vector<Label>& labels, size_t i, const char* kind) {
    return i < labels.size() ? labels[i].id : std::string(kind) + "-" + std::to_string(i + 1);
}
std::string nameOf(const std::vector<Label>& labels, size_t i, const char* kind) {
    return i < labels.size() ? labels[i].name : std::string(kind) + " " + std::to_string(i + 1);
}
Json labelJson(const std::vector<Label>& labels, size_t i, const char* kind) {
    return Json{{"id", idOf(labels, i, kind)}, {"name", nameOf(labels, i, kind)}};
}

/// Floats as the shortest decimal that reads back the same float.
Json real(float v) {
    char buf[32];
    for (int precision = 6; precision <= 9; ++precision) {
        std::snprintf(buf, sizeof buf, "%.*g", precision, double(v));
        if (std::strtof(buf, nullptr) == v) break;
    }
    return Json::parse(buf);
}

Json actionJson(const Command& c, const Song& song, const SongInfo& info) {
    const auto when = [](Quantize q) { return kQuantize[int(q)]; };
    switch (c.kind) {
    case Command::Kind::GotoSection: {
        Json j{{"type", "gotoSection"}, {"section", idOf(info.sections, c.a, "section")}, {"when", when(c.when)},
               {"entry", kEntries[int(c.entry)]}};
        if (c.b != kNoIndex) j["via"] = idOf(info.sections, c.b, "section");
        return j;
    }
    case Command::Kind::PlayStinger:
        return Json{{"type", "playStinger"}, {"pattern", idOf(info.patterns, c.data, "pattern")}, {"when", when(c.when)}};
    case Command::Kind::SetLayerMute:
        return Json{{"type", "setLayerMute"}, {"layer", idOf(info.layers, c.a, "layer")}, {"muted", c.b != 0},
                    {"when", when(c.when)}};
    case Command::Kind::SetParam:
        return Json{{"type", "setParam"}, {"param", idOf(info.params, c.a, "param")}, {"value", real(c.value)}};
    case Command::Kind::SetKey: {
        Json j{{"type", "setKey"}};
        j.update(keyJson(Key{c.a, c.data}));
        j["when"] = when(c.when);
        return j;
    }
    case Command::Kind::SetTempo:
        return Json{{"type", "setTempo"}, {"tempo", real(c.value)}, {"when", when(c.when)}};
    case Command::Kind::Stop:
        return Json{{"type", "stop"}};
    case Command::Kind::SetChannelMute:
        return Json{{"type", "setChannelMute"}, {"channel", idOf(info.channels, c.a, "channel")}, {"muted", c.b != 0},
                    {"when", when(c.when)}};
    case Command::Kind::SetChannelSolo:
        return Json{{"type", "setChannelSolo"}, {"channel", idOf(info.channels, c.a, "channel")}, {"soloed", c.b != 0},
                    {"when", when(c.when)}};
    case Command::Kind::SetChannelPause:
        return Json{{"type", "setChannelPause"}, {"channel", idOf(info.channels, c.a, "channel")}, {"paused", c.b != 0},
                    {"when", when(c.when)}};
    case Command::Kind::RestartChannel:
        return Json{{"type", "restartChannel"}, {"channel", idOf(info.channels, c.a, "channel")}, {"when", when(c.when)}};
    default:
        (void)song;
        return Json();  // not something an event can do
    }
}

Json mappingJson(const Mapping& m, const SongInfo& info) {
    Json j{{"param", idOf(info.params, m.param, "param")}, {"target", kTargets[int(m.target) - 1]}};
    switch (m.target) {
    case Mapping::Target::ChannelGain:
    case Mapping::Target::ChannelCutoff: j["channel"] = idOf(info.channels, m.index, "channel"); break;
    case Mapping::Target::LayerGain: j["layer"] = idOf(info.layers, m.index, "layer"); break;
    case Mapping::Target::Threshold:
        j["threshold"] = real(m.threshold);
        if (m.index != kNoIndex) j["above"] = idOf(info.events, m.index, "event");
        if (m.indexBelow != kNoIndex) j["below"] = idOf(info.events, m.indexBelow, "event");
        return j;
    default: break;
    }
    j["curve"] = kCurves[int(m.curve)];
    j["from"] = real(m.outMin);
    j["to"] = real(m.outMax);
    j["rampMs"] = m.rampMs;
    return j;
}

/// Pretty JSON, but lists of plain values and small objects of plain values
/// (cells, sequences) stay on one line, so a pattern reads row by row.
void write(const Json& j, std::string& out, int indent) {
    const auto simple = [](const Json& v) { return !v.is_structured(); };
    const bool oneLine = j.is_structured() &&
                         (j.empty() || (std::all_of(j.begin(), j.end(), simple) && (j.is_array() || j.size() <= 8)));
    if (!j.is_structured()) {
        out += j.dump();
        return;
    }
    const bool isObject = j.is_object();
    if (oneLine) {
        out += isObject ? "{" : "[";
        size_t n = 0;
        for (auto it = j.begin(); it != j.end(); ++it, ++n) {
            if (n > 0) out += ", ";
            if (isObject) out += Json(it.key()).dump() + ": ";
            out += it->dump();
        }
        out += isObject ? "}" : "]";
        return;
    }
    const std::string pad(size_t(indent) * 2, ' '), inner(size_t(indent + 1) * 2, ' ');
    out += isObject ? "{\n" : "[\n";
    size_t n = 0;
    for (auto it = j.begin(); it != j.end(); ++it, ++n) {
        out += inner;
        if (isObject) out += Json(it.key()).dump() + ": ";
        write(*it, out, indent + 1);
        out += n + 1 < j.size() ? ",\n" : "\n";
    }
    out += pad + (isObject ? "}" : "]");
}

} // namespace

// ---------------------------------------------------------------- public API

LoadResult load(std::string_view text, Song& song, SongInfo& info) {
    LoadResult result;
    try {
        song = Song();
        info = SongInfo();
        const Json root = Json::parse(text.begin(), text.end());
        object(root, "the file");
        if (string(root, "format", "", "") != kFormat) fail("format", std::string("expected \"") + kFormat + "\"");
        const long version = integer(root, "version", "", kRequired, 1, 1000000);
        if (version > kVersion) {
            fail("version", "this song needs a newer version of the app (file version " + std::to_string(version) +
                                ", app reads " + std::to_string(kVersion) + ")");
        }
        if (version < kVersion) {
            fail("version", "this song was saved by an older version of the app, whose files this one doesn't read (file version " +
                                std::to_string(version) + ", app reads " + std::to_string(kVersion) + ")");
        }
        info.title = string(root, "title", "", "");
        song.tempoMilliBpm = uint32_t(std::lround(number(root, "tempo", "", 120, 1, 999) * 1000.0));
        if (const Json* key = field(root, "key")) song.homeKey = parseKey(*key, "key");

        Ids ids;
        readInstruments(root, song, info, ids);
        // Parameters before channels: columns can be gated on them.
        readLabels(root, "params", info.params, ids.params, config::kMaxParams);
        song.numParams = uint8_t(info.params.size());
        for (size_t i = 0; i < info.params.size(); ++i) {
            song.paramInitial[i] = float(number(array(root, "params", "")[i], "initial", at("params", i), 0, 0, 1));
        }
        // Layers before channels too: columns name the layers they are in.
        readLabels(root, "layers", info.layers, ids.layers, config::kMaxLayers);
        song.numLayers = uint8_t(info.layers.size());
        readChannels(root, song, info, ids);
        readLabels(root, "events", info.events, ids.events, config::kMaxEvents);
        readSections(root, song, info, ids);
        readEvents(root, song, info, ids);
        readMappings(root, song, ids);
        if (field(root, "start") != nullptr) {
            song.startSection = ids.sections.ref(root, "start", "");
        } else if (song.numSections == 0) {
            song.startSection = 0;
        }
        result.ok = true;
    } catch (const FormatError& e) {
        result.error = e.what();
    } catch (const nlohmann::json::exception& e) {
        result.error = e.what();
    }
    return result;
}

std::string save(const Song& song, const SongInfo& info) {
    Json root;
    root["format"] = kFormat;
    root["version"] = kVersion;
    root["title"] = info.title;
    root["tempo"] = real(float(song.tempoMilliBpm) / 1000.0f);
    root["key"] = keyJson(song.homeKey);
    if (song.startSection < song.numSections) root["start"] = idOf(info.sections, song.startSection, "section");

    Json& instruments = root["instruments"] = Json::array();
    for (uint8_t i = 0; i < song.numInstruments; ++i) {
        Json j = labelJson(info.instruments, i, "instrument");
        j["voice"] = kVoices[0];
        if (song.instrumentPitch[i] <= uint8_t(PitchMode::Degree)) j["pitch"] = kPitchModes[song.instrumentPitch[i]];
        Json params;
        for (uint8_t p = 0; p < Instrument::kNumParams; ++p) {
            const auto param = static_cast<Instrument::Param>(p);
            params[Instrument::key(param)] = real(song.instruments[i].get(param));
        }
        j["params"] = params;
        instruments.push_back(j);
    }

    Json& channels = root["channels"] = Json::array();
    for (uint8_t ch = 0; ch < song.numChannels; ++ch) {
        const ChannelSetup& setup = song.channels[ch];
        Json j = labelJson(info.channels, ch, "channel");
        j["volume"] = real(setup.volume);
        Json& columns = j["columns"] = Json::array();
        for (uint8_t c = 0; c < setup.numColumns; ++c) {
            Json col = Json::object();
            if (setup.instruments[c] < song.numInstruments) {
                col["instrument"] = idOf(info.instruments, setup.instruments[c], "instrument");
            }
            col["pitch"] = kPitchModes[int(setup.pitchModes[c])];
            if (setup.gateParams[c] < song.numParams) {
                col["playsWhen"] = Json{{"param", idOf(info.params, setup.gateParams[c], "param")},
                                        {"atLeast", real(setup.gateThresholds[c])}};
            }
            Json inLayers = Json::array();
            for (uint8_t l = 0; l < song.numLayers; ++l) {
                if (song.layers[l] & layerBit(ch, c)) inLayers.push_back(idOf(info.layers, l, "layer"));
            }
            if (!inLayers.empty()) col["layers"] = inLayers;
            columns.push_back(col);
        }
        Json& patterns = j["patterns"] = Json::array();
        for (uint8_t p = 0; p < song.numPatterns; ++p) {
            const Pattern& pattern = song.patterns[p];
            if (pattern.channel != ch) continue;
            Json pj = labelJson(info.patterns, p, "pattern");
            pj["rows"] = pattern.numRows;
            pj["ticksPerRow"] = pattern.ticksPerRow;
            Json& cells = pj["cells"] = Json::array();
            // All of the pattern's columns are kept, including ones the
            // channel doesn't play now, so removing a column loses nothing.
            for (uint32_t r = 0; r < pattern.numRows; ++r) {
                for (uint32_t c = 0; c < config::kMaxColumns; ++c) {
                    const Cell& cell = pattern.cells[r][c];
                    if (cell.note == Cell::kNoteEmpty && cell.volume == Cell::kVolumeDefault &&
                        cell.effect == fx::kNone && cell.effectParam == 0) {
                        continue;
                    }
                    cells.push_back(cellJson(cell, r, c, setup.pitchModes[c]));
                }
            }
            patterns.push_back(pj);
        }
        channels.push_back(j);
    }

    Json& params = root["params"] = Json::array();
    for (uint8_t i = 0; i < song.numParams; ++i) {
        Json j = labelJson(info.params, i, "param");
        j["initial"] = real(song.paramInitial[i]);
        params.push_back(j);
    }

    Json& sections = root["sections"] = Json::array();
    for (uint8_t i = 0; i < song.numSections; ++i) {
        const Section& s = song.sections[i];
        Json j = labelJson(info.sections, i, "section");
        if (s.tempoMilliBpm != 0) j["tempo"] = real(float(s.tempoMilliBpm) / 1000.0f);
        j["beatsPerBar"] = s.beatsPerBar;
        if (s.bridgeBeats != 0) j["bridgeBeats"] = s.bridgeBeats;
        Json play = Json::object();
        for (uint8_t ch = 0; ch < song.numChannels; ++ch) {
            const Sequence& seq = s.sequences[ch];
            if (seq.length == 0) continue;
            Json list = Json::array();
            for (uint8_t k = 0; k < seq.length; ++k) list.push_back(idOf(info.patterns, seq.entries[k], "pattern"));
            play[idOf(info.channels, ch, "channel")] = list;
        }
        j["channels"] = play;
        sections.push_back(j);
    }

    Json& layers = root["layers"] = Json::array();
    for (uint8_t i = 0; i < song.numLayers; ++i) {
        Json j = labelJson(info.layers, i, "layer");
        layers.push_back(j);
    }

    Json& events = root["events"] = Json::array();
    for (uint8_t i = 0; i < song.numEvents; ++i) {
        Json j = labelJson(info.events, i, "event");
        Json actions = Json::array();
        for (uint8_t k = 0; k < song.events[i].numActions; ++k) {
            Json a = actionJson(song.events[i].actions[k], song, info);
            if (!a.is_null()) actions.push_back(a);
        }
        j["actions"] = actions;
        events.push_back(j);
    }

    Json& mappings = root["mappings"] = Json::array();
    for (uint8_t i = 0; i < song.numMappings; ++i) {
        if (song.mappings[i].target != Mapping::Target::None) mappings.push_back(mappingJson(song.mappings[i], info));
    }

    std::string out;
    write(root, out, 0);
    out += "\n";
    return out;
}

std::string makeId(std::string_view name, const std::vector<Label>& taken) {
    std::string base;
    for (char c : name) {
        if (std::isalnum(static_cast<unsigned char>(c))) {
            base += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        } else if (!base.empty() && base.back() != '-') {
            base += '-';
        }
    }
    while (!base.empty() && base.back() == '-') base.pop_back();
    if (base.empty()) base = "item";
    const auto used = [&](const std::string& id) {
        for (const auto& l : taken) {
            if (l.id == id) return true;
        }
        return false;
    };
    if (!used(base)) return base;
    for (int n = 2;; ++n) {
        const std::string id = base + "-" + std::to_string(n);
        if (!used(id)) return id;
    }
}

} // namespace adaptive::songfile
