#include "CharacterSections.hpp"
#include "DataFilePaths.hpp"
#include "GameDataCore.hpp"
#include "HashDatabase.hpp"
#include "RelationshipMap.hpp"
#include "SectionNames.hpp"
#include "version.hpp"

#include <filesystem>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <vector>

namespace {

std::filesystem::path executableDirectoryFromArgv0(const char* argv0) {
    std::error_code error;
    const auto absolutePath = std::filesystem::absolute(
        argv0 && *argv0 ? std::filesystem::path(argv0) : std::filesystem::path{}, error);
    if (!error && absolutePath.has_parent_path()) return absolutePath.parent_path();
    const auto current = std::filesystem::current_path(error);
    return error ? std::filesystem::path{"."} : current;
}

void useBundledDefault(std::filesystem::path& value,
                       const std::filesystem::path& executableDirectory,
                       const std::filesystem::path& fileName) {
    if (!value.empty()) return;
    const auto candidate = gdtv::locateBundledDataFile(executableDirectory, fileName);
    std::error_code error;
    if (std::filesystem::is_regular_file(candidate, error)) value = candidate;
}

std::string jsonEscape(const std::string& input) {
    std::string output;
    output.reserve(input.size() + 8);
    for (const char value : input) {
        const auto ch = static_cast<unsigned char>(value);
        switch (ch) {
        case '"': output += "\\\""; break;
        case '\\': output += "\\\\"; break;
        case '\b': output += "\\b"; break;
        case '\f': output += "\\f"; break;
        case '\n': output += "\\n"; break;
        case '\r': output += "\\r"; break;
        case '\t': output += "\\t"; break;
        default:
            if (ch < 0x20) {
                constexpr char hex[] = "0123456789ABCDEF";
                output += "\\u00";
                output.push_back(hex[(ch >> 4) & 0xF]);
                output.push_back(hex[ch & 0xF]);
            } else {
                output.push_back(static_cast<char>(ch));
            }
        }
    }
    return output;
}

void printSave(const gdtv::SaveData& save, bool trailingComma) {
    std::cout << "    {\n";
    std::cout << "      \"file\": \"" << jsonEscape(save.path().string()) << "\",\n";
    std::cout << "      \"size\": " << save.fileSize() << ",\n";
    std::cout << "      \"root_offset\": " << save.rootOffset() << ",\n";
    std::cout << "      \"records\": " << save.recordCount() << ",\n";
    std::cout << "      \"keys\": " << save.keyCount() << ",\n";
    std::cout << "      \"linked_structures\": " << save.linkedClusters().size() << ",\n";
    std::cout << "      \"vectors\": [\n";
    for (std::size_t i = 0; i < save.vectors().size(); ++i) {
        const auto& vector = save.vectors()[i];
        const auto type = gdtv::valueTypeForVector(vector.number);
        std::cout << "        {\"number\": " << vector.number
                  << ", \"type\": \"" << gdtv::valueTypeName(type) << "\""
                  << ", \"element_size\": " << gdtv::valueTypeSize(type)
                  << ", \"present\": " << (vector.present ? "true" : "false")
                  << ", \"offset\": ";
        if (vector.offset) std::cout << *vector.offset;
        else std::cout << "null";
        std::cout << ", \"records\": " << vector.count
                  << ", \"keys\": " << vector.keys.size() << "}";
        if (i + 1 != save.vectors().size()) std::cout << ',';
        std::cout << '\n';
    }
    std::cout << "      ]\n";
    std::cout << "    }" << (trailingComma ? "," : "") << "\n";
}

std::string csvEscape(const std::string& value) {
    if (value.find_first_of(",\"\r\n") == std::string::npos) return value;
    std::string result = "\"";
    for (const char ch : value) {
        if (ch == '"') result += "\"\"";
        else result.push_back(ch);
    }
    result.push_back('"');
    return result;
}

std::size_t exportRelationshipReport(const std::filesystem::path& path,
                                     const gdtv::SaveData& save,
                                     const gdtv::SectionNameMap& sectionNames) {
    const auto physical = gdtv::buildPhysicalSectionOrder(save);
    const auto families = gdtv::buildRelationshipFamilies(save);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not create relationship report CSV");
    output << "Row Type,Order,Family,Confidence,Key Hex,Locator,Section Name,Vector,"
              "First Record Offset,Last Record Offset,Records,Shared UnitIDs,Evidence\n";

    std::size_t rows = 0U;
    for (std::size_t index = 0U; index < physical.size(); ++index) {
        const auto& section = physical[index];
        const auto* named = sectionNames.find(section.key);
        const auto* group = save.findGroup(section.key);
        output << "Physical," << (index + 1U) << ",,,0x"
               << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << section.key
               << std::dec << ','
               << csvEscape(named ? named->locator : std::string{}) << ','
               << csvEscape(named ? named->name : std::string{}) << ','
               << section.vectorNumber << ",0x" << std::uppercase << std::hex
               << section.firstRecordOffset << ",0x" << section.lastRecordOffset << std::dec << ','
               << section.recordCount << ",,";
        if (group) output << csvEscape(group->indexRanges(12U));
        output << '\n';
        ++rows;
    }

    for (std::size_t familyIndex = 0U; familyIndex < families.size(); ++familyIndex) {
        const auto& family = families[familyIndex];
        for (const auto key : family.keys) {
            const auto* named = sectionNames.find(key);
            const auto* group = save.findGroup(key);
            output << "Family Member," << (familyIndex + 1U) << ','
                   << csvEscape(family.name) << ',' << csvEscape(family.confidence) << ",0x"
                   << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << key
                   << std::dec << ','
                   << csvEscape(named ? named->locator : std::string{}) << ','
                   << csvEscape(named ? named->name : std::string{}) << ','
                   << (group ? group->vectorNumber : 0U) << ",,,"
                   << (group ? group->records.size() : 0U) << ',' << family.recordCount << ','
                   << csvEscape(family.reason) << '\n';
            ++rows;
        }
    }
    if (!output) throw std::runtime_error("could not write complete relationship report CSV");
    return rows;
}

std::size_t exportUnresolvedCandidates(
    const std::filesystem::path& path, const std::vector<std::unique_ptr<gdtv::SaveData>>& saves,
    const gdtv::HashDatabase& hashes) {
    struct CombinedCandidate {
        std::uint64_t occurrences{};
        std::vector<std::string> examples;
    };
    std::map<std::uint32_t, CombinedCandidate> candidates;
    for (std::size_t saveIndex = 0; saveIndex < saves.size(); ++saveIndex) {
        const auto values = saves[saveIndex]->collectUIntValues(6);
        const std::string role = saveIndex == 0U ? "Primary" : "Comparison";
        for (const auto& [value, summary] : values) {
            if (value == 0U || value == 0xFFFFFFFFU || hashes.find(value)) continue;
            auto& combined = candidates[value];
            combined.occurrences += summary.occurrences;
            for (const auto& occurrence : summary.examples) {
                if (combined.examples.size() >= 10U) break;
                const auto* group = saves[saveIndex]->findGroup(occurrence.key);
                std::ostringstream location;
                location << role << " V" << (group ? group->vectorNumber : 7U)
                         << ":0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0')
                         << occurrence.key << std::dec << ':' << occurrence.unitId
                         << '[' << occurrence.elementIndex << ']';
                combined.examples.push_back(location.str());
            }
        }
    }

    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not create unresolved candidate CSV");
    output << "Hash Hex,Raw Little Endian,ID,Display Name,Category,Source,Notes,Occurrences,Example Locations\n";
    for (const auto& [value, candidate] : candidates) {
        std::ostringstream examples;
        for (std::size_t i = 0; i < candidate.examples.size(); ++i) {
            if (i) examples << " | ";
            examples << candidate.examples[i];
        }
        output << gdtv::hashHex(value) << ',' << gdtv::hashRawLittleEndian(value)
               << ",,,,Unresolved UInt scan,"
               << "\"Review before naming; UInt values can also be counters, flags, or bitfields.\","
               << candidate.occurrences << ",\"" << examples.str() << "\"\n";
    }
    if (!output) throw std::runtime_error("could not write complete unresolved candidate CSV");
    return candidates.size();
}

void usage() {
    std::cerr << GDTV_APP_NAME << " CLI v" << GDTV_APP_VERSION << "\n\n"
              << "Usage:\n"
              << "  GBFR-TOOL-CLI [--map sections.csv] [--hash-db GBFR-Hash-Database.txt]\n"
              << "      [--character-map GBFR-Character-Sections.txt]\n"
              << "      [--section-names GBFR-Section-Names.txt]\n"
              << "      [--export-unresolved candidates.csv] [--export-relationships relationships.csv]\n"
              << "      primary [comparison]\n"
              << "  GBFR-TOOL-CLI --hash-string PL2400\n"
              << "  GBFR-TOOL-CLI --find-hash 1BB37EF0 primary [comparison]\n";
}

} // namespace

