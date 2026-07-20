#include "RelationshipMap.hpp"

#include "LogicalFamilies.hpp"

#include <algorithm>
#include <array>
#include <limits>
#include <map>
#include <set>
#include <tuple>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace gdtv {
namespace {

std::vector<std::uint32_t> unitIds(const KeyGroup& group) {
    std::vector<std::uint32_t> result;
    result.reserve(group.records.size());
    for (const auto& record : group.records) result.push_back(record.index);
    return result;
}

bool sameUnitIds(const SaveData& save, const std::vector<std::uint32_t>& keys,
                 std::size_t* recordCount = nullptr) {
    if (keys.size() < 2U) return false;
    const auto* first = save.findGroup(keys.front());
    if (!first || first->records.empty()) return false;
    const auto signature = unitIds(*first);
    for (std::size_t index = 1U; index < keys.size(); ++index) {
        const auto* group = save.findGroup(keys[index]);
        if (!group || unitIds(*group) != signature) return false;
    }
    if (recordCount) *recordCount = first->records.size();
    return true;
}

void addKnownFamily(std::vector<RelationshipFamilyInfo>& output, const SaveData& save,
                    std::string name, std::string confidence, std::string reason,
                    std::vector<std::uint32_t> required,
                    std::vector<std::uint32_t> optional = {}) {
    for (const auto key : required) {
        if (!save.findGroup(key)) return;
    }
    std::vector<std::uint32_t> members = std::move(required);
    for (const auto key : optional) {
        if (save.findGroup(key)) members.push_back(key);
    }
    std::sort(members.begin(), members.end());
    members.erase(std::unique(members.begin(), members.end()), members.end());
    std::size_t records = 0U;
    if (!sameUnitIds(save, members, &records)) return;
    output.push_back(RelationshipFamilyInfo{
        std::move(name), std::move(confidence), std::move(reason), true,
        std::move(members), records
    });
}

bool containedByExplicitFamily(const std::vector<RelationshipFamilyInfo>& families,
                               const std::vector<std::uint32_t>& keys) {
    for (const auto& family : families) {
        if (!family.explicitDefinition) continue;
        bool contained = true;
        for (const auto key : keys) {
            if (std::find(family.keys.begin(), family.keys.end(), key) == family.keys.end()) {
                contained = false;
                break;
            }
        }
        if (contained) return true;
    }
    return false;
}

} // namespace

std::vector<PhysicalSectionInfo> buildPhysicalSectionOrder(const SaveData& save) {
    std::vector<PhysicalSectionInfo> result;
    result.reserve(save.groupsByKey().size());

    for (const auto& [key, group] : save.groupsByKey()) {
        if (group.records.empty()) continue;
        auto firstRecord = std::numeric_limits<std::uint32_t>::max();
        std::uint32_t lastRecord = 0U;
        auto firstPayload = std::numeric_limits<std::uint32_t>::max();
        std::uint64_t lastPayloadEnd = 0U;

        for (const auto& record : group.records) {
            firstRecord = std::min(firstRecord, record.recordOffset);
            lastRecord = std::max(lastRecord, record.recordOffset);
            if (record.payloadOffset) {
                firstPayload = std::min(firstPayload, *record.payloadOffset);
                lastPayloadEnd = std::max(lastPayloadEnd,
                    static_cast<std::uint64_t>(*record.payloadOffset) + record.payloadByteLength);
            }
        }

        result.push_back(PhysicalSectionInfo{
            key,
            group.vectorNumber,
            firstRecord,
            lastRecord,
            firstPayload == std::numeric_limits<std::uint32_t>::max() ? 0U : firstPayload,
            static_cast<std::uint32_t>(std::min<std::uint64_t>(
                lastPayloadEnd, std::numeric_limits<std::uint32_t>::max())),
            group.records.size()
        });
    }

    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        return std::tie(left.firstRecordOffset, left.vectorNumber, left.key) <
               std::tie(right.firstRecordOffset, right.vectorNumber, right.key);
    });
    return result;
}

