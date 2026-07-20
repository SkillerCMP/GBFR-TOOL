#include "SectionNames.hpp"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <fstream>
#include <optional>
#include <set>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace gdtv {
namespace {

constexpr std::string_view kHeader =
    "[IDTYPE\tLOCATOR\tNAME\tSUBSYSTEM\tCONFIDENCE\tOFFICIALSAVEIDTYPE\tSTORAGETYPE\t"
    "HASHCATEGORY\tINTERNALPREFIX\tNOTE\tRECOMMENDEDTEST]";

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

std::vector<std::string> splitTabs(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0U;
    while (start <= line.size()) {
        const auto separator = line.find('\t', start);
        fields.push_back(trim(line.substr(start, separator == std::string::npos
                                                    ? std::string::npos
                                                    : separator - start)));
        if (separator == std::string::npos) break;
        start = separator + 1U;
    }
    return fields;
}

bool isNA(std::string_view value) noexcept {
    if (value.size() != 2U) return false;
    return std::toupper(static_cast<unsigned char>(value[0])) == 'N' &&
           std::toupper(static_cast<unsigned char>(value[1])) == 'A';
}

std::string decodeField(std::string value) {
    return isNA(value) ? std::string{} : std::move(value);
}

std::optional<std::uint32_t> parseKey(std::string_view text) {
    std::string value = trim(std::string(text));
    if (value.empty()) return std::nullopt;
    int base = 10;
    if (value.size() > 2U && value[0] == '0' && (value[1] == 'x' || value[1] == 'X')) {
        value.erase(0, 2U);
        base = 16;
    }
    std::uint32_t result{};
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result, base);
    if (parsed.ec != std::errc{} || parsed.ptr != value.data() + value.size()) return std::nullopt;
    return result;
}

std::string lowerAscii(std::string_view value) {
    std::string result;
    result.reserve(value.size());
    for (const char ch : value) {
        result.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(ch))));
    }
    return result;
}

} // namespace

std::size_t SectionNameMap::load(const std::filesystem::path& path, bool clearExisting) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open section names text file");
    if (clearExisting) entries_.clear();

    std::string line;
    std::size_t loaded = 0U;
    bool firstLine = true;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (firstLine && line.size() >= 3U && static_cast<unsigned char>(line[0]) == 0xEFU &&
            static_cast<unsigned char>(line[1]) == 0xBBU && static_cast<unsigned char>(line[2]) == 0xBFU) {
            line.erase(0, 3U);
        }
        firstLine = false;
        line = trim(std::move(line));
        if (line.empty() || line == kHeader || line.rfind("//", 0U) == 0U || line[0] == '#') continue;

        const auto fields = splitTabs(line);
        if (fields.size() != 11U) continue;
        const auto key = parseKey(fields[0]);
        if (!key) continue;

        SectionNameEntry entry;
        entry.key = *key;
        entry.locator = decodeField(fields[1]);
        entry.name = decodeField(fields[2]);
        entry.subsystem = decodeField(fields[3]);
        entry.confidence = decodeField(fields[4]);
        entry.officialSaveIdType = decodeField(fields[5]);
        entry.storageType = decodeField(fields[6]);
        entry.hashCategory = decodeField(fields[7]);
        entry.internalPrefix = decodeField(fields[8]);
        entry.note = decodeField(fields[9]);
        entry.recommendedTest = decodeField(fields[10]);
        entries_[*key] = std::move(entry);
        ++loaded;
    }
    sourcePath_ = path;
    return loaded;
}

const SectionNameEntry* SectionNameMap::find(std::uint32_t key) const noexcept {
    const auto iterator = entries_.find(key);
    return iterator == entries_.end() ? nullptr : &iterator->second;
}

std::vector<std::string> SectionNameMap::subsystems() const {
    std::set<std::string> unique;
    for (const auto& [key, entry] : entries_) {
        (void)key;
        if (!entry.subsystem.empty()) unique.insert(entry.subsystem);
    }
    return {unique.begin(), unique.end()};
}

std::string confidenceMarker(std::string_view confidence) {
    const auto lower = lowerAscii(confidence);
    if (lower == "confirmed") return {};
    if (lower == "strong") return " [Strong]";
    if (lower == "tentative") return " [?]";
    if (lower == "unknown") return " [Unknown]";
    return confidence.empty() ? std::string{} : " [" + std::string(confidence) + ']';
}

} // namespace gdtv
