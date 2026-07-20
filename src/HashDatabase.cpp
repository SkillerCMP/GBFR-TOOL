#include "HashDatabase.hpp"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <iomanip>
#include <set>
#include <sstream>
#include <stdexcept>
#include <utility>

namespace gdtv {
namespace {

constexpr std::uint32_t kPrime1 = 0x9E3779B1U;
constexpr std::uint32_t kPrime2 = 0x85EBCA77U;
constexpr std::uint32_t kPrime3 = 0xC2B2AE3DU;
constexpr std::uint32_t kPrime4 = 0x27D4EB2FU;
constexpr std::uint32_t kPrime5 = 0x165667B1U;
constexpr std::string_view kHeader =
    "[HASH[BE]\tHASH[LE]\tTYPE\tINGAMENAME\tINTERNALNAME\tVERSION\tNOTE]";
constexpr std::string_view kLegacyHeader =
    "[HASH[BE]|HASH[LE]|TYPE|INGAMENAME|INTERNALNAME|VERSION|NOTE]";

constexpr std::uint32_t rotateLeft(std::uint32_t value, int amount) noexcept {
    return (value << amount) | (value >> (32 - amount));
}

constexpr std::uint32_t round(std::uint32_t seed, std::uint32_t input) noexcept {
    return rotateLeft(seed + input * kPrime2, 13) * kPrime1;
}

std::uint32_t readU32(const unsigned char* data) noexcept {
    return static_cast<std::uint32_t>(data[0]) |
           (static_cast<std::uint32_t>(data[1]) << 8U) |
           (static_cast<std::uint32_t>(data[2]) << 16U) |
           (static_cast<std::uint32_t>(data[3]) << 24U);
}

std::string trim(std::string value) {
    const auto notSpace = [](unsigned char ch) { return !std::isspace(ch); };
    value.erase(value.begin(), std::find_if(value.begin(), value.end(), notSpace));
    value.erase(std::find_if(value.rbegin(), value.rend(), notSpace).base(), value.end());
    return value;
}

std::vector<std::string> splitDatabaseRow(const std::string& line) {
    // TAB is the native v0.26.0 format. Pipe remains accepted so databases
    // created by earlier builds can still be opened and re-saved as TAB text.
    const char separatorCharacter = line.find('\t') != std::string::npos ? '\t' : '|';
    std::vector<std::string> fields;
    std::size_t start = 0;
    while (true) {
        const auto separator = line.find(separatorCharacter, start);
        if (separator == std::string::npos) {
            fields.push_back(trim(line.substr(start)));
            break;
        }
        fields.push_back(trim(line.substr(start, separator - start)));
        start = separator + 1U;
    }
    return fields;
}

bool isNA(std::string_view value) {
    if (value.size() != 2U) return false;
    return (value[0] == 'N' || value[0] == 'n') && (value[1] == 'A' || value[1] == 'a');
}

std::string decodeField(std::string value) {
    value = trim(std::move(value));
    return value.empty() || isNA(value) ? std::string{} : value;
}

std::string encodeField(std::string_view value) {
    if (value.empty()) return "NA";
    std::string result(value);
    std::replace(result.begin(), result.end(), '\t', ' ');
    std::replace(result.begin(), result.end(), '\r', ' ');
    std::replace(result.begin(), result.end(), '\n', ' ');
    result = trim(std::move(result));
    return result.empty() ? "NA" : result;
}


bool isBuiltInGlobalEntry(const HashEntry& entry) noexcept {
    return isGlobalEmptySlotHash(entry.hash) && entry.source == "Built-in";
}

bool categoryMatches(std::string_view filter, std::string_view category) noexcept {
    if (filter.empty()) return false;
    if (filter.back() == '*') {
        filter.remove_suffix(1U);
        return category.size() >= filter.size() && category.compare(0U, filter.size(), filter) == 0;
    }
    return filter == category;
}

void insertEntry(std::unordered_map<std::uint32_t, std::vector<HashEntry>>& map, HashEntry entry) {
    auto& bucket = map[entry.hash];
    const auto same = std::find_if(bucket.begin(), bucket.end(), [&](const HashEntry& existing) {
        return existing.id == entry.id && existing.displayName == entry.displayName &&
               existing.category == entry.category && existing.version == entry.version &&
               existing.notes == entry.notes;
    });
    if (same == bucket.end()) bucket.push_back(std::move(entry));
    else *same = std::move(entry);
}

} // namespace

std::uint32_t xxHash32Custom(std::string_view text) noexcept {
    const auto* begin = reinterpret_cast<const unsigned char*>(text.data());
    const auto* pointer = begin;
    std::size_t remaining = text.size();
    std::uint32_t hash = 0x178A54A4U;

    if (remaining >= 16U) {
        std::uint32_t v1 = 0x2557311BU;
        std::uint32_t v2 = 0x871FB76AU;
        std::uint32_t v3 = 0x0133ECF3U;
        std::uint32_t v4 = 0x62FC7342U;
        do {
            v1 = round(v1, readU32(pointer));
            v2 = round(v2, readU32(pointer + 4));
            v3 = round(v3, readU32(pointer + 8));
            v4 = round(v4, readU32(pointer + 12));
            pointer += 16;
            remaining -= 16U;
        } while (remaining > 16U);
        hash = rotateLeft(v1, 1) + rotateLeft(v2, 7) + rotateLeft(v3, 12) + rotateLeft(v4, 18);
    }

    hash += static_cast<std::uint32_t>(text.size());
    while (remaining >= 4U) {
        hash = rotateLeft(hash + readU32(pointer) * kPrime3, 17) * kPrime4;
        pointer += 4;
        remaining -= 4U;
    }
    while (remaining > 0U) {
        hash = rotateLeft(hash + static_cast<std::uint32_t>(*pointer) * kPrime5, 11) * kPrime1;
        ++pointer;
        --remaining;
    }
    hash ^= hash >> 15U;
    hash *= kPrime2;
    hash ^= hash >> 13U;
    hash *= kPrime3;
    hash ^= hash >> 16U;
    return hash;
}

std::string hashHex(std::uint32_t value) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setw(8) << std::setfill('0') << value;
    return out.str();
}

