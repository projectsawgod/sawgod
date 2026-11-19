// SawGod engine: version string and native display constants.
#pragma once

namespace sawgod {

// Semantic-ish version for the engine; bumped by hand until releases exist.
extern const char *const kVersionString;
// This build's identity: the version label plus the source revision ("0.7.3+ff65f0577754").
extern const char *const kBuildId;

// WonderSwan Color native resolution. The port renders at this size and
// scales up with integer nearest-neighbour scaling.
inline constexpr int kNativeWidth = 224;
inline constexpr int kNativeHeight = 144;

} // namespace sawgod
