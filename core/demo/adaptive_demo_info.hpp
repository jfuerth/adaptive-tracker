#pragma once

#include <adaptive/songfile.hpp>
#include <demo/adaptive_demo.hpp>

#include <iterator>

namespace adaptive::demo {

/// The adaptive demo's names, with ids made from them, for its song file.
inline songfile::SongInfo adaptiveDemoInfo() {
    using D = AdaptiveDemo;
    songfile::SongInfo info;
    info.title = D::kTitle;
    auto fill = [](std::vector<songfile::Label>& out, const char* const* names, size_t n) {
        for (size_t i = 0; i < n; ++i) out.push_back({songfile::makeId(names[i], out), names[i]});
    };
    fill(info.channels, D::kChannelNames, std::size(D::kChannelNames));
    fill(info.patterns, D::kPatternNames, std::size(D::kPatternNames));
    fill(info.sections, D::kSectionNames, std::size(D::kSectionNames));
    fill(info.layers, D::kLayerNames, std::size(D::kLayerNames));
    fill(info.params, D::kParamNames, std::size(D::kParamNames));
    fill(info.events, D::kEventNames, std::size(D::kEventNames));
    fill(info.instruments, D::kInstrumentNames, std::size(D::kInstrumentNames));
    return info;
}

} // namespace adaptive::demo