std::string hashRawLittleEndian(std::uint32_t value) {
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (unsigned int shift = 0; shift < 32U; shift += 8U) {
        out << std::setw(2) << ((value >> shift) & 0xFFU);
    }
    return out.str();
}

std::optional<std::uint32_t> parseHashValue(std::string_view text, bool rawLittleEndian) noexcept {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) text.remove_prefix(1);
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) text.remove_suffix(1);
    if (text.size() >= 3U && (text.substr(0, 3) == "LE:" || text.substr(0, 3) == "le:")) {
        rawLittleEndian = true;
        text.remove_prefix(3);
    }
    if (text.size() >= 2U && (text.substr(0, 2) == "0x" || text.substr(0, 2) == "0X")) text.remove_prefix(2);
    if (text.size() != 8U) return std::nullopt;
    std::uint32_t value{};
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value, 16);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size()) return std::nullopt;
    if (rawLittleEndian) {
        value = ((value & 0x000000FFU) << 24U) |
                ((value & 0x0000FF00U) << 8U) |
                ((value & 0x00FF0000U) >> 8U) |
                ((value & 0xFF000000U) >> 24U);
    }
    return value;
}

HashDatabase::HashDatabase() {
    clear();
}

void HashDatabase::installBuiltInEntries() {
    HashEntry entry;
    entry.hash = kGlobalEmptySlotHash;
    entry.displayName = std::string(kGlobalEmptySlotName);
    entry.category = "Global";
    entry.source = "Built-in";
    entry.builtInFriendly = true;
    insertEntry(entriesByHash_, std::move(entry));
}

