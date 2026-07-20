#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gdtv {

struct SectionNameEntry {
    std::uint32_t key{};
    std::string locator;
    std::string name;
    std::string subsystem;
    std::string confidence;
    std::string officialSaveIdType;
    std::string storageType;
    std::string hashCategory;
    std::string internalPrefix;
    std::string note;
    std::string recommendedTest;
};

class SectionNameMap {
public:
    std::size_t load(const std::filesystem::path& path, bool clearExisting = true);
    [[nodiscard]] const SectionNameEntry* find(std::uint32_t key) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::filesystem::path& sourcePath() const noexcept { return sourcePath_; }
    [[nodiscard]] std::vector<std::string> subsystems() const;

private:
    std::unordered_map<std::uint32_t, SectionNameEntry> entries_;
    std::filesystem::path sourcePath_;
};

[[nodiscard]] std::string confidenceMarker(std::string_view confidence);

} // namespace gdtv
