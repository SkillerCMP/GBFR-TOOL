#pragma once

#include "GameDataCore.hpp"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace gdtv {

class HashDatabase;

enum class LogicalValueKind {
    Unsigned,
    Signed,
    Hash,
    State,
    Bitfield
};


enum class LogicalGroupingKind {
    Flat,
    MasteryCharacter,
    OverMasteryCharacter,
    CurrentTraitsCharacter,
    CurioSlotEntries
};

enum class LogicalFieldUnitScope {
    Entry,
    CurioSlot,
    SigilTrait1,
    SigilTrait2,
    WrightstoneTrait1,
    WrightstoneTrait2,
    WrightstoneTrait3
};

struct LogicalUnitAddress {
    bool valid{};
    bool shared{};
    bool characterScoped{};
    std::uint32_t characterGroup{};
    std::uint32_t nameSpace{};
    std::uint32_t position{};
    std::uint32_t slot{};
};

struct LogicalFieldDefinition {
    std::uint32_t key{};
    std::uint32_t elementIndex{};
    std::string_view locator;
    std::string_view label;
    std::string_view confidence;
    LogicalValueKind kind{LogicalValueKind::Unsigned};
    bool optional{};
    // Optional per-field Hash List filter. When blank, the family's legacy
    // category filter remains the fallback.
    std::string_view hashCategoryFilter;
    bool hashCategoryPrefix{};
    LogicalFieldUnitScope unitScope{LogicalFieldUnitScope::Entry};

    constexpr LogicalFieldDefinition(std::uint32_t fieldKey,
                                     std::uint32_t fieldElementIndex,
                                     std::string_view fieldLocator,
                                     std::string_view fieldLabel,
                                     std::string_view fieldConfidence,
                                     LogicalValueKind fieldKind,
                                     bool fieldOptional,
                                     std::string_view fieldHashCategoryFilter = {},
                                     bool fieldHashCategoryPrefix = false,
                                     LogicalFieldUnitScope fieldUnitScope =
                                         LogicalFieldUnitScope::Entry) noexcept
        : key(fieldKey),
          elementIndex(fieldElementIndex),
          locator(fieldLocator),
          label(fieldLabel),
          confidence(fieldConfidence),
          kind(fieldKind),
          optional(fieldOptional),
          hashCategoryFilter(fieldHashCategoryFilter),
          hashCategoryPrefix(fieldHashCategoryPrefix),
          unitScope(fieldUnitScope) {}
};

struct LogicalFamilyDefinition {
    std::string_view name;
    std::string_view slotLabel;
    std::uint32_t anchorKey{};
    const LogicalFieldDefinition* fields{};
    std::size_t fieldCount{};
    std::string_view hashCategoryFilter;
    bool hashCategoryPrefix{};
    LogicalGroupingKind grouping{LogicalGroupingKind::Flat};
};

struct SpecialCurrencyDefinition {
    std::uint32_t itemHash{};
    std::uint32_t balanceKey{};
    std::string_view name;
    std::string_view abbreviation;
};

struct CurioHashChoice {
    std::uint32_t hash{};
    std::string_view label;
};

struct CurioSlotNormalizationResult {
    bool empty{};
    bool newlyActivated{};
    bool rewardActivated{};
    std::uint32_t counter{};
    std::uint32_t tier{};
};

[[nodiscard]] const LogicalFamilyDefinition& summonInventoryFamily() noexcept;
[[nodiscard]] const LogicalFamilyDefinition& masteryTreeFamily() noexcept;
[[nodiscard]] const LogicalFamilyDefinition& currentTraitsFamily() noexcept;
[[nodiscard]] const LogicalFamilyDefinition& overMasteryFamily() noexcept;
[[nodiscard]] const LogicalFamilyDefinition& itemsFamily() noexcept;
[[nodiscard]] const LogicalFamilyDefinition& weaponsFamily() noexcept;
[[nodiscard]] const LogicalFamilyDefinition& wrightstonesFamily() noexcept;
[[nodiscard]] const LogicalFamilyDefinition& currentSigilsFamily() noexcept;
[[nodiscard]] const LogicalFamilyDefinition& acquiredSigilsFamily() noexcept;
[[nodiscard]] const LogicalFamilyDefinition& curiosFamily() noexcept;
[[nodiscard]] const LogicalFamilyDefinition& quickValuesFamily() noexcept;
[[nodiscard]] const SpecialCurrencyDefinition* specialCurrencyForItemHash(
    std::uint32_t itemHash) noexcept;