void HashDatabase::clear() {
    entriesByHash_.clear();
    databasePath_.clear();
    databaseEntryCount_ = 0;
    baseEntryCount_ = 0;
    friendlyEntryCount_ = 0;
    userEntryCount_ = 0;
    verifiedBaseCount_ = 0;
    baseMismatchCount_ = 0;
    verifiedFriendlyCount_ = 0;
    friendlyMismatchCount_ = 0;
    endianMismatchCount_ = 0;
    invalidLineCount_ = 0;
    installBuiltInEntries();
}

void HashDatabase::recount() {
    databaseEntryCount_ = 0;
    baseEntryCount_ = 0;
    friendlyEntryCount_ = 0;
    userEntryCount_ = 0;
    verifiedBaseCount_ = 0;
    baseMismatchCount_ = 0;
    verifiedFriendlyCount_ = 0;
    friendlyMismatchCount_ = 0;
    for (const auto& pair : entriesByHash_) {
        for (const auto& entry : pair.second) {
            if (isBuiltInGlobalEntry(entry)) continue;
            ++databaseEntryCount_;
            if (!entry.id.empty()) {
                ++baseEntryCount_;
                if (entry.algorithmVerified) ++verifiedBaseCount_;
                else ++baseMismatchCount_;
            }
            if (!entry.displayName.empty()) {
                ++friendlyEntryCount_;
                if (!entry.id.empty()) {
                    if (entry.algorithmVerified) ++verifiedFriendlyCount_;
                    else ++friendlyMismatchCount_;
                }
            }
            if (entry.userDefined) ++userEntryCount_;
        }
    }
}

std::size_t HashDatabase::loadDatabase(const std::filesystem::path& path, bool clearExisting) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open unified hash database text file");
    if (clearExisting) clear();

    std::string line;
    std::size_t loaded = 0;
    std::size_t endianMismatches = 0;
    std::size_t invalidLines = 0;
    bool firstLine = true;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (firstLine && line.size() >= 3U && static_cast<unsigned char>(line[0]) == 0xEFU &&
            static_cast<unsigned char>(line[1]) == 0xBBU && static_cast<unsigned char>(line[2]) == 0xBFU) {
            line.erase(0, 3);
        }
        firstLine = false;
        line = trim(std::move(line));
        if (line.empty() || line == kHeader || line == kLegacyHeader ||
            line.rfind("//", 0) == 0 || line[0] == '#' || line[0] == '\\') continue;
        const auto fields = splitDatabaseRow(line);
        if (fields.size() != 7U) {
            ++invalidLines;
            continue;
        }

        auto internalName = decodeField(fields[4]);
        auto canonical = isNA(fields[0]) ? std::optional<std::uint32_t>{} : parseHashValue(fields[0], false);
        const auto raw = isNA(fields[1]) ? std::optional<std::uint32_t>{} : parseHashValue(fields[1], true);
        if (!canonical && raw) canonical = raw;
        if (!canonical && !internalName.empty()) canonical = xxHash32Custom(internalName);
        if (!canonical) {
            ++invalidLines;
            continue;
        }
        if (raw && *raw != *canonical) ++endianMismatches;

        const auto verified = !internalName.empty() && xxHash32Custom(internalName) == *canonical;
        auto displayName = decodeField(fields[3]);
        HashEntry entry{*canonical,
                        std::move(internalName),
                        std::move(displayName),
                        decodeField(fields[2]),
                        decodeField(fields[5]),
                        path.filename().string(),
                        decodeField(fields[6]),
                        false,
                        verified,
                        false};
        entry.builtInFriendly = !entry.displayName.empty();
        const auto before = entriesByHash_[entry.hash].size();
        insertEntry(entriesByHash_, std::move(entry));
        if (entriesByHash_[*canonical].size() != before) ++loaded;
    }

    databasePath_ = path;
    endianMismatchCount_ += endianMismatches;
    invalidLineCount_ += invalidLines;
    recount();
    return loaded;
}

