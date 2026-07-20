#pragma once

#include <filesystem>

namespace gdtv {

inline constexpr const char* kHashFolderName = "Hashfolder";

// For reading bundled data: prefer Hashfolder when that exact file exists,
// otherwise fall back to the executable/source root for legacy layouts.
std::filesystem::path locateBundledDataFile(
    const std::filesystem::path& baseDirectory,
    const std::filesystem::path& fileName);

// For creating a bundled data file: use Hashfolder when the directory exists,
// otherwise retain the legacy executable/source-root location.
std::filesystem::path preferredBundledDataFile(
    const std::filesystem::path& baseDirectory,
    const std::filesystem::path& fileName);

} // namespace gdtv
