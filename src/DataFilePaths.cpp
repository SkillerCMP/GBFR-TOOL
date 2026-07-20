#include "DataFilePaths.hpp"

#include <system_error>

namespace gdtv {

std::filesystem::path locateBundledDataFile(
    const std::filesystem::path& baseDirectory,
    const std::filesystem::path& fileName) {
    const auto nested = baseDirectory / kHashFolderName / fileName;
    std::error_code error;
    if (std::filesystem::is_regular_file(nested, error)) return nested;
    return baseDirectory / fileName;
}

std::filesystem::path preferredBundledDataFile(
    const std::filesystem::path& baseDirectory,
    const std::filesystem::path& fileName) {
    const auto nestedDirectory = baseDirectory / kHashFolderName;
    std::error_code error;
    if (std::filesystem::is_directory(nestedDirectory, error)) {
        return nestedDirectory / fileName;
    }
    return baseDirectory / fileName;
}

} // namespace gdtv