void HashDatabase::saveDatabase(const std::filesystem::path& path) const {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw std::runtime_error("could not create unified hash database text file");
    output << kHeader << '\n';
    auto entries = allEntries();
    std::sort(entries.begin(), entries.end(), [](const HashEntry& left, const HashEntry& right) {
        if (left.category != right.category) return left.category < right.category;
        if (left.displayName != right.displayName) return left.displayName < right.displayName;
        if (left.id != right.id) return left.id < right.id;
        if (left.version != right.version) return left.version < right.version;
        if (left.notes != right.notes) return left.notes < right.notes;
        return left.hash < right.hash;
    });
    for (const auto& entry : entries) {
        if (isBuiltInGlobalEntry(entry)) continue;
        output << hashHex(entry.hash) << '\t' << hashRawLittleEndian(entry.hash) << '\t'
               << encodeField(entry.category) << '\t' << encodeField(entry.displayName) << '\t'
               << encodeField(entry.id) << '\t' << encodeField(entry.version) << '\t'
               << encodeField(entry.notes) << '\n';
    }
    if (!output) throw std::runtime_error("could not write complete unified hash database text file");
}

void HashDatabase::addOrUpdateUser(HashEntry entry) {
    entry.userDefined = true;
    entry.builtInFriendly = !entry.displayName.empty();
    if (entry.hash == 0U && !entry.id.empty()) entry.hash = xxHash32Custom(entry.id);
    entry.algorithmVerified = !entry.id.empty() && xxHash32Custom(entry.id) == entry.hash;
    auto& bucket = entriesByHash_[entry.hash];
    const auto same = std::find_if(bucket.begin(), bucket.end(), [&](const HashEntry& value) {
        return value.id == entry.id || (!entry.displayName.empty() && value.displayName == entry.displayName);
    });
    if (same != bucket.end()) *same = std::move(entry);
    else bucket.push_back(std::move(entry));
    installBuiltInEntries();
    recount();
}

const std::vector<HashEntry>* HashDatabase::find(std::uint32_t hash) const noexcept {
    const auto it = entriesByHash_.find(hash);
    return it == entriesByHash_.end() ? nullptr : &it->second;
}

const HashEntry* HashDatabase::preferred(std::uint32_t hash) const noexcept {
    const auto* entries = find(hash);
    if (!entries || entries->empty()) return nullptr;
    if (isGlobalEmptySlotHash(hash)) {
        const auto builtIn = std::find_if(entries->begin(), entries->end(), isBuiltInGlobalEntry);
        if (builtIn != entries->end()) return &*builtIn;
    }
    const auto choose = [&](const auto& predicate) -> const HashEntry* {
        const auto it = std::find_if(entries->begin(), entries->end(), predicate);
        return it == entries->end() ? nullptr : &*it;
    };
    if (const auto* entry = choose([](const HashEntry& value) {
            return value.userDefined && !value.displayName.empty();
        })) return entry;
    if (const auto* entry = choose([](const HashEntry& value) {
            return value.builtInFriendly && !value.displayName.empty();
        })) return entry;
    if (const auto* entry = choose([](const HashEntry& value) { return value.userDefined; })) return entry;
    return &entries->front();
}

const HashEntry* HashDatabase::preferredMatching(std::uint32_t hash,
                                                 std::string_view category,
                                                 std::string_view internalPrefix) const noexcept {
    if (isGlobalEmptySlotHash(hash)) return preferred(hash);
    const auto* entries = find(hash);
    if (!entries || entries->empty()) return nullptr;
    const auto matches = [&](const HashEntry& value) {
        const bool categoryMatch = categoryMatches(category, value.category);
        const bool prefixMatch = !internalPrefix.empty() &&
                                 value.id.size() >= internalPrefix.size() &&
                                 value.id.compare(0U, internalPrefix.size(), internalPrefix) == 0;
        return category.empty() && internalPrefix.empty() ? true : categoryMatch || prefixMatch;
    };
    const auto choose = [&](const auto& predicate) -> const HashEntry* {
        const auto it = std::find_if(entries->begin(), entries->end(), [&](const HashEntry& value) {
            return matches(value) && predicate(value);
        });
        return it == entries->end() ? nullptr : &*it;
    };
    if (const auto* entry = choose([](const HashEntry& value) {
            return value.userDefined && !value.displayName.empty();
        })) return entry;
    if (const auto* entry = choose([](const HashEntry& value) {
            return value.builtInFriendly && !value.displayName.empty();
        })) return entry;
    if (const auto* entry = choose([](const HashEntry& value) { return value.userDefined; })) return entry;
    return choose([](const HashEntry&) { return true; });
}

