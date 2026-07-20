#pragma once

#include "GameDataCore.hpp"
#include "HashDatabase.hpp"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace gdtv {

struct PhysicalSectionInfo {
    std::uint32_t key{};
    std::uint32_t vectorNumber{};
    std::uint32_t firstRecordOffset{};
    std::uint32_t lastRecordOffset{};
    std::uint32_t firstPayloadOffset{};
    std::uint32_t lastPayloadEnd{};
    std::size_t recordCount{};
};

struct ReferenceCategoryInfo {
    std::string category;
    std::uint64_t occurrences{};
    std::vector<std::uint32_t> exampleHashes;
};

struct ReferenceSectionInfo {
    std::uint32_t key{};
    std::uint64_t totalValues{};
    std::uint64_t resolvedValues{};
    std::uint64_t unresolvedValues{};
    std::vector<ReferenceCategoryInfo> categories;
};

struct RelationshipFamilyInfo {
    std::string name;
    std::string confidence;
    std::string reason;
    bool explicitDefinition{};
    std::vector<std::uint32_t> keys;
    std::size_t recordCount{};
};

[[nodiscard]] std::vector<PhysicalSectionInfo> buildPhysicalSectionOrder(const SaveData& save);
[[nodiscard]] std::vector<RelationshipFamilyInfo> buildRelationshipFamilies(const SaveData& save);
[[nodiscard]] std::vector<ReferenceSectionInfo> buildReferenceSections(
    const SaveData& save, const HashDatabase& database);
[[nodiscard]] std::vector<std::uint32_t> exactUnitIdPeers(const SaveData& save,
                                                         std::uint32_t key,
                                                         std::size_t minimumRecords = 2U);

} // namespace gdtv
