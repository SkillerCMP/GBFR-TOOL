#pragma once

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gdtv {

inline constexpr std::uint32_t kGlobalEmptySlotHash = 0x887AE0B0U;
inline constexpr std::string_view kGlobalEmptySlotName = "Global Empty Slot";

[[nodiscard]] constexpr bool isGlobalEmptySlotHash(std::uint32_t value) noexcept {
    return value == kGlobalEmptySlotHash;
}

struct HashEntry {
    std::uint32_t hash{};
    std::string id;
    std::string displayName;
    std::string category;
    std::string version;
    std::string source;
    std::string notes;
    bool userDefined{};
    bool algorithmVerified{};
    bool builtInFriendly{};
};

[[nodiscard]] std::uint32_t xxHash32Custom(std::string_view text) noexcept;
[[nodiscard]] std::string hashHex(std::uint32_t value);
[[nodiscard]] std::string hashRawLittleEndian(std::uint32_t value);
[[nodiscard]] std::optional<std::uint32_t> parseHashValue(std::string_view text,
                                                         bool rawLittleEndian = false) noexcept;

class HashDatabase {
public:
    HashDatabase();

    // Unified text format:
    // [HASH[BE]<TAB>HASH[LE]<TAB>TYPE<TAB>INGAMENAME<TAB>INTERNALNAME<TAB>VERSION<TAB>NOTE]
    // Older pipe-delimited rows are accepted when loading and are converted to TAB on save.
    std::size_t loadDatabase(const std::filesystem::path& path, bool clearExisting = true);
    void saveDatabase(const std::filesystem::path& path) const;

    void addOrUpdateUser(HashEntry entry);
    [[nodiscard]] const std::vector<HashEntry>* find(std::uint32_t hash) const noexcept;
    [[nodiscard]] const HashEntry* preferred(std::uint32_t hash) const noexcept;
    [[nodiscard]] const HashEntry* preferredMatching(std::uint32_t hash,
                                                     std::string_view category,
                                                     std::string_view internalPrefix) const noexcept;
    [[nodiscard]] bool hasMatchingEntry(std::uint32_t hash,
                                        std::string_view category,
                                        std::string_view internalPrefix) const noexcept;
    [[nodiscard]] std::vector<HashEntry> friendlyEntries() const;
    [[nodiscard]] std::vector<HashEntry> userEntries() const;
    [[nodiscard]] std::vector<HashEntry> allEntries() const;
    [[nodiscard]] std::vector<std::uint32_t> hashesForText(std::string_view text) const;
    [[nodiscard]] std::vector<std::string> idsForHash(std::uint32_t hash) const;
    [[nodiscard]] std::vector<std::string> idsForHashMatching(std::uint32_t hash,
                                                              std::string_view category,
                                                              std::string_view internalPrefix) const;

    [[nodiscard]] std::size_t uniqueHashCount() const noexcept { return entriesByHash_.size(); }
    [[nodiscard]] std::size_t databaseEntryCount() const noexcept { return databaseEntryCount_; }
    [[nodiscard]] std::size_t baseEntryCount() const noexcept { return baseEntryCount_; }
    [[nodiscard]] std::size_t friendlyEntryCount() const noexcept { return friendlyEntryCount_; }
    [[nodiscard]] std::size_t userEntryCount() const noexcept { return userEntryCount_; }
    [[nodiscard]] std::size_t verifiedBaseCount() const noexcept { return verifiedBaseCount_; }
    [[nodiscard]] std::size_t baseMismatchCount() const noexcept { return baseMismatchCount_; }
    [[nodiscard]] std::size_t verifiedFriendlyCount() const noexcept { return verifiedFriendlyCount_; }
    [[nodiscard]] std::size_t friendlyMismatchCount() const noexcept { return friendlyMismatchCount_; }
    [[nodiscard]] std::size_t endianMismatchCount() const noexcept { return endianMismatchCount_; }
    [[nodiscard]] std::size_t invalidLineCount() const noexcept { return invalidLineCount_; }
    [[nodiscard]] const std::filesystem::path& databasePath() const noexcept { return databasePath_; }

private:
    std::unordered_map<std::uint32_t, std::vector<HashEntry>> entriesByHash_;
    std::filesystem::path databasePath_;
    std::size_t databaseEntryCount_{};
    std::size_t baseEntryCount_{};
    std::size_t friendlyEntryCount_{};
    std::size_t userEntryCount_{};
    std::size_t verifiedBaseCount_{};
    std::size_t baseMismatchCount_{};
    std::size_t verifiedFriendlyCount_{};
    std::size_t friendlyMismatchCount_{};
    std::size_t endianMismatchCount_{};
    std::size_t invalidLineCount_{};

    void clear();
    void installBuiltInEntries();
    void recount();
};

} // namespace gdtv
