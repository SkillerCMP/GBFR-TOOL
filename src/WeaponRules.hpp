#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace gdtv {

inline constexpr std::uint16_t kUnknownWeaponCharacterGroup = 0xFFFFU;
inline constexpr std::size_t kWeaponSetupSlotCount = 5U;

struct WeaponRuleDefinition {
    // Selector key stored in the save's FF F3 0A weapon record.
    std::uint32_t weaponHash{};
    // Public weapon-definition hash used by the hash/name database.
    std::uint32_t displayHash{};
    std::uint16_t characterGroup{kUnknownWeaponCharacterGroup};
    std::array<std::uint32_t, kWeaponSetupSlotCount> traitGroupHashes{};
};

struct WeaponRuleRange {
    const WeaponRuleDefinition* data{};
    std::size_t size{};

    [[nodiscard]] constexpr const WeaponRuleDefinition* begin() const noexcept { return data; }
    [[nodiscard]] constexpr const WeaponRuleDefinition* end() const noexcept {
        return data ? data + size : nullptr;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return size == 0U; }
};

struct WeaponTraitRuleGroup {
    std::uint32_t groupHash{};
    std::uint32_t firstChoice{};
    std::uint16_t choiceCount{};
};

struct WeaponTraitChoiceRange {
    const std::uint32_t* data{};
    std::size_t size{};

    [[nodiscard]] constexpr const std::uint32_t* begin() const noexcept { return data; }
    [[nodiscard]] constexpr const std::uint32_t* end() const noexcept {
        return data ? data + size : nullptr;
    }
    [[nodiscard]] constexpr bool empty() const noexcept { return size == 0U; }
};

[[nodiscard]] WeaponRuleRange weaponRules() noexcept;
[[nodiscard]] const WeaponRuleDefinition* weaponRuleForHash(
    std::uint32_t weaponHash) noexcept;
// Hash used to resolve the weapon name/ID in GBFR-Hash-Database.txt. Some
// save keys are already the public/database hash and therefore carry the
// empty-slot sentinel in displayHash instead of a second hash.
[[nodiscard]] std::uint32_t weaponDatabaseHash(
    const WeaponRuleDefinition& definition) noexcept;
[[nodiscard]] std::uint32_t weaponDatabaseHashForSaveHash(
    std::uint32_t weaponHash) noexcept;
[[nodiscard]] WeaponRuleRange weaponRulesForDisplayHash(
    std::uint32_t displayHash) noexcept;
[[nodiscard]] const WeaponTraitRuleGroup* weaponTraitRuleGroup(
    std::uint32_t groupHash) noexcept;
[[nodiscard]] WeaponTraitChoiceRange weaponTraitChoices(
    std::uint32_t groupHash) noexcept;
[[nodiscard]] bool weaponTraitAllowed(
    std::uint32_t groupHash, std::uint32_t traitHash) noexcept;
[[nodiscard]] std::size_t weaponRuleCount() noexcept;
[[nodiscard]] std::size_t weaponTraitRuleGroupCount() noexcept;

} // namespace gdtv
