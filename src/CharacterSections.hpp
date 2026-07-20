#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace gdtv {

struct CharacterSectionEntry {
    std::uint32_t group{};
    std::string inGameName;
    std::string internalName;
    std::string note;
};

class CharacterSectionMap {
public:
    std::size_t load(const std::filesystem::path& path, bool clearExisting = true);
    [[nodiscard]] const CharacterSectionEntry* find(std::uint32_t group) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return entries_.size(); }
    [[nodiscard]] const std::filesystem::path& sourcePath() const noexcept { return sourcePath_; }

private:
    std::unordered_map<std::uint32_t, CharacterSectionEntry> entries_;
    std::filesystem::path sourcePath_;
};

} // namespace gdtv