int main(int argc, char** argv) {
    try {
        std::filesystem::path mapPath;
        std::filesystem::path hashDbPath;
        std::filesystem::path characterMapPath;
        std::filesystem::path sectionNamesPath;
        std::filesystem::path unresolvedExportPath;
        std::filesystem::path relationshipExportPath;
        std::string hashString;
        std::optional<std::uint32_t> findHash;
        std::vector<std::filesystem::path> saves;
        for (int i = 1; i < argc; ++i) {
            const std::string arg = argv[i];
            if (arg == "--map" || arg == "--hash-db" || arg == "--character-map" ||
                arg == "--section-names" || arg == "--export-unresolved" ||
                arg == "--export-relationships" || arg == "--hash-string" ||
                arg == "--find-hash") {
                if (i + 1 >= argc) {
                    usage();
                    return 2;
                }
                const std::string value = argv[++i];
                if (arg == "--map") mapPath = value;
                else if (arg == "--hash-db") hashDbPath = value;
                else if (arg == "--character-map") characterMapPath = value;
                else if (arg == "--section-names") sectionNamesPath = value;
                else if (arg == "--export-unresolved") unresolvedExportPath = value;
                else if (arg == "--export-relationships") relationshipExportPath = value;
                else if (arg == "--hash-string") hashString = value;
                else {
                    findHash = gdtv::parseHashValue(value, false);
                    if (!findHash) findHash = gdtv::parseHashValue(value, true);
                    if (!findHash) {
                        std::cerr << "Invalid hash value: " << value << '\n';
                        return 2;
                    }
                }
            } else if (arg == "--scan-only") {
                // Kept for compatibility with the Python prototype.
            } else {
                saves.emplace_back(arg);
            }
        }

        const auto bundledDirectory = executableDirectoryFromArgv0(argc > 0 ? argv[0] : nullptr);
        useBundledDefault(mapPath, bundledDirectory, "GameData-Section-Cross-Reference.csv");
        useBundledDefault(hashDbPath, bundledDirectory, "GBFR-Hash-Database.txt");
        useBundledDefault(characterMapPath, bundledDirectory, "GBFR-Character-Sections.txt");
        useBundledDefault(sectionNamesPath, bundledDirectory, "GBFR-Section-Names.txt");

        if (!hashString.empty()) {
            const auto hash = gdtv::xxHash32Custom(hashString);
            std::cout << hashString << "\n"
                      << "Canonical: 0x" << gdtv::hashHex(hash) << "\n"
                      << "Raw little-endian: " << gdtv::hashRawLittleEndian(hash) << "\n";
            return 0;
        }

        if (saves.empty() || saves.size() > 2) {
            usage();
            return 2;
        }

        gdtv::SectionMap sectionMap;
        std::size_t mappedSections = 0;
        if (!mapPath.empty()) mappedSections = sectionMap.load(mapPath);

        gdtv::HashDatabase hashDatabase;
        if (!hashDbPath.empty()) hashDatabase.loadDatabase(hashDbPath);

        gdtv::CharacterSectionMap characterSections;
        if (!characterMapPath.empty()) characterSections.load(characterMapPath);

        gdtv::SectionNameMap sectionNames;
        if (!sectionNamesPath.empty()) sectionNames.load(sectionNamesPath);

        std::vector<std::unique_ptr<gdtv::SaveData>> parsed;
        for (const auto& path : saves) parsed.push_back(std::make_unique<gdtv::SaveData>(path));

        std::optional<std::size_t> unresolvedCandidateCount;
        std::optional<std::size_t> relationshipReportRows;
        if (!unresolvedExportPath.empty()) {
            unresolvedCandidateCount = exportUnresolvedCandidates(unresolvedExportPath, parsed, hashDatabase);
        }
        if (!relationshipExportPath.empty()) {
            relationshipReportRows = exportRelationshipReport(relationshipExportPath, *parsed.front(), sectionNames);
        }

        std::cout << "{\n";
        std::cout << "  \"application\": \"" << GDTV_APP_NAME << " v" << GDTV_APP_VERSION << "\",\n";
        std::cout << "  \"mapped_sections\": " << mappedSections << ",\n";
        std::cout << "  \"character_sections\": " << characterSections.size() << ",\n";
        std::cout << "  \"section_names\": " << sectionNames.size() << ",\n";
        std::cout << "  \"hash_database\": {\n"
                  << "    \"rows\": " << hashDatabase.databaseEntryCount() << ",\n"
                  << "    \"unique_hashes\": " << hashDatabase.uniqueHashCount() << ",\n"
                  << "    \"internal_name_rows\": " << hashDatabase.baseEntryCount() << ",\n"
                  << "    \"verified_internal_names\": " << hashDatabase.verifiedBaseCount() << ",\n"
                  << "    \"internal_name_mismatches\": " << hashDatabase.baseMismatchCount() << ",\n"
                  << "    \"in_game_name_rows\": " << hashDatabase.friendlyEntryCount() << ",\n"
                  << "    \"endian_mismatches\": " << hashDatabase.endianMismatchCount() << ",\n"
                  << "    \"invalid_lines\": " << hashDatabase.invalidLineCount() << "\n"
                  << "  },\n";
        if (relationshipReportRows) {
            std::cout << "  \"relationship_report\": {\n"
                      << "    \"file\": \"" << jsonEscape(relationshipExportPath.string()) << "\",\n"
                      << "    \"rows\": " << *relationshipReportRows << "\n"
                      << "  },\n";
        }
        if (unresolvedCandidateCount) {
            std::cout << "  \"unresolved_candidate_export\": {\n"
                      << "    \"file\": \"" << jsonEscape(unresolvedExportPath.string()) << "\",\n"
                      << "    \"candidates\": " << *unresolvedCandidateCount << "\n"
                      << "  },\n";
        }
        std::cout << "  \"saves\": [\n";
        for (std::size_t i = 0; i < parsed.size(); ++i) printSave(*parsed[i], i + 1 != parsed.size());
        std::cout << "  ]";

        if (findHash) {
            std::cout << ",\n  \"hash_occurrences\": {\n"
                      << "    \"hash\": \"0x" << gdtv::hashHex(*findHash) << "\",\n"
                      << "    \"raw_little_endian\": \"" << gdtv::hashRawLittleEndian(*findHash) << "\",\n";
            if (const auto* preferred = hashDatabase.preferred(*findHash)) {
                std::cout << "    \"display_name\": \"" << jsonEscape(preferred->displayName) << "\",\n"
                          << "    \"category\": \"" << jsonEscape(preferred->category) << "\",\n";
                const auto ids = hashDatabase.idsForHash(*findHash);
                std::cout << "    \"ids\": [";
                for (std::size_t i = 0; i < ids.size(); ++i) {
                    if (i) std::cout << ',';
                    std::cout << '"' << jsonEscape(ids[i]) << '"';
                }
                std::cout << "],\n";
            }
            std::cout << "    \"saves\": [\n";
            for (std::size_t saveIndex = 0; saveIndex < parsed.size(); ++saveIndex) {
                const auto occurrences = parsed[saveIndex]->findUIntValue(*findHash);
                std::cout << "      {\"file\": \"" << jsonEscape(parsed[saveIndex]->path().string())
                          << "\", \"count\": " << occurrences.size() << ", \"items\": [";
                const auto shown = std::min<std::size_t>(occurrences.size(), 200);
                for (std::size_t i = 0; i < shown; ++i) {
                    const auto& occurrence = occurrences[i];
                    if (i) std::cout << ',';
                    std::cout << "{\"key\": " << occurrence.key
                              << ", \"key_hex\": \"0x" << std::uppercase << std::hex
                              << std::setw(4) << std::setfill('0') << occurrence.key << std::dec
                              << std::setfill(' ') << "\", \"unit_id\": " << occurrence.unitId
                              << ", \"element_index\": " << occurrence.elementIndex << '}';
                }
                std::cout << "]}";
                if (saveIndex + 1U != parsed.size()) std::cout << ',';
                std::cout << '\n';
            }
            std::cout << "    ]\n  }";
        }

        if (parsed.size() == 2) {
            std::set<std::uint32_t> keys;
            for (const auto& [key, group] : parsed[0]->groupsByKey()) { (void)group; keys.insert(key); }
            for (const auto& [key, group] : parsed[1]->groupsByKey()) { (void)group; keys.insert(key); }
            std::map<std::string, std::uint64_t> statuses;
            for (const auto key : keys) ++statuses[gdtv::compareKey(parsed[0].get(), parsed[1].get(), key).status];
            std::cout << ",\n  \"comparison_status_counts\": {\n";
            std::size_t index = 0;
            for (const auto& [status, count] : statuses) {
                std::cout << "    \"" << jsonEscape(status) << "\": " << count;
                if (++index != statuses.size()) std::cout << ',';
                std::cout << '\n';
            }
            std::cout << "  }\n";
        } else {
            std::cout << '\n';
        }
        std::cout << "}\n";
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "Error: " << error.what() << '\n';
        return 1;
    }
}