bool HashDatabase::hasMatchingEntry(std::uint32_t hash,
                                    std::string_view category,
                                    std::string_view internalPrefix) const noexcept {
    return preferredMatching(hash, category, internalPrefix) != nullptr;
}

std::vector<HashEntry> HashDatabase::friendlyEntries() const {
    std::vector<HashEntry> result;
    result.reserve(friendlyEntryCount_);
    for (const auto& pair : entriesByHash_) {
        for (const auto& entry : pair.second) if (!entry.displayName.empty()) result.push_back(entry);
    }
    return result;
}

std::vector<HashEntry> HashDatabase::userEntries() const {
    std::vector<HashEntry> result;
    result.reserve(userEntryCount_);
    for (const auto& pair : entriesByHash_) {
        for (const auto& entry : pair.second) if (entry.userDefined) result.push_back(entry);
    }
    return result;
}

std::vector<HashEntry> HashDatabase::allEntries() const {
    std::vector<HashEntry> result;
    result.reserve(databaseEntryCount_);
    for (const auto& pair : entriesByHash_) {
        result.insert(result.end(), pair.second.begin(), pair.second.end());
    }
    return result;
}

std::vector<std::uint32_t> HashDatabase::hashesForText(std::string_view text) const {
    auto normalize = [](std::string_view value) {
        std::string result;
        result.reserve(value.size());
        for (const char ch : value) {
            const auto byte = static_cast<unsigned char>(ch);
            if (!std::isspace(byte)) result.push_back(static_cast<char>(std::tolower(byte)));
        }
        return result;
    };
    const auto wanted = normalize(text);
    std::set<std::uint32_t> matches;
    if (const auto parsed = parseHashValue(text, false)) matches.insert(*parsed);
    if (const auto parsed = parseHashValue(text, true)) matches.insert(*parsed);
    for (const auto& pair : entriesByHash_) {
        for (const auto& entry : pair.second) {
            if ((!entry.id.empty() && normalize(entry.id) == wanted) ||
                (!entry.displayName.empty() && normalize(entry.displayName) == wanted)) {
                matches.insert(pair.first);
            }
        }
    }
    return {matches.begin(), matches.end()};
}

std::vector<std::string> HashDatabase::idsForHash(std::uint32_t hash) const {
    std::set<std::string> unique;
    if (const auto* entries = find(hash)) {
        for (const auto& entry : *entries) if (!entry.id.empty()) unique.insert(entry.id);
    }
    return {unique.begin(), unique.end()};
}

std::vector<std::string> HashDatabase::idsForHashMatching(std::uint32_t hash,
                                                          std::string_view category,
                                                          std::string_view internalPrefix) const {
    if (isGlobalEmptySlotHash(hash)) return idsForHash(hash);
    std::set<std::string> unique;
    if (const auto* entries = find(hash)) {
        for (const auto& entry : *entries) {
            const bool categoryMatch = categoryMatches(category, entry.category);
            const bool prefixMatch = !internalPrefix.empty() &&
                                     entry.id.size() >= internalPrefix.size() &&
                                     entry.id.compare(0U, internalPrefix.size(), internalPrefix) == 0;
            if ((category.empty() && internalPrefix.empty()) || categoryMatch || prefixMatch) {
                if (!entry.id.empty()) unique.insert(entry.id);
            }
        }
    }
    return {unique.begin(), unique.end()};
}

} // namespace gdtv
