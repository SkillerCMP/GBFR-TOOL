#include "LogicalFamilies.hpp"

#include "HashDatabase.hpp"

#include <algorithm>
#include <set>
#include <utility>

namespace gdtv {
namespace {

constexpr std::array<LogicalFieldDefinition, 7> kSummonFields{{
    {0x05B0U, 0U, "FFB005", "Unique instance number", "Likely", LogicalValueKind::Unsigned, false},
    {0x05B1U, 0U, "FFB105", "Summon ID", "Confirmed", LogicalValueKind::Hash, false},
    {0x05B2U, 0U, "FFB205[0]", "Active Trait ID (Left Icon)", "Confirmed", LogicalValueKind::Hash, false},
    {0x05B2U, 1U, "FFB205[1]", "Active Equip Bonus ID (Right Icon)", "Confirmed", LogicalValueKind::Hash, false},
    {0x05B3U, 0U, "FFB305[0]", "Active Trait Level", "Unconfirmed", LogicalValueKind::Signed, false},
    {0x05B3U, 1U, "FFB305[1]", "Active Equip Bonus Level", "Unconfirmed", LogicalValueKind::Signed, false},
    {0x05B4U, 0U, "FFB405", "Occupied/state bitfield", "Strong", LogicalValueKind::State, false},
}};

constexpr LogicalFamilyDefinition kSummonFamily{
    "Summon Inventory", "Summon Inventory Slot", 0x05B1U,
    kSummonFields.data(), kSummonFields.size(), {}, false, LogicalGroupingKind::Flat
};

// 0x0641 and 0x0642 have matching UnitID sets and together form the current
// mastery-tree save structure. Older saves also contain 0x0645 with the same
// UnitID set. Its values overlap the 0x0642 state but are not identical for all
// records, so it remains an explicitly labelled optional legacy companion.
constexpr std::array<LogicalFieldDefinition, 3> kMasteryTreeFields{{
    {0x0641U, 0U, "FF4106", "Mastery Tree ID", "Confirmed", LogicalValueKind::Hash, false},
    {0x0642U, 0U, "FF4206", "Mastery Tree Activated / State Bitfield", "Confirmed", LogicalValueKind::Bitfield, false},
    {0x0645U, 0U, "FF4506", "Legacy Mastery Tree State Companion", "Strong", LogicalValueKind::Bitfield, true},
}};

constexpr LogicalFamilyDefinition kMasteryTreeFamily{
    "Mastery Tree", "Mastery Tree Entry", 0x0641U,
    kMasteryTreeFields.data(), kMasteryTreeFields.size(), "Masteries", true,
    LogicalGroupingKind::MasteryCharacter
};

// SaveFiletest confirms 0x06A5 and 0x06A6 contain the same 25,968 UnitIDs.
// The ID is a UInt hash while the companion level is a signed Int.
constexpr std::array<LogicalFieldDefinition, 2> kCurrentTraitFields{{
    {0x06A5U, 0U, "FFA506", "Current Trait ID", "Confirmed", LogicalValueKind::Hash, false},
    {0x06A6U, 0U, "FFA606", "Current Trait Level", "Confirmed", LogicalValueKind::Signed, false},
}};

constexpr LogicalFamilyDefinition kCurrentTraitsFamily{
    "Current Traits", "Current Trait Entry", 0x06A5U,
    kCurrentTraitFields.data(), kCurrentTraitFields.size(), "Trait", false,
    LogicalGroupingKind::CurrentTraitsCharacter
};

// These sections deliberately remain separate from Mastery Tree. SaveFiletest
// confirms the pair has 164 matching UnitIDs with four records per character.
constexpr std::array<LogicalFieldDefinition, 2> kOverMasteryFields{{
    {0x0646U, 0U, "FF4606", "OverMastery ID", "Confirmed", LogicalValueKind::Hash, false},
    {0x0647U, 0U, "FF4706", "OverMastery Level / State", "Confirmed", LogicalValueKind::Bitfield, false},
}};

constexpr LogicalFamilyDefinition kOverMasteryFamily{
    "OverMastery", "OverMastery Entry", 0x0646U,
    kOverMasteryFields.data(), kOverMasteryFields.size(), "Masteries - Over", false,
    LogicalGroupingKind::OverMasteryCharacter
};

// SaveFiletest confirms all five item arrays contain UnitIDs 0..499. Unknown
// fields remain conservatively named until their semantics are verified.
constexpr std::array<LogicalFieldDefinition, 5> kItemFields{{
    {0x0709U, 0U, "FF0907", "Item ID", "Confirmed", LogicalValueKind::Hash, false},
    {0x070AU, 0U, "FF0A07", "Item Count", "Confirmed", LogicalValueKind::Signed, false},
    {0x070BU, 0U, "FF0B07", "Item Flags", "Confirmed structure / unknown flags", LogicalValueKind::Bitfield, false},
    {0x070CU, 0U, "FF0C07", "Unknown Item Field", "Confirmed structure / unknown meaning", LogicalValueKind::Bitfield, false},
    {0x070FU, 0U, "FF0F07", "Unknown Item Field", "Confirmed structure / unknown meaning", LogicalValueKind::Bitfield, false},
}};

constexpr LogicalFamilyDefinition kItemsFamily{
    "Items", "Item Entry", 0x0709U,
    kItemFields.data(), kItemFields.size(), {}, false, LogicalGroupingKind::Flat
};


// Weapon inventory arrays share UnitIDs 40000-40255. Only the fields whose
// meanings are Confirmed or Strong are exposed by the simple editor; unknown
// state/skin/flag companions remain untouched in the raw tree.
constexpr std::array<LogicalFieldDefinition, 5> kWeaponFields{{
    {0x0AF3U, 0U, "FFF30A", "Weapon ID", "Confirmed", LogicalValueKind::Hash, false,
     "Weapons", false},
    {0x0AF4U, 0U, "FFF40A", "Weapon Experience", "Confirmed", LogicalValueKind::Unsigned, false},
    {0x0AF5U, 0U, "FFF50A", "Weapon Level Cap", "Strong", LogicalValueKind::Signed, false},
    {0x0AF6U, 0U, "FFF60A", "Mirage Munition Plus Marks", "Strong", LogicalValueKind::Signed, false},
    {0x0B00U, 0U, "FF000B", "Wrightstone Item ID", "Confirmed", LogicalValueKind::Hash, false},
}};

constexpr LogicalFamilyDefinition kWeaponsFamily{
    "Weapons", "Weapon Slot", 0x0AF3U,
    kWeaponFields.data(), kWeaponFields.size(), {}, false, LogicalGroupingKind::Flat
};

// The three editor-facing fields are the confirmed/understood portion of the
// five-array Current Sigils relationship. Instance and companion fields remain
// preserved but are not exposed by the simple editor.
constexpr std::array<LogicalFieldDefinition, 3> kCurrentSigilFields{{
    {0x0A8FU, 0U, "FF8F0A", "Sigil ID", "Confirmed", LogicalValueKind::Hash, false,
     "Sigils", false},
    {0x0A90U, 0U, "FF900A", "Sigil Level", "Confirmed", LogicalValueKind::Signed, false},
    {0x0A92U, 0U, "FF920A", "Equipped / Worn By", "Confirmed", LogicalValueKind::Hash, false,
     "Characters", false},
}};

constexpr LogicalFamilyDefinition kCurrentSigilsFamily{
    "Current Sigils", "Sigil Slot", 0x0A8FU,
    kCurrentSigilFields.data(), kCurrentSigilFields.size(), "Sigils", false,
    LogicalGroupingKind::Flat
};

constexpr std::array<LogicalFieldDefinition, 2> kAcquiredSigilFields{{
    {0x1F41U, 0U, "FF411F", "Acquired Sigil ID", "Confirmed", LogicalValueKind::Hash, false,
     "Sigils", false},
    {0x1F42U, 0U, "FF421F", "Acquired State / Companion", "Confirmed structure / unknown flags",
     LogicalValueKind::Bitfield, false},
}};

constexpr LogicalFamilyDefinition kAcquiredSigilsFamily{
    "Acquired Sigils", "Acquired Sigil Entry", 0x1F41U,
    kAcquiredSigilFields.data(), kAcquiredSigilFields.size(), "Sigils", false,
    LogicalGroupingKind::Flat
};

// Curio reward records share 4,995 exact UnitIDs. The fourth 0x0770 field is
// intentionally omitted because its meaning is not yet known.
constexpr std::array<LogicalFieldDefinition, 3> kCurioFields{{
    {0x076DU, 0U, "FF6D07", "Curio Reward Item ID", "Confirmed", LogicalValueKind::Hash, false},
    {0x076EU, 0U, "FF6E07", "Activation / Quantity", "Strong", LogicalValueKind::Signed, false},
    {0x076FU, 0U, "FF6F07", "State / Flags", "Tentative", LogicalValueKind::Bitfield, false},
}};

constexpr LogicalFamilyDefinition kCuriosFamily{
    "Curios", "Curio Reward Entry", 0x076DU,
    kCurioFields.data(), kCurioFields.size(), {}, false, LogicalGroupingKind::CurioSlotEntries
};

// Confirmed scalar values can be edited together as a single global record.
constexpr std::array<LogicalFieldDefinition, 4> kQuickValueFields{{
    {0x0450U, 0U, "FF5004", "Rupies", "Confirmed", LogicalValueKind::Signed, false},
    {0x0451U, 0U, "FF5104", "Transmarvel Currency / Count", "Strong", LogicalValueKind::Signed, false},
    {0x0452U, 0U, "FF5204", "Commendations", "Confirmed", LogicalValueKind::Signed, false},
    {0x0458U, 0U, "FF5804", "Mastery Points (MSP)", "Confirmed", LogicalValueKind::Signed, false},
}};

constexpr LogicalFamilyDefinition kQuickValuesFamily{
    "Quick Values", "Global Values", 0x0450U,
    kQuickValueFields.data(), kQuickValueFields.size(), {}, false, LogicalGroupingKind::Flat
};

constexpr std::array<const LogicalFamilyDefinition*, 9> kSharedLogicalFamilies{{
    &kQuickValuesFamily,
    &kSummonFamily,
    &kCurrentTraitsFamily,
    &kOverMasteryFamily,
    &kItemsFamily,
    &kWeaponsFamily,
    &kCurrentSigilsFamily,
    &kAcquiredSigilsFamily,
    &kCuriosFamily,
}};

} // namespace

const LogicalFamilyDefinition& summonInventoryFamily() noexcept { return kSummonFamily; }
const LogicalFamilyDefinition& masteryTreeFamily() noexcept { return kMasteryTreeFamily; }
const LogicalFamilyDefinition& currentTraitsFamily() noexcept { return kCurrentTraitsFamily; }
const LogicalFamilyDefinition& overMasteryFamily() noexcept { return kOverMasteryFamily; }
const LogicalFamilyDefinition& itemsFamily() noexcept { return kItemsFamily; }
const LogicalFamilyDefinition& weaponsFamily() noexcept { return kWeaponsFamily; }
const LogicalFamilyDefinition& currentSigilsFamily() noexcept { return kCurrentSigilsFamily; }
const LogicalFamilyDefinition& acquiredSigilsFamily() noexcept { return kAcquiredSigilsFamily; }
const LogicalFamilyDefinition& curiosFamily() noexcept { return kCuriosFamily; }
const LogicalFamilyDefinition& quickValuesFamily() noexcept { return kQuickValuesFamily; }

const std::array<const LogicalFamilyDefinition*, 9>& sharedLogicalFamilies() noexcept {
    return kSharedLogicalFamilies;
}

const LogicalFamilyDefinition* logicalFamilyForAnchor(std::uint32_t anchorKey) noexcept {
    if (anchorKey == kSummonFamily.anchorKey) return &kSummonFamily;
    if (anchorKey == kMasteryTreeFamily.anchorKey) return &kMasteryTreeFamily;
    if (anchorKey == kCurrentTraitsFamily.anchorKey) return &kCurrentTraitsFamily;
    if (anchorKey == kOverMasteryFamily.anchorKey) return &kOverMasteryFamily;
    if (anchorKey == kItemsFamily.anchorKey) return &kItemsFamily;
    if (anchorKey == kWeaponsFamily.anchorKey) return &kWeaponsFamily;
    if (anchorKey == kCurrentSigilsFamily.anchorKey) return &kCurrentSigilsFamily;
    if (anchorKey == kAcquiredSigilsFamily.anchorKey) return &kAcquiredSigilsFamily;
    if (anchorKey == kCuriosFamily.anchorKey) return &kCuriosFamily;
    if (anchorKey == kQuickValuesFamily.anchorKey) return &kQuickValuesFamily;
    return nullptr;
}

bool familyAvailable(const SaveData& save, const LogicalFamilyDefinition& family) noexcept {
    if (!save.findGroup(family.anchorKey)) return false;
    for (std::size_t index = 0; index < family.fieldCount; ++index) {
        const auto& field = family.fields[index];
        if (!field.optional && !save.findGroup(field.key)) return false;
    }
    return true;
}

LogicalUnitAddress decodeLogicalUnitId(const LogicalFamilyDefinition& family,
                                           std::uint32_t unitId) noexcept {
    LogicalUnitAddress result{};
    switch (family.grouping) {
    case LogicalGroupingKind::Flat:
        result.valid = true;
        result.slot = unitId;
        return result;
    case LogicalGroupingKind::MasteryCharacter:
        result.valid = true;
        if (unitId < 10000000U) {
            result.shared = true;
            result.slot = unitId;
            return result;
        }
        result.characterScoped = true;
        unitId -= 10000000U;
        result.characterGroup = unitId / 1000U;
        result.slot = unitId % 1000U;
        return result;
    case LogicalGroupingKind::OverMasteryCharacter:
        if (unitId < 10000000U) return result;
        result.valid = true;
        result.characterScoped = true;
        unitId -= 10000000U;
        result.characterGroup = unitId / 1000U;
        result.slot = unitId % 1000U;
        return result;
    case LogicalGroupingKind::CurrentTraitsCharacter: {
        const auto nameSpace = unitId / 10000000U;
        if (nameSpace < 12U || nameSpace > 14U) return result;
        const auto remainder = unitId % 10000000U;
        result.valid = true;
        result.characterScoped = true;
        result.nameSpace = nameSpace;
        result.characterGroup = remainder / 10000U;
        const auto local = remainder % 10000U;
        result.position = local / 100U;
        result.slot = local % 100U;
        return result;
    }
    case LogicalGroupingKind::CurioSlotEntries:
        result.position = unitId % 100U;
        if (result.position > 4U) return result;
        result.valid = true;
        result.slot = unitId / 100U;
        return result;
    }
    return result;
}

bool logicalFieldAvailable(const SaveData& save,
                           const LogicalFieldDefinition& field,
                           std::uint32_t unitId) noexcept {
    const auto* record = save.findRecord(field.key, unitId);
    return record && field.elementIndex < record->elementCount;
}

std::string logicalHashDisplayName(const HashDatabase& hashDatabase,
                                   std::uint32_t hash) {
    if (hash == 0U || hash == 0xFFFFFFFFU || isGlobalEmptySlotHash(hash)) return {};
    if (const auto* entry = hashDatabase.preferred(hash)) {
        if (!entry->displayName.empty()) return entry->displayName;
        if (!entry->id.empty()) return entry->id;
    }
    return "0x" + hashHex(hash);
}

std::string logicalHashFieldDisplayName(const SaveData& save,
                                        const HashDatabase& hashDatabase,
                                        const LogicalFieldDefinition& field,
                                        std::uint32_t unitId) {
    if (field.kind != LogicalValueKind::Hash ||
        !logicalFieldAvailable(save, field, unitId)) {
        return {};
    }
    const auto hash = static_cast<std::uint32_t>(
        save.elementBits(field.key, unitId, field.elementIndex));
    return logicalHashDisplayName(hashDatabase, hash);
}

std::vector<std::string> curioSlotRewardDisplayNames(const SaveData& save,
                                                     const HashDatabase& hashDatabase,
                                                     std::uint32_t slotIndex) {
    const auto& family = curiosFamily();
    const auto* anchor = save.findGroup(family.anchorKey);
    if (!anchor || family.fieldCount == 0U) return {};

    const auto& rewardField = family.fields[0];
    std::vector<std::pair<std::uint32_t, std::uint32_t>> entries;
    for (const auto& record : anchor->records) {
        const auto address = decodeLogicalUnitId(family, record.index);
        if (!address.valid || address.slot != slotIndex) continue;
        entries.emplace_back(address.position, record.index);
    }
    std::sort(entries.begin(), entries.end());

    std::vector<std::string> names;
    std::set<std::string> includedNames;
    for (const auto& [position, unitId] : entries) {
        (void)position;
        if (!logicalFieldAvailable(save, rewardField, unitId)) continue;
        const auto rewardHash = static_cast<std::uint32_t>(
            save.elementBits(rewardField.key, unitId, rewardField.elementIndex));
        if (isGlobalEmptySlotHash(rewardHash)) continue;

        auto displayName = logicalHashDisplayName(hashDatabase, rewardHash);
        if (displayName.empty()) continue;
        if (includedNames.insert(displayName).second) names.push_back(std::move(displayName));
    }
    return names;
}

} // namespace gdtv
