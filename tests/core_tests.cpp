#include "CharacterSections.hpp"
#include "DataFilePaths.hpp"
#include "GameDataCore.hpp"
#include "HashDatabase.hpp"
#include "LogicalFamilies.hpp"
#include "SectionNames.hpp"
#include "ValueDecoder.hpp"

#include <array>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

namespace {

int failures = 0;

void expect(bool condition, const char* message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

} // namespace

int main() {
    using namespace gdtv;

    const auto dataPathRoot = std::filesystem::temp_directory_path() /
                              "gdtv-bundled-data-path-tests";
    const auto dataPathFolder = dataPathRoot / kHashFolderName;
    std::error_code dataPathError;
    std::filesystem::remove_all(dataPathRoot, dataPathError);
    std::filesystem::create_directories(dataPathFolder, dataPathError);
    {
        std::ofstream rootFile(dataPathRoot / "sample.txt", std::ios::binary);
        rootFile << "root";
    }
    expect(locateBundledDataFile(dataPathRoot, "sample.txt") == dataPathRoot / "sample.txt",
           "bundled data falls back to executable root");
    {
        std::ofstream nestedFile(dataPathFolder / "sample.txt", std::ios::binary);
        nestedFile << "nested";
    }
    expect(locateBundledDataFile(dataPathRoot, "sample.txt") == dataPathFolder / "sample.txt",
           "Hashfolder data takes precedence over executable root");
    expect(preferredBundledDataFile(dataPathRoot, "new.txt") == dataPathFolder / "new.txt",
           "new bundled data prefers existing Hashfolder");
    std::filesystem::remove_all(dataPathFolder, dataPathError);
    expect(preferredBundledDataFile(dataPathRoot, "new.txt") == dataPathRoot / "new.txt",
           "new bundled data falls back to executable root");
    std::filesystem::remove_all(dataPathRoot, dataPathError);

    const auto gallanza = xxHash32Custom("PL2400");
    expect(gallanza == 0x1BB37EF0U, "PL2400 custom XXHash32");
    expect(hashHex(gallanza) == "1BB37EF0", "canonical hash formatting");
    expect(hashRawLittleEndian(gallanza) == "F07EB31B", "raw little-endian formatting");
    expect(parseHashValue("1BB37EF0") == gallanza, "parse canonical hash");
    expect(parseHashValue("LE:F07EB31B") == gallanza, "parse raw LE hash prefix");
    expect(parseHashValue("F07EB31B", true) == gallanza, "parse raw LE hash flag");

    HashDatabase labelDatabase;
    HashEntry friendlyLabel{};
    friendlyLabel.hash = 0x12345678U;
    friendlyLabel.id = "SKILL_TEST_TRAIT";
    friendlyLabel.displayName = "Test Trait";
    friendlyLabel.category = "Trait";
    labelDatabase.addOrUpdateUser(friendlyLabel);
    HashEntry internalOnlyLabel{};
    internalOnlyLabel.hash = 0x23456789U;
    internalOnlyLabel.id = "LB_TEST_INTERNAL";
    internalOnlyLabel.category = "Masteries";
    labelDatabase.addOrUpdateUser(internalOnlyLabel);
    expect(logicalHashDisplayName(labelDatabase, 0x12345678U) == "Test Trait",
           "logical label prefers friendly name");
    expect(logicalHashDisplayName(labelDatabase, 0x23456789U) == "LB_TEST_INTERNAL",
           "logical label falls back to internal ID");
    expect(logicalHashDisplayName(labelDatabase, 0x3456789AU) == "0x3456789A",
           "logical label falls back to unresolved hash");
    expect(logicalHashDisplayName(labelDatabase, kGlobalEmptySlotHash).empty(),
           "logical label omits global empty slot");
    expect(logicalHashDisplayName(labelDatabase, 0U).empty(),
           "logical label omits zero sentinel");
    expect(logicalHashDisplayName(labelDatabase, 0xFFFFFFFFU).empty(),
           "logical label omits all-ones sentinel");

    const auto& summonFamily = summonInventoryFamily();
    expect(summonFamily.anchorKey == 0x05B1U, "summon family anchor key");
    expect(summonFamily.fieldCount == 7U, "summon family field count");
    expect(summonFamily.fields[2].key == 0x05B2U && summonFamily.fields[2].elementIndex == 0U,
           "main trait logical field");
    expect(summonFamily.fields[2].label == "Active Trait ID (Left Icon)",
           "main trait logical label");
    expect(summonFamily.fields[3].key == 0x05B2U && summonFamily.fields[3].elementIndex == 1U,
           "equipment bonus logical field");
    expect(summonFamily.fields[3].label == "Active Equip Bonus ID (Right Icon)",
           "equipment bonus logical label");
    expect(summonFamily.fields[4].key == 0x05B3U && summonFamily.fields[4].elementIndex == 0U,
           "main trait level logical field");
    expect(summonFamily.fields[4].label == "Active Trait Level",
           "main trait level logical label");

    const auto& masteryFamily = masteryTreeFamily();
    expect(masteryFamily.anchorKey == 0x0641U, "mastery family anchor key");
    expect(masteryFamily.fieldCount == 3U, "mastery family field count");
    expect(masteryFamily.fields[0].key == 0x0641U &&
           masteryFamily.fields[0].kind == LogicalValueKind::Hash,
           "mastery ID field");
    expect(masteryFamily.fields[1].key == 0x0642U &&
           masteryFamily.fields[1].kind == LogicalValueKind::Bitfield,
           "mastery activated/state field");
    expect(masteryFamily.fields[2].key == 0x0645U && masteryFamily.fields[2].optional,
           "optional legacy mastery companion");
    expect(logicalFamilyForAnchor(0x0641U) == &masteryFamily,
           "mastery family anchor lookup");

    const auto& currentTraits = currentTraitsFamily();
    expect(currentTraits.anchorKey == 0x06A5U, "current traits anchor key");
    expect(currentTraits.fieldCount == 2U, "current traits field count");
    expect(currentTraits.fields[0].key == 0x06A5U &&
           currentTraits.fields[0].kind == LogicalValueKind::Hash,
           "current trait ID field");
    expect(currentTraits.fields[1].key == 0x06A6U &&
           currentTraits.fields[1].kind == LogicalValueKind::Signed,
           "current trait level field");
    expect(currentTraits.hashCategoryFilter == "Trait" && !currentTraits.hashCategoryPrefix,
           "current trait hash-list category");

    const auto& overMastery = overMasteryFamily();
    expect(overMastery.anchorKey == 0x0646U, "overmastery anchor key");
    expect(overMastery.fieldCount == 2U, "overmastery field count");
    expect(overMastery.fields[0].key == 0x0646U &&
           overMastery.fields[0].kind == LogicalValueKind::Hash,
           "overmastery ID field");
    expect(overMastery.fields[1].key == 0x0647U &&
           overMastery.fields[1].kind == LogicalValueKind::Bitfield,
           "overmastery level/state field");
    expect(overMastery.hashCategoryFilter == "Masteries - Over" &&
           !overMastery.hashCategoryPrefix,
           "overmastery hash-list category");

    const auto& items = itemsFamily();
    expect(items.anchorKey == 0x0709U, "items anchor key");
    expect(items.fieldCount == 5U, "items field count");
    expect(items.fields[0].key == 0x0709U && items.fields[0].kind == LogicalValueKind::Hash,
           "item ID field");
    expect(items.fields[1].key == 0x070AU && items.fields[1].kind == LogicalValueKind::Signed,
           "item count field");
    expect(items.fields[2].key == 0x070BU && items.fields[2].kind == LogicalValueKind::Bitfield,
           "item flags field");
    expect(items.fields[3].key == 0x070CU && items.fields[3].kind == LogicalValueKind::Bitfield,
           "first unknown item field");
    expect(items.fields[4].key == 0x070FU && items.fields[4].kind == LogicalValueKind::Bitfield,
           "second unknown item field");

    const auto& sharedFamilies = sharedLogicalFamilies();
    expect(sharedFamilies.size() == 9U, "shared logical family count");
    expect(sharedFamilies[0] == &quickValuesFamily() && sharedFamilies[1] == &summonFamily &&
           sharedFamilies[2] == &currentTraits && sharedFamilies[3] == &overMastery &&
           sharedFamilies[4] == &items && sharedFamilies[5] == &weaponsFamily() &&
           sharedFamilies[6] == &currentSigilsFamily() &&
           sharedFamilies[7] == &acquiredSigilsFamily() && sharedFamilies[8] == &curiosFamily(),
           "shared logical family order");
    expect(logicalFamilyForAnchor(0x06A5U) == &currentTraits,
           "current traits anchor lookup");
    expect(logicalFamilyForAnchor(0x0646U) == &overMastery,
           "overmastery anchor lookup");
    expect(logicalFamilyForAnchor(0x0709U) == &items,
           "items anchor lookup");

    const auto& weapons = weaponsFamily();
    expect(weapons.anchorKey == 0x0AF3U && weapons.fieldCount == 5U,
           "weapons family definition");
    expect(weapons.fields[0].hashCategoryFilter == "Weapons",
           "weapon ID field filter");
    expect(weapons.fields[4].key == 0x0B00U &&
           weapons.fields[4].kind == LogicalValueKind::Hash,
           "weapon Wrightstone field");

    const auto& currentSigils = currentSigilsFamily();
    expect(currentSigils.anchorKey == 0x0A8FU && currentSigils.fieldCount == 3U,
           "current sigils editor fields");
    expect(currentSigils.fields[0].hashCategoryFilter == "Sigils" &&
           currentSigils.fields[2].hashCategoryFilter == "Characters",
           "current sigils per-field filters");

    const auto& acquiredSigils = acquiredSigilsFamily();
    expect(acquiredSigils.anchorKey == 0x1F41U && acquiredSigils.fieldCount == 2U,
           "acquired sigils family definition");

    const auto& curios = curiosFamily();
    expect(curios.anchorKey == 0x076DU && curios.fieldCount == 3U,
           "curios family definition");
    expect(curios.grouping == LogicalGroupingKind::CurioSlotEntries,
           "curios grouped as five entries per slot");
    expect(curios.fields[1].key == 0x076EU && curios.fields[1].kind == LogicalValueKind::Signed,
           "curio activation quantity field");

    const auto& quickValues = quickValuesFamily();
    expect(quickValues.anchorKey == 0x0450U && quickValues.fieldCount == 4U,
           "quick values family definition");
    expect(quickValues.fields[3].key == 0x0458U,
           "quick values mastery points field");

    const auto masteryShared = decodeLogicalUnitId(masteryFamily, 199U);
    expect(masteryShared.valid && masteryShared.shared && masteryShared.slot == 199U,
           "mastery shared slot decoding");
    const auto masteryCharacter = decodeLogicalUnitId(masteryFamily, 10040399U);
    expect(masteryCharacter.valid && masteryCharacter.characterScoped &&
           masteryCharacter.characterGroup == 40U && masteryCharacter.slot == 399U,
           "mastery character slot decoding");
    const auto overCharacter = decodeLogicalUnitId(overMastery, 10040003U);
    expect(overCharacter.valid && overCharacter.characterGroup == 40U &&
           overCharacter.slot == 3U, "overmastery character slot decoding");
    const auto currentTrait = decodeLogicalUnitId(currentTraits, 140499902U);
    expect(currentTrait.valid && currentTrait.nameSpace == 14U &&
           currentTrait.characterGroup == 49U && currentTrait.position == 99U &&
           currentTrait.slot == 2U, "current trait namespace/character/position/slot decoding");
    const auto curioSlotOneItem = decodeLogicalUnitId(curios, 0U);
    expect(curioSlotOneItem.valid && curioSlotOneItem.slot == 0U &&
           curioSlotOneItem.position == 0U, "curio slot 1 item entry decoding");
    const auto curioSlotOneUnknownThree = decodeLogicalUnitId(curios, 4U);
    expect(curioSlotOneUnknownThree.valid && curioSlotOneUnknownThree.slot == 0U &&
           curioSlotOneUnknownThree.position == 4U, "curio slot 1 fifth entry decoding");
    const auto curioSlotTwoSigil = decodeLogicalUnitId(curios, 101U);
    expect(curioSlotTwoSigil.valid && curioSlotTwoSigil.slot == 1U &&
           curioSlotTwoSigil.position == 1U, "curio slot 2 sigil entry decoding");
    const auto curioLastEntry = decodeLogicalUnitId(curios, 99804U);
    expect(curioLastEntry.valid && curioLastEntry.slot == 998U &&
           curioLastEntry.position == 4U, "curio final slot decoding");
    expect(!decodeLogicalUnitId(curios, 105U).valid,
           "curio non-entry remainder rejected");

    const auto characterTemp = std::filesystem::temp_directory_path() /
                               "gdtv-character-sections-unit-test.txt";
    {
        std::ofstream characters(characterTemp, std::ios::binary | std::ios::trunc);
        characters << "[CHARACTERGROUP|INGAMENAME|INTERNALNAME|NOTE]\n"
                   << "0|Male - Gran|PL0000|Confirmed section\n"
                   << "40|NA|PL2400|NA\n";
    }
    CharacterSectionMap characterSections;
    expect(characterSections.load(characterTemp) == 2U, "character section map load count");
    const auto* characterZero = characterSections.find(0U);
    expect(characterZero && characterZero->inGameName == "Male - Gran" &&
           characterZero->internalName == "PL0000", "character section named row");
    const auto* characterForty = characterSections.find(40U);
    expect(characterForty && characterForty->inGameName.empty() &&
           characterForty->internalName == "PL2400", "character section NA decoding");
    std::error_code characterEc;
    std::filesystem::remove(characterTemp, characterEc);

    const auto sectionNamesTemp = std::filesystem::temp_directory_path() /
                                  "gdtv-section-names-unit-test.txt";
    {
        std::ofstream sections(sectionNamesTemp, std::ios::binary | std::ios::trunc);
        sections << "[IDTYPE\tLOCATOR\tNAME\tSUBSYSTEM\tCONFIDENCE\tOFFICIALSAVEIDTYPE\t"
                    "STORAGETYPE\tHASHCATEGORY\tINTERNALPREFIX\tNOTE\tRECOMMENDEDTEST]\n"
                 << "0x0A8F\tFF8F0A0000\tSigil IDs\tSigils\tConfirmed\tGEMDATA_GEM_ID\t"
                    "UInt\tSigils\tGEEN_\tOfficial enum\tNA\n"
                 << "1104\tFF50040000\tRupies\tUser Data\tConfirmed\tUSERDATA_RUPIES\t"
                    "Int\tNA\tNA\tOfficial enum\tNA\n"
                 << "0x08FE\tFE08000000\tParty Set Names\tParty Sets\tConfirmed\tNA\t"
                    "Byte\tNA\tNA\tSpecial direct locator\tNA\n";
    }
    SectionNameMap sectionNames;
    expect(sectionNames.load(sectionNamesTemp) == 3U, "section name map load count");
    const auto* sigilSection = sectionNames.find(0x0A8FU);
    expect(sigilSection && sigilSection->name == "Sigil IDs" &&
           sigilSection->subsystem == "Sigils" &&
           sigilSection->hashCategory == "Sigils" &&
           sigilSection->internalPrefix == "GEEN_",
           "section name metadata row");
    const auto* rupiesSection = sectionNames.find(1104U);
    expect(rupiesSection && rupiesSection->officialSaveIdType == "USERDATA_RUPIES" &&
           rupiesSection->hashCategory.empty(), "section name decimal key and NA decoding");
    const auto* partySetNames = sectionNames.find(0x08FEU);
    expect(partySetNames && partySetNames->locator == "FE08000000",
           "section name preserves special non-FF locator");
    expect(confidenceMarker("Confirmed").empty(), "confirmed confidence marker");
    expect(confidenceMarker("Tentative") == " [?]", "tentative confidence marker");
    std::error_code sectionNamesEc;
    std::filesystem::remove(sectionNamesTemp, sectionNamesEc);

    const std::array<ValueType, 10> expectedTypes{
        ValueType::Bool, ValueType::Byte, ValueType::UByte, ValueType::Short, ValueType::UShort,
        ValueType::Int, ValueType::UInt, ValueType::Long, ValueType::ULong, ValueType::Float
    };
    const std::array<std::size_t, 10> expectedSizes{1, 1, 1, 2, 2, 4, 4, 8, 8, 4};
    for (std::uint32_t vector = 1; vector <= 10; ++vector) {
        expect(valueTypeForVector(vector) == expectedTypes[vector - 1U], "vector type mapping");
        expect(valueTypeSize(expectedTypes[vector - 1U]) == expectedSizes[vector - 1U], "vector element size");
    }

    const auto databaseTemp = std::filesystem::temp_directory_path() / "gdtv-hash-database-unit-test.txt";
    {
        std::ofstream database(databaseTemp, std::ios::binary | std::ios::trunc);
        database << "[HASH[BE]\tHASH[LE]\tTYPE\tINGAMENAME\tINTERNALNAME\tVERSION\tNOTE]\n"
                 << "1BB37EF0\tF07EB31B\tCharacters\tGallanza\tPL2400\tNA\tVerified internal name\n"
                 << "0E0287DC\tDC87020E\tWeapons\tSword of Eos\tNA\t2.0.2.0\tFriendly-only row\n";
    }
    HashDatabase layered;
    expect(layered.loadDatabase(databaseTemp) == 2U, "unified database load count");
    expect(layered.databaseEntryCount() == 2U, "unified database row count");
    expect(layered.friendlyEntryCount() == 2U, "in-game name row count");
    expect(layered.baseEntryCount() == 1U, "internal name row count");
    expect(layered.endianMismatchCount() == 0U, "unified database endian validation");
    expect(layered.invalidLineCount() == 0U, "unified database line validation");
    const auto* friendlyGallanza = layered.preferred(gallanza);
    expect(friendlyGallanza && friendlyGallanza->displayName == "Gallanza", "preferred in-game name");
    expect(friendlyGallanza && friendlyGallanza->builtInFriendly, "in-game name layer flag");
    expect(layered.allEntries().size() == 3U, "all hash entries accessor includes global empty slot");
    expect(layered.preferredMatching(gallanza, "Characters", "PL") == friendlyGallanza,
           "filtered preferred hash match");
    expect(layered.preferredMatching(gallanza, "Char*", "") == friendlyGallanza,
           "filtered category-prefix hash match");
    expect(layered.preferredMatching(gallanza, "Sigils", "GEEN_") == nullptr,
           "filtered preferred rejects wrong section type");
    expect(layered.hasMatchingEntry(gallanza, "Characters", "PL"),
           "filtered match presence");
    expect(!layered.hasMatchingEntry(gallanza, "Sigils", "GEEN_"),
           "filtered mismatch presence");

    const auto legacyPipeTemp = std::filesystem::temp_directory_path() /
                                "gdtv-hash-database-legacy-pipe-test.txt";
    {
        std::ofstream database(legacyPipeTemp, std::ios::binary | std::ios::trunc);
        database << "[HASH[BE]|HASH[LE]|TYPE|INGAMENAME|INTERNALNAME|VERSION|NOTE]\n"
                 << "1BB37EF0|F07EB31B|Characters|Gallanza|PL2400|NA|Legacy pipe row\n";
    }
    HashDatabase legacyPipe;
    expect(legacyPipe.loadDatabase(legacyPipeTemp) == 1U,
           "legacy pipe-delimited database remains readable");
    expect(legacyPipe.invalidLineCount() == 0U, "legacy pipe row validation");

    const auto versionRowsTemp = std::filesystem::temp_directory_path() / "gdtv-hash-version-rows.txt";
    {
        std::ofstream database(versionRowsTemp, std::ios::binary | std::ios::trunc);
        database << "[HASH[BE]\tHASH[LE]\tTYPE\tINGAMENAME\tINTERNALNAME\tVERSION\tNOTE]\n"
                 << "A8900C80\t800C90A8\tSummon Base Bonuses\tAttack Power Up +####\tNA\t2.0.2.0\tMax Value is 9\n"
                 << "A8900C80\t800C90A8\tSummon Base Bonuses\tAttack Power Up +####\tNA\t2.1.0.0\tMax Value is 10\n";
    }
    HashDatabase versionRows;
    expect(versionRows.loadDatabase(versionRowsTemp) == 2U,
           "same hash/name/type with different version metadata stays as separate rows");
    expect(versionRows.databaseEntryCount() == 2U, "versioned row count");
    expect(versionRows.preferredMatching(0xA8900C80U, "Summon Base Bonuses", "") != nullptr,
           "summon base bonus category matches renamed TYPE");
    expect(versionRows.preferredMatching(0xA8900C80U, "Equip-Bonuses", "") == nullptr,
           "retired equip-bonuses category no longer matches");
    const auto gallanzaNameMatches = layered.hashesForText(" Gallanza ");
    expect(gallanzaNameMatches.size() == 1U && gallanzaNameMatches.front() == gallanza,
           "in-game-name exact hash lookup");
    const auto gallanzaIdMatches = layered.hashesForText("pl2400");
    expect(gallanzaIdMatches.size() == 1U && gallanzaIdMatches.front() == gallanza,
           "internal-name case-insensitive hash lookup");

    HashDatabase hashes;
    HashEntry entry;
    entry.hash = gallanza;
    entry.id = "PL2400";
    entry.displayName = "Gallanza";
    entry.category = "Characters";
    entry.version = "2.0.2.0";
    entry.source = "Unit Test";
    entry.notes = "Round-trip | pipe allowed";
    hashes.addOrUpdateUser(entry);
    const auto* preferred = hashes.preferred(gallanza);
    expect(preferred != nullptr, "hash mapping exists");
    if (preferred) {
        expect(preferred->id == "PL2400", "hash internal-name mapping");
        expect(preferred->displayName == "Gallanza", "hash display-name mapping");
        expect(preferred->version == "2.0.2.0", "hash version mapping");
        expect(preferred->algorithmVerified, "hash internal-name algorithm verification");
    }

    layered.addOrUpdateUser(entry);
    const auto* overridePreferred = layered.preferred(gallanza);
    expect(overridePreferred && overridePreferred->userDefined, "session update beats loaded row");

    const auto roundTripTemp = std::filesystem::temp_directory_path() / "gdtv-hash-database-round-trip.txt";
    hashes.saveDatabase(roundTripTemp);
    HashDatabase reloaded;
    expect(reloaded.loadDatabase(roundTripTemp) == 1U, "unified database round-trip row count");
    const auto* roundTrip = reloaded.preferred(gallanza);
    expect(roundTrip && roundTrip->displayName == "Gallanza", "unified database round-trip value");
    expect(roundTrip && roundTrip->version == "2.0.2.0", "unified database round-trip version");
    expect(roundTrip && roundTrip->notes == "Round-trip | pipe allowed",
           "TAB database preserves pipe characters inside fields");
    {
        std::ifstream savedDatabase(roundTripTemp, std::ios::binary);
        std::string savedHeader;
        std::getline(savedDatabase, savedHeader);
        expect(savedHeader.find('\t') != std::string::npos &&
               savedHeader.find('|') == std::string::npos,
               "database writer emits a TAB-delimited header");
    }
    std::error_code ec;
    std::filesystem::remove(databaseTemp, ec);
    std::filesystem::remove(legacyPipeTemp, ec);
    std::filesystem::remove(versionRowsTemp, ec);
    std::filesystem::remove(roundTripTemp, ec);

    const std::array<char, 8> payload{
        static_cast<char>(0xF0), static_cast<char>(0x7E), static_cast<char>(0xB3), static_cast<char>(0x1B),
        0, 0, 0, 0
    };
    const auto decoded = decodeValues(std::string_view(payload.data(), payload.size()), ValueType::UInt, &hashes);
    expect(decoded.resolvedHashes == 1U, "decoded UInt hash resolution count");
    expect(decoded.text.find("Gallanza") != std::string::npos, "decoded UInt friendly name");
    expect(decoded.text.find("PL2400") != std::string::npos, "decoded UInt game ID");
    const HashResolutionFilter characterFilter{"Characters", "PL"};
    const auto filteredDecoded = decodeValues(std::string_view(payload.data(), payload.size()),
                                              ValueType::UInt, &hashes, 4000U, &characterFilter);
    expect(filteredDecoded.filterMismatches == 0U, "section-filtered hash decode match");
    const HashResolutionFilter sigilFilter{"Sigils", "GEEN_"};
    const auto mismatchedDecoded = decodeValues(std::string_view(payload.data(), payload.size()),
                                                ValueType::UInt, &hashes, 4000U, &sigilFilter);
    expect(mismatchedDecoded.filterMismatches == 1U, "section-filtered hash decode mismatch");
    expect(mismatchedDecoded.text.find("unexpected for this section") != std::string::npos,
           "section-filtered mismatch warning text");

    HashDatabase builtInEmptySlotHashes;
    const auto* builtInEmpty = builtInEmptySlotHashes.preferred(kGlobalEmptySlotHash);
    expect(builtInEmpty && builtInEmpty->displayName == kGlobalEmptySlotName,
           "global empty slot is built in without a database file");
    const auto* filteredBuiltInEmpty = builtInEmptySlotHashes.preferredMatching(
        kGlobalEmptySlotHash, "Sigils", "GEEN_");
    expect(filteredBuiltInEmpty && filteredBuiltInEmpty->displayName == kGlobalEmptySlotName,
           "global empty slot bypasses every ID filter");
    const auto globalTextMatches = builtInEmptySlotHashes.hashesForText("Global Empty Slot");
    expect(globalTextMatches.size() == 1U && globalTextMatches.front() == kGlobalEmptySlotHash,
           "global empty slot resolves by display name");
    HashEntry attemptedOverride;
    attemptedOverride.hash = kGlobalEmptySlotHash;
    attemptedOverride.displayName = "Old Empty Label";
    builtInEmptySlotHashes.addOrUpdateUser(std::move(attemptedOverride));
    const auto* protectedEmpty = builtInEmptySlotHashes.preferred(kGlobalEmptySlotHash);
    expect(protectedEmpty && protectedEmpty->displayName == kGlobalEmptySlotName,
           "global empty slot remains preferred after a user alias is added");

    HashDatabase emptySlotHashes;
    HashEntry emptySlotEntry;
    emptySlotEntry.hash = 0x887AE0B0U;
    emptySlotEntry.displayName = "--Removed (Used for Empty Slots)";
    emptySlotEntry.category = "Weapons";
    emptySlotHashes.addOrUpdateUser(emptySlotEntry);
    const std::array<char, 4> emptyPayload{
        static_cast<char>(0xB0), static_cast<char>(0xE0),
        static_cast<char>(0x7A), static_cast<char>(0x88)
    };
    const auto emptyFiltered = decodeValues(
        std::string_view(emptyPayload.data(), emptyPayload.size()), ValueType::UInt,
        &emptySlotHashes, 4000U, &sigilFilter);
    expect(emptyFiltered.filterMismatches == 0U,
           "empty-slot hash bypasses section mismatch warning");

    if (failures != 0) {
        std::cerr << failures << " test(s) failed.\n";
        return 1;
    }
    std::cout << "All core/hash tests passed.\n";
    return 0;
}
