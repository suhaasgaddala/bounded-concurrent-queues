#pragma once

#define ORBITQUEUE_VERSION_MAJOR 0
#define ORBITQUEUE_VERSION_MINOR 1
#define ORBITQUEUE_VERSION_PATCH 1

namespace orbitqueue {

inline constexpr int version_major = ORBITQUEUE_VERSION_MAJOR;
inline constexpr int version_minor = ORBITQUEUE_VERSION_MINOR;
inline constexpr int version_patch = ORBITQUEUE_VERSION_PATCH;
inline constexpr const char* version_string = "0.1.1";

} // namespace orbitqueue