[[nodiscard]] const SpecialCurrencyDefinition* specialCurrencyForItemEntry(
    const SaveData& save, std::uint32_t unitId) noexcept;
[[nodiscard]] bool isCurioItemHash(std::uint32_t itemHash) noexcept;
[[nodiscard]] bool isProtectedBulkItemHash(
    std::uint32_t itemHash) noexcept;
[[nodiscard]] const std::array<CurioHashChoice, 5>&
curioHashChoices() noexcept;
[[nodiscard]] std::uint32_t curioTierNumberForHash(
    std::uint32_t curioHash) noexcept;
[[nodiscard]] std::uint32_t curioSlotTierNumber(
    const SaveData& save, std::uint32_t slotIndex) noexcept;
[[nodiscard]] bool curioRewardEntryFilled(
    const SaveData& save, std::uint32_t rewardUnitId) noexcept;
[[nodiscard]] bool curioSlotHasRewards(
    const SaveData& save, std::uint32_t slotIndex) noexcept;
// Returns one more than the nearest earlier occupied Curio slot counter.
// If no earlier occupied slot has a usable counter, the sequence starts at 1.
[[nodiscard]] std::uint32_t suggestedCurioSlotCounter(
    const SaveData& save, std::uint32_t slotIndex) noexcept;
// Applies the Curio slot consistency rules after one reward entry is edited.
// Empty slots use counter 0. A newly populated slot continues the nearest
// earlier active counter, defaults an invalid/empty tier to T1, and activates
// the inserted reward entry when its activation value is still zero.
[[nodiscard]] CurioSlotNormalizationResult normalizeCurioSlotAfterRewardEdit(
    SaveData& save, std::uint32_t slotIndex, std::uint32_t rewardUnitId,
    bool slotWasEmpty);

// Families presented through the shared Logical Save Records tree/editor path.
// Mastery Tree keeps its established dedicated editor and is therefore not in
// this list even though logicalFamilyForAnchor() can still resolve it.
[[nodiscard]] const std::array<const LogicalFamilyDefinition*, 9>&
sharedLogicalFamilies() noexcept;

[[nodiscard]] const LogicalFamilyDefinition* logicalFamilyForAnchor(std::uint32_t anchorKey) noexcept;
[[nodiscard]] bool familyAvailable(const SaveData& save,
                                   const LogicalFamilyDefinition& family) noexcept;
[[nodiscard]] LogicalUnitAddress decodeLogicalUnitId(const LogicalFamilyDefinition& family,
                                                       std::uint32_t unitId) noexcept;
[[nodiscard]] std::uint32_t logicalFieldRecordUnitId(
    const LogicalFieldDefinition& field,
    std::uint32_t logicalUnitId) noexcept;
[[nodiscard]] bool logicalFieldAvailable(const SaveData& save,
                                         const LogicalFieldDefinition& field,
                                         std::uint32_t unitId) noexcept;
// Returns an in-game name when available, then an internal ID, and finally
// 0xXXXXXXXX for unresolved non-empty hashes. Zero, FFFFFFFF, and the
// Global Empty Slot sentinel return empty.
[[nodiscard]] std::string logicalHashDisplayName(
    const HashDatabase& hashDatabase,
    std::uint32_t hash);
[[nodiscard]] std::string logicalHashFieldDisplayName(
    const SaveData& save,
    const HashDatabase& hashDatabase,
    const LogicalFieldDefinition& field,
    std::uint32_t unitId);
[[nodiscard]] std::vector<std::string> curioSlotRewardDisplayNames(
    const SaveData& save,
    const HashDatabase& hashDatabase,
    std::uint32_t slotIndex);

} // namespace gdtv