std::vector<RelationshipFamilyInfo> buildRelationshipFamilies(const SaveData& save) {
    std::vector<RelationshipFamilyInfo> result;

    addKnownFamily(result, save, "Summon Inventory", "Confirmed",
                   "Seven logical fields stored across five parallel section arrays.",
                   {0x05B0U, 0x05B1U, 0x05B2U, 0x05B3U, 0x05B4U});
    addKnownFamily(result, save, "Mastery Tree", "Confirmed",
                   "Mastery IDs and activation states share the complete UnitID set.",
                   {0x0641U, 0x0642U}, {0x0645U});
    addKnownFamily(result, save, "OverMastery", "Confirmed",
                   "OverMastery IDs and state/level values share the complete UnitID set.",
                   {0x0646U, 0x0647U});
    addKnownFamily(result, save, "Current Traits", "Confirmed",
                   "Trait IDs and trait levels share the complete UnitID set.",
                   {0x06A5U, 0x06A6U});
    addKnownFamily(result, save, "Items", "Confirmed",
                   "Item ID, count, and companion fields share UnitIDs.",
                   {0x0709U, 0x070AU, 0x070BU, 0x070CU, 0x070FU});
    addKnownFamily(result, save, "Current Sigils", "Confirmed",
                   "Sigil instance, ID, level, wearer, and companion fields share UnitIDs 30000-35099.",
                   {0x0A8EU, 0x0A8FU, 0x0A90U, 0x0A92U, 0x0A93U});
    addKnownFamily(result, save, "Acquired Sigils", "Confirmed",
                   "Acquired sigil IDs and companion states share the complete UnitID set.",
                   {0x1F41U, 0x1F42U});
    addKnownFamily(result, save, "Weapons", "Confirmed",
                   "Weapon ID, XP, level cap, Mirage Munitions, and Wrightstone share UnitIDs 40000-40255.",
                   {0x0AF3U, 0x0AF4U, 0x0AF5U, 0x0AF6U, 0x0B00U});
    addKnownFamily(result, save, "Curios", "Confirmed",
                   "Curio reward item, activation/quantity, state, and companion fields share exact UnitIDs.",
                   {0x076DU, 0x076EU, 0x076FU, 0x0770U});
    addKnownFamily(result, save, "Quick Values", "Confirmed",
                   "Global scalar values share logical UnitID 0; DLC special-currency balances are optional.",
                   {0x0450U, 0x0451U, 0x0452U, 0x0458U},
                   {0x0A31U, 0x045CU});

    const auto& detected = save.linkedClusters();
    for (std::size_t index = 0U; index < detected.size(); ++index) {
        const auto& cluster = detected[index];
        if (cluster.size() < 2U || containedByExplicitFamily(result, cluster)) continue;
        const auto* first = save.findGroup(cluster.front());
        if (!first || first->records.size() < 2U) continue;
        result.push_back(RelationshipFamilyInfo{
            "Detected Parallel Array " + std::to_string(index + 1U),
            "Strong",
            "Sections have the exact same UnitID signature and nearby numeric IDTypes.",
            false,
            cluster,
            first->records.size()
        });
    }

    std::sort(result.begin(), result.end(), [](const auto& left, const auto& right) {
        if (left.explicitDefinition != right.explicitDefinition) return left.explicitDefinition > right.explicitDefinition;
        if (left.keys.empty() || right.keys.empty()) return left.name < right.name;
        if (left.keys.front() != right.keys.front()) return left.keys.front() < right.keys.front();
        return left.name < right.name;
    });
    return result;
}


std::vector<ReferenceSectionInfo> buildReferenceSections(const SaveData& save,
                                                         const HashDatabase& database) {
    std::vector<ReferenceSectionInfo> result;
    const auto physical = buildPhysicalSectionOrder(save);
    std::unordered_map<std::uint32_t, std::size_t> physicalPositions;
    for (std::size_t index = 0U; index < physical.size(); ++index) {
        physicalPositions[physical[index].key] = index;
    }

    for (const auto& [key, group] : save.groupsByKey()) {
        if (group.valueType() != ValueType::UInt) continue;
        struct CategoryWork {
            std::uint64_t occurrences{};
            std::vector<std::uint32_t> examples;
            std::unordered_set<std::uint32_t> seen;
        };
        std::map<std::string, CategoryWork> workByCategory;
        ReferenceSectionInfo summary;
        summary.key = key;

        for (const auto& record : group.records) {
            const auto payload = save.payloadView(record);
            const auto* bytes = reinterpret_cast<const unsigned char*>(payload.data());
            const auto count = payload.size() / sizeof(std::uint32_t);
            for (std::size_t element = 0U; element < count; ++element) {
                const auto offset = element * sizeof(std::uint32_t);
                const auto value = static_cast<std::uint32_t>(bytes[offset]) |
                    (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
                    (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
                    (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
                ++summary.totalValues;
                if (value == 0U || value == 0xFFFFFFFFU || isGlobalEmptySlotHash(value)) continue;
                const auto* entry = database.preferred(value);
                if (!entry) {
                    ++summary.unresolvedValues;
                    continue;
                }
                ++summary.resolvedValues;
                const auto category = entry->category.empty() ? std::string("Uncategorized") : entry->category;
                auto& work = workByCategory[category];
                ++work.occurrences;
                if (work.seen.insert(value).second && work.examples.size() < 8U) {
                    work.examples.push_back(value);
                }
            }
        }

        if (summary.resolvedValues == 0U) continue;
        for (auto& [category, work] : workByCategory) {
            summary.categories.push_back(ReferenceCategoryInfo{
                category, work.occurrences, std::move(work.examples)
            });
        }
        std::sort(summary.categories.begin(), summary.categories.end(), [](const auto& left, const auto& right) {
            if (left.occurrences != right.occurrences) return left.occurrences > right.occurrences;
            return left.category < right.category;
        });
        result.push_back(std::move(summary));
    }

    std::sort(result.begin(), result.end(), [&](const auto& left, const auto& right) {
        const auto leftIt = physicalPositions.find(left.key);
        const auto rightIt = physicalPositions.find(right.key);
        const auto leftPosition = leftIt == physicalPositions.end()
            ? std::numeric_limits<std::size_t>::max() : leftIt->second;
        const auto rightPosition = rightIt == physicalPositions.end()
            ? std::numeric_limits<std::size_t>::max() : rightIt->second;
        if (leftPosition != rightPosition) return leftPosition < rightPosition;
        return left.key < right.key;
    });
    return result;
}

std::vector<std::uint32_t> exactUnitIdPeers(const SaveData& save, std::uint32_t key,
                                            std::size_t minimumRecords) {
    const auto* target = save.findGroup(key);
    if (!target || target->records.size() < minimumRecords) return {};
    const auto signature = unitIds(*target);
    std::vector<std::uint32_t> peers;
    for (const auto& [candidateKey, group] : save.groupsByKey()) {
        if (candidateKey == key || group.records.size() != signature.size()) continue;
        if (unitIds(group) == signature) peers.push_back(candidateKey);
    }
    std::sort(peers.begin(), peers.end());
    return peers;
}

} // namespace gdtv
