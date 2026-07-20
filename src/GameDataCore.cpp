#include "GameDataCore.hpp"

#include <algorithm>
#include <charconv>
#include <cstring>
#include <fstream>
#include <iomanip>
#include <limits>
#include <set>
#include <sstream>
#include <tuple>

namespace gdtv {
namespace {

std::uint16_t readU16(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset > data.size() || data.size() - offset < 2) {
        throw FbError("u16 out of range at 0x" + [&] {
            std::ostringstream out; out << std::hex << std::uppercase << offset; return out.str();
        }());
    }
    const auto value = static_cast<std::uint32_t>(data[offset]) |
                       (static_cast<std::uint32_t>(data[offset + 1]) << 8U);
    return static_cast<std::uint16_t>(value);
}

std::uint32_t readU32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    if (offset > data.size() || data.size() - offset < 4) {
        throw FbError("u32 out of range at 0x" + [&] {
            std::ostringstream out; out << std::hex << std::uppercase << offset; return out.str();
        }());
    }
    return static_cast<std::uint32_t>(data[offset]) |
           (static_cast<std::uint32_t>(data[offset + 1]) << 8U) |
           (static_cast<std::uint32_t>(data[offset + 2]) << 16U) |
           (static_cast<std::uint32_t>(data[offset + 3]) << 24U);
}

std::int32_t readI32(const std::vector<std::uint8_t>& data, std::size_t offset) {
    const auto value = readU32(data, offset);
    std::int32_t signedValue{};
    static_assert(sizeof(signedValue) == sizeof(value));
    std::memcpy(&signedValue, &value, sizeof(value));
    return signedValue;
}

struct TableFields {
    std::uint32_t vtable{};
    std::uint16_t objectSize{};
    std::vector<std::uint16_t> fields;
};

TableFields tableFields(const std::vector<std::uint8_t>& data, std::uint32_t tableOffset) {
    if (tableOffset >= data.size()) {
        throw FbError("table offset outside file");
    }
    const auto back = readI32(data, tableOffset);
    const auto vtableSigned = static_cast<std::int64_t>(tableOffset) - back;
    if (vtableSigned < 0 || static_cast<std::uint64_t>(vtableSigned) + 4U > data.size()) {
        throw FbError("invalid vtable for table");
    }
    const auto vtable = static_cast<std::uint32_t>(vtableSigned);
    const auto vtableSize = readU16(data, vtable);
    const auto objectSize = readU16(data, vtable + 2U);
    if (vtableSize < 4 || (vtableSize % 2) != 0 ||
        static_cast<std::uint64_t>(vtable) + vtableSize > data.size()) {
        throw FbError("invalid vtable size");
    }
    TableFields result;
    result.vtable = vtable;
    result.objectSize = objectSize;
    const auto fieldCount = static_cast<std::size_t>((vtableSize - 4U) / 2U);
    result.fields.reserve(fieldCount);
    for (std::size_t i = 0; i < fieldCount; ++i) {
        result.fields.push_back(readU16(data, vtable + 4U + i * 2U));
    }
    return result;
}

std::optional<std::uint32_t> fieldPosition(std::uint32_t tableOffset,
                                           const std::vector<std::uint16_t>& fields,
                                           std::size_t fieldNumber) {
    if (fieldNumber >= fields.size() || fields[fieldNumber] == 0) {
        return std::nullopt;
    }
    return tableOffset + fields[fieldNumber];
}

std::vector<std::string> parseCsvLine(const std::string& line) {
    std::vector<std::string> fields;
    std::string current;
    bool quoted = false;
    for (std::size_t i = 0; i < line.size(); ++i) {
        const char ch = line[i];
        if (quoted) {
            if (ch == '"') {
                if (i + 1 < line.size() && line[i + 1] == '"') {
                    current.push_back('"');
                    ++i;
                } else {
                    quoted = false;
                }
            } else {
                current.push_back(ch);
            }
        } else if (ch == '"') {
            quoted = true;
        } else if (ch == ',') {
            fields.push_back(current);
            current.clear();
        } else {
            current.push_back(ch);
        }
    }
    fields.push_back(current);
    return fields;
}

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1);
}

std::optional<std::uint32_t> parseUnsigned(std::string text, int base = 0) {
    text = trim(std::move(text));
    if (text.empty()) return std::nullopt;
    try {
        std::size_t used = 0;
        const auto value = std::stoull(text, &used, base);
        if (used != text.size() || value > std::numeric_limits<std::uint32_t>::max()) {
            return std::nullopt;
        }
        return static_cast<std::uint32_t>(value);
    } catch (...) {
        return std::nullopt;
    }
}

std::string keyHexValue(std::uint32_t key) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(4) << std::setfill('0') << key;
    return out.str();
}

std::uint64_t readU64(const std::vector<std::uint8_t>& data, std::size_t offset) {
    const auto low = static_cast<std::uint64_t>(readU32(data, offset));
    const auto high = static_cast<std::uint64_t>(readU32(data, offset + 4U));
    return low | (high << 32U);
}

void writeUnsigned(std::vector<std::uint8_t>& data, std::size_t offset,
                   std::uint64_t value, std::size_t size) {
    if (size == 0U || size > 8U || offset > data.size() || data.size() - offset < size) {
        throw FbError("write outside file");
    }
    for (std::size_t i = 0; i < size; ++i) {
        data[offset + i] = static_cast<std::uint8_t>((value >> (i * 8U)) & 0xFFU);
    }
}

constexpr std::uint64_t kXxhPrime1 = 11400714785074694791ULL;
constexpr std::uint64_t kXxhPrime2 = 14029467366897019727ULL;
constexpr std::uint64_t kXxhPrime3 = 1609587929392839161ULL;
constexpr std::uint64_t kXxhPrime4 = 9650029242287828579ULL;
constexpr std::uint64_t kXxhPrime5 = 2870177450012600261ULL;

std::uint64_t rotateLeft(std::uint64_t value, unsigned bits) noexcept {
    return (value << bits) | (value >> (64U - bits));
}

std::uint64_t xxhRound(std::uint64_t accumulator, std::uint64_t input) noexcept {
    accumulator += input * kXxhPrime2;
    accumulator = rotateLeft(accumulator, 31U);
    accumulator *= kXxhPrime1;
    return accumulator;
}

std::uint64_t xxhMergeRound(std::uint64_t accumulator, std::uint64_t value) noexcept {
    accumulator ^= xxhRound(0U, value);
    accumulator = accumulator * kXxhPrime1 + kXxhPrime4;
    return accumulator;
}

std::uint64_t xxHash64(const std::uint8_t* input, std::size_t length, std::uint64_t seed) noexcept {
    const auto* cursor = input;
    const auto* const end = input + length;
    std::uint64_t hash{};

    if (length >= 32U) {
        std::uint64_t v1 = seed + kXxhPrime1 + kXxhPrime2;
        std::uint64_t v2 = seed + kXxhPrime2;
        std::uint64_t v3 = seed;
        std::uint64_t v4 = seed - kXxhPrime1;
        const auto* const limit = end - 32U;
        do {
            std::uint64_t lane{};
            std::memcpy(&lane, cursor, sizeof(lane));
            v1 = xxhRound(v1, lane);
            cursor += 8U;
            std::memcpy(&lane, cursor, sizeof(lane));
            v2 = xxhRound(v2, lane);
            cursor += 8U;
            std::memcpy(&lane, cursor, sizeof(lane));
            v3 = xxhRound(v3, lane);
            cursor += 8U;
            std::memcpy(&lane, cursor, sizeof(lane));
            v4 = xxhRound(v4, lane);
            cursor += 8U;
        } while (cursor <= limit);
        hash = rotateLeft(v1, 1U) + rotateLeft(v2, 7U) + rotateLeft(v3, 12U) + rotateLeft(v4, 18U);
        hash = xxhMergeRound(hash, v1);
        hash = xxhMergeRound(hash, v2);
        hash = xxhMergeRound(hash, v3);
        hash = xxhMergeRound(hash, v4);
    } else {
        hash = seed + kXxhPrime5;
    }

    hash += static_cast<std::uint64_t>(length);
    while (cursor + 8U <= end) {
        std::uint64_t lane{};
        std::memcpy(&lane, cursor, sizeof(lane));
        const auto mixed = xxhRound(0U, lane);
        hash ^= mixed;
        hash = rotateLeft(hash, 27U) * kXxhPrime1 + kXxhPrime4;
        cursor += 8U;
    }
    if (cursor + 4U <= end) {
        std::uint32_t lane{};
        std::memcpy(&lane, cursor, sizeof(lane));
        hash ^= static_cast<std::uint64_t>(lane) * kXxhPrime1;
        hash = rotateLeft(hash, 23U) * kXxhPrime2 + kXxhPrime3;
        cursor += 4U;
    }
    while (cursor < end) {
        hash ^= static_cast<std::uint64_t>(*cursor) * kXxhPrime5;
        hash = rotateLeft(hash, 11U) * kXxhPrime1;
        ++cursor;
    }

    hash ^= hash >> 33U;
    hash *= kXxhPrime2;
    hash ^= hash >> 29U;
    hash *= kXxhPrime3;
    hash ^= hash >> 32U;
    return hash;
}

} // namespace

ValueType valueTypeForVector(std::uint32_t vectorNumber) {
    switch (vectorNumber) {
    case 1: return ValueType::Bool;
    case 2: return ValueType::Byte;
    case 3: return ValueType::UByte;
    case 4: return ValueType::Short;
    case 5: return ValueType::UShort;
    case 6: return ValueType::Int;
    case 7: return ValueType::UInt;
    case 8: return ValueType::Long;
    case 9: return ValueType::ULong;
    case 10: return ValueType::Float;
    default: throw FbError("invalid root vector number");
    }
}

std::string_view valueTypeName(ValueType type) noexcept {
    switch (type) {
    case ValueType::Bool: return "Bool";
    case ValueType::Byte: return "Byte";
    case ValueType::UByte: return "UByte";
    case ValueType::Short: return "Short";
    case ValueType::UShort: return "UShort";
    case ValueType::Int: return "Int";
    case ValueType::UInt: return "UInt";
    case ValueType::Long: return "Long";
    case ValueType::ULong: return "ULong";
    case ValueType::Float: return "Float";
    }
    return "Unknown";
}

std::size_t valueTypeSize(ValueType type) noexcept {
    switch (type) {
    case ValueType::Bool:
    case ValueType::Byte:
    case ValueType::UByte: return 1;
    case ValueType::Short:
    case ValueType::UShort: return 2;
    case ValueType::Int:
    case ValueType::UInt:
    case ValueType::Float: return 4;
    case ValueType::Long:
    case ValueType::ULong: return 8;
    }
    return 1;
}

std::string KeyGroup::keyHex() const { return keyHexValue(key); }

std::string KeyGroup::stableLocator() const {
    return "V" + std::to_string(vectorNumber) + ":" + keyHex();
}

std::string KeyGroup::indexRanges(std::size_t maxRanges) const {
    if (records.empty()) return {};
    std::vector<std::uint32_t> values;
    values.reserve(records.size());
    for (const auto& record : records) values.push_back(record.index);
    std::sort(values.begin(), values.end());

    std::vector<std::pair<std::uint32_t, std::uint32_t>> ranges;
    auto start = values.front();
    auto end = start;
    for (std::size_t i = 1; i < values.size(); ++i) {
        if (values[i] == end + 1U) {
            end = values[i];
        } else {
            ranges.emplace_back(start, end);
            start = end = values[i];
        }
    }
    ranges.emplace_back(start, end);

    std::ostringstream out;
    const auto shown = std::min(maxRanges, ranges.size());
    for (std::size_t i = 0; i < shown; ++i) {
        if (i) out << ", ";
        if (ranges[i].first == ranges[i].second) out << ranges[i].first;
        else out << ranges[i].first << '-' << ranges[i].second;
    }
    if (ranges.size() > shown) out << ", ... (+" << (ranges.size() - shown) << " ranges)";
    return out.str();
}

ValueType KeyGroup::valueType() const { return valueTypeForVector(vectorNumber); }

std::size_t KeyGroup::elementSize() const { return valueTypeSize(valueType()); }

std::string KeyGroup::elementCounts() const {
    std::set<std::uint32_t> lengths;
    for (const auto& record : records) lengths.insert(record.elementCount);
    std::ostringstream out;
    bool first = true;
    for (const auto value : lengths) {
        if (!first) out << ", ";
        first = false;
        out << value;
    }
    return out.str();
}

std::string KeyGroup::payloadByteLengths() const {
    std::set<std::uint64_t> lengths;
    for (const auto& record : records) lengths.insert(record.payloadByteLength);
    std::ostringstream out;
    bool first = true;
    for (const auto value : lengths) {
        if (!first) out << ", ";
        first = false;
        out << value;
    }
    return out.str();
}

SaveData::SaveData(const std::filesystem::path& path) : path_(path) {
    std::ifstream input(path_, std::ios::binary);
    if (!input) throw FbError("could not open save file");
    input.seekg(0, std::ios::end);
    const auto length = input.tellg();
    if (length < 0) throw FbError("could not determine save size");
    data_.resize(static_cast<std::size_t>(length));
    input.seekg(0, std::ios::beg);
    if (!data_.empty()) input.read(reinterpret_cast<char*>(data_.data()), static_cast<std::streamsize>(data_.size()));
    if (!input && !data_.empty()) throw FbError("could not read complete save file");
    parse();
}

std::string SaveData::filename() const { return path_.filename().string(); }

std::uint64_t SaveData::recordCount() const noexcept {
    std::uint64_t count = 0;
    for (const auto& vector : vectors_) count += vector.count;
    return count;
}

const KeyGroup* SaveData::findGroup(std::uint32_t key) const noexcept {
    const auto it = groupsByKey_.find(key);
    return it == groupsByKey_.end() ? nullptr : &it->second;
}

std::string_view SaveData::payloadView(const Record& record) const {
    if (!record.payloadOffset || record.payloadByteLength == 0) return {};
    const auto offset = static_cast<std::size_t>(*record.payloadOffset);
    if (record.payloadByteLength > std::numeric_limits<std::size_t>::max()) {
        throw FbError("payload is too large for this platform");
    }
    const auto byteLength = static_cast<std::size_t>(record.payloadByteLength);
    if (offset > data_.size() || data_.size() - offset < byteLength) {
        throw FbError("payload view outside file");
    }
    return {reinterpret_cast<const char*>(data_.data() + offset), byteLength};
}

bool SaveData::payloadEqual(const Record& first, const SaveData& other, const Record& second) const {
    if (first.payloadByteLength != second.payloadByteLength) return false;
    return payloadView(first) == other.payloadView(second);
}

std::vector<UIntValueOccurrence> SaveData::findUIntValue(std::uint32_t value, std::size_t maxResults) const {
    std::vector<UIntValueOccurrence> result;
    for (const auto& [key, group] : groupsByKey_) {
        if (group.valueType() != ValueType::UInt) continue;
        for (std::size_t recordOrdinal = 0; recordOrdinal < group.records.size(); ++recordOrdinal) {
            const auto& record = group.records[recordOrdinal];
            const auto payload = payloadView(record);
            const auto* bytes = reinterpret_cast<const unsigned char*>(payload.data());
            for (std::uint32_t elementIndex = 0; elementIndex < record.elementCount; ++elementIndex) {
                const auto offset = static_cast<std::size_t>(elementIndex) * 4U;
                const auto candidate = static_cast<std::uint32_t>(bytes[offset]) |
                    (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
                    (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
                    (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
                if (candidate == value) {
                    result.push_back(UIntValueOccurrence{key, recordOrdinal, record.index, elementIndex});
                    if (result.size() >= maxResults) return result;
                }
            }
        }
    }
    return result;
}

std::map<std::uint32_t, UIntValueSummary> SaveData::collectUIntValues(
    std::size_t maxExamplesPerValue) const {
    std::map<std::uint32_t, UIntValueSummary> values;
    for (const auto& [key, group] : groupsByKey_) {
        if (group.valueType() != ValueType::UInt) continue;
        for (std::size_t recordOrdinal = 0; recordOrdinal < group.records.size(); ++recordOrdinal) {
            const auto& record = group.records[recordOrdinal];
            const auto payload = payloadView(record);
            const auto* bytes = reinterpret_cast<const unsigned char*>(payload.data());
            const auto elementCount = payload.size() / sizeof(std::uint32_t);
            for (std::size_t element = 0; element < elementCount; ++element) {
                const auto offset = element * sizeof(std::uint32_t);
                const auto value = static_cast<std::uint32_t>(bytes[offset]) |
                                   (static_cast<std::uint32_t>(bytes[offset + 1U]) << 8U) |
                                   (static_cast<std::uint32_t>(bytes[offset + 2U]) << 16U) |
                                   (static_cast<std::uint32_t>(bytes[offset + 3U]) << 24U);
                auto& summary = values[value];
                summary.value = value;
                ++summary.occurrences;
                if (summary.examples.size() < maxExamplesPerValue) {
                    summary.examples.push_back(UIntValueOccurrence{
                        key, recordOrdinal, record.index, static_cast<std::uint32_t>(element)});
                }
            }
        }
    }
    return values;
}


const Record* SaveData::findRecord(std::uint32_t key, std::uint32_t unitId,
                                   std::size_t* ordinal) const noexcept {
    const auto* group = findGroup(key);
    if (!group) return nullptr;
    const auto it = std::lower_bound(group->records.begin(), group->records.end(), unitId,
        [](const Record& record, std::uint32_t value) { return record.index < value; });
    if (it == group->records.end() || it->index != unitId) return nullptr;
    if (ordinal) *ordinal = static_cast<std::size_t>(std::distance(group->records.begin(), it));
    return &*it;
}

std::optional<std::uint32_t> SaveData::elementOffset(std::uint32_t key, std::uint32_t unitId,
                                                     std::uint32_t elementIndex) const {
    const auto* group = findGroup(key);
    const auto* record = findRecord(key, unitId);
    if (!group || !record || !record->payloadOffset || elementIndex >= record->elementCount) {
        return std::nullopt;
    }
    const auto elementSize = group->elementSize();
    const auto offset64 = static_cast<std::uint64_t>(*record->payloadOffset) +
                          static_cast<std::uint64_t>(elementIndex) * elementSize;
    if (offset64 > std::numeric_limits<std::uint32_t>::max() ||
        offset64 > data_.size() || data_.size() - static_cast<std::size_t>(offset64) < elementSize) {
        throw FbError("element offset outside file");
    }
    return static_cast<std::uint32_t>(offset64);
}

std::uint64_t SaveData::elementBits(std::uint32_t key, std::uint32_t unitId,
                                    std::uint32_t elementIndex) const {
    const auto* group = findGroup(key);
    const auto offset = elementOffset(key, unitId, elementIndex);
    if (!group || !offset) throw FbError("element was not found");
    const auto size = group->elementSize();
    std::uint64_t result = 0;
    for (std::size_t i = 0; i < size; ++i) {
        result |= static_cast<std::uint64_t>(data_[static_cast<std::size_t>(*offset) + i]) << (i * 8U);
    }
    return result;
}

void SaveData::setElementBits(std::uint32_t key, std::uint32_t unitId,
                              std::uint32_t elementIndex, std::uint64_t bits) {
    const auto* group = findGroup(key);
    const auto offset = elementOffset(key, unitId, elementIndex);
    if (!group || !offset) throw FbError("element was not found");
    const auto size = group->elementSize();
    if (size < 8U) {
        const auto maximum = (std::uint64_t{1} << (size * 8U)) - 1U;
        if (bits > maximum) throw FbError("value does not fit the selected element type");
    }
    const auto old = elementBits(key, unitId, elementIndex);
    if (old == bits) return;
    writeUnsigned(data_, *offset, bits, size);
    dirty_ = true;
    ++editCount_;
}

SaveIntegrityInfo SaveData::repairIntegrityHash() {
    SaveIntegrityInfo result;
    constexpr std::uint64_t hashSeed = 0x2F1A43EBCDULL;
    constexpr std::array<std::pair<std::uint32_t, std::uint32_t>, 10> sections{{
        {0x58U, 0x80U}, {0x30U, 0xA0U}, {0x28U, 0x30U}, {0x38U, 0xC0U}, {0x40U, 0xB0U},
        {0x68U, 0x50U}, {0x48U, 0x60U}, {0x70U, 0x90U}, {0x50U, 0x40U}, {0x60U, 0x70U}
    }};

    const auto* seedGroup = findGroup(1003U);
    const auto* seedRecord = findRecord(1003U, 0U);
    if (!seedGroup || seedGroup->valueType() != ValueType::UInt || !seedRecord ||
        seedRecord->elementCount == 0U || data_.size() < 0x14U) {
        return result;
    }

    result.selector = static_cast<std::uint32_t>(elementBits(1003U, 0U, 0U));
    result.activeIndex = result.selector % 10U;
    result.hashTableOffset = readU32(data_, data_.size() - 0x14U);
    const auto [startOffset, subSize] = sections[result.activeIndex];
    if (result.hashTableOffset < subSize || result.hashTableOffset > data_.size()) {
        throw FbError("invalid save hash table offset");
    }
    const auto regionEnd = static_cast<std::size_t>(result.hashTableOffset - subSize);
    if (startOffset > regionEnd || regionEnd > data_.size()) {
        throw FbError("invalid save hash region");
    }
    const auto entry64 = static_cast<std::uint64_t>(result.hashTableOffset) +
                         static_cast<std::uint64_t>(result.activeIndex) * 8U;
    if (entry64 > data_.size() || data_.size() - static_cast<std::size_t>(entry64) < 8U) {
        throw FbError("save hash entry is outside file");
    }
    result.hashEntryOffset = static_cast<std::uint32_t>(entry64);
    result.oldHash = readU64(data_, result.hashEntryOffset);
    result.newHash = xxHash64(data_.data() + startOffset, regionEnd - startOffset, hashSeed);
    writeUnsigned(data_, result.hashEntryOffset, result.newHash, 8U);
    result.supported = true;
    return result;
}

SaveIntegrityInfo SaveData::saveAs(const std::filesystem::path& path, bool repairIntegrity) {
    SaveIntegrityInfo integrity;
    if (repairIntegrity) integrity = repairIntegrityHash();
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) throw FbError("could not create output save file");
    if (!data_.empty()) {
        output.write(reinterpret_cast<const char*>(data_.data()),
                     static_cast<std::streamsize>(data_.size()));
    }
    if (!output) throw FbError("could not write complete output save file");
    path_ = path;
    dirty_ = false;
    return integrity;
}


void SaveData::parse() {
    if (data_.size() < 8) throw FbError("file is too small");
    rootOffset_ = readU32(data_, 0);
    const auto root = tableFields(data_, rootOffset_);
    rootVtableOffset_ = root.vtable;
    if (const auto scalar = fieldPosition(rootOffset_, root.fields, 0)) rootScalar0_ = readU32(data_, *scalar);

    for (std::uint32_t vectorNumber = 1; vectorNumber <= 10; ++vectorNumber) {
        auto& vectorInfo = vectors_[vectorNumber - 1U];
        vectorInfo.number = vectorNumber;
        const auto vectorField = fieldPosition(rootOffset_, root.fields, vectorNumber);
        if (!vectorField) {
            vectorInfo.present = false;
            continue;
        }

        const auto vectorOffset64 = static_cast<std::uint64_t>(*vectorField) + readU32(data_, *vectorField);
        if (vectorOffset64 + 4U > data_.size()) throw FbError("root vector points outside file");
        const auto vectorOffset = static_cast<std::uint32_t>(vectorOffset64);
        const auto count = readU32(data_, vectorOffset);
        if (static_cast<std::uint64_t>(vectorOffset) + 4U + static_cast<std::uint64_t>(count) * 4U > data_.size()) {
            throw FbError("root vector element table runs outside file");
        }

        vectorInfo.present = true;
        vectorInfo.offset = vectorOffset;
        vectorInfo.count = count;

        for (std::uint32_t vectorIndex = 0; vectorIndex < count; ++vectorIndex) {
            const auto elementOffset = vectorOffset + 4U + vectorIndex * 4U;
            const auto recordTable64 = static_cast<std::uint64_t>(elementOffset) + readU32(data_, elementOffset);
            if (recordTable64 >= data_.size()) throw FbError("record table points outside file");
            const auto recordTable = static_cast<std::uint32_t>(recordTable64);
            const auto recordFields = tableFields(data_, recordTable);

            const auto keyPosition = fieldPosition(recordTable, recordFields.fields, 0);
            const auto indexPosition = fieldPosition(recordTable, recordFields.fields, 1);
            const auto payloadPosition = fieldPosition(recordTable, recordFields.fields, 2);
            const auto key = keyPosition ? readU32(data_, *keyPosition) : 0U;
            const auto index = indexPosition ? readU32(data_, *indexPosition) : 0U;

            std::optional<std::uint32_t> payloadOffset;
            std::uint32_t elementCount = 0;
            std::uint64_t payloadByteLength = 0;
            if (payloadPosition) {
                const auto payloadVector64 = static_cast<std::uint64_t>(*payloadPosition) + readU32(data_, *payloadPosition);
                if (payloadVector64 + 4U > data_.size()) throw FbError("payload vector points outside file");
                const auto payloadVector = static_cast<std::uint32_t>(payloadVector64);
                elementCount = readU32(data_, payloadVector);
                payloadByteLength = static_cast<std::uint64_t>(elementCount) *
                                    valueTypeSize(valueTypeForVector(vectorNumber));
                const auto bytesOffset64 = payloadVector64 + 4U;
                if (bytesOffset64 + payloadByteLength > data_.size()) throw FbError("payload runs past end of file");
                payloadOffset = static_cast<std::uint32_t>(bytesOffset64);
            }

            auto [groupIt, inserted] = groupsByKey_.try_emplace(key);
            auto& group = groupIt->second;
            if (inserted) {
                group.vectorNumber = vectorNumber;
                group.key = key;
                vectorInfo.keys.push_back(key);
            } else if (group.vectorNumber != vectorNumber) {
                throw FbError("same key occurs in more than one root vector");
            }
            group.records.push_back(Record{vectorIndex, index, indexPosition.has_value(), recordTable,
                                           payloadOffset, elementCount, payloadByteLength});
        }

        std::sort(vectorInfo.keys.begin(), vectorInfo.keys.end());
        for (const auto key : vectorInfo.keys) {
            auto& records = groupsByKey_.at(key).records;
            std::sort(records.begin(), records.end(), [](const Record& a, const Record& b) {
                return std::tie(a.index, a.vectorIndex) < std::tie(b.index, b.vectorIndex);
            });
        }
    }
}

const std::vector<std::vector<std::uint32_t>>& SaveData::linkedClusters(std::uint32_t maxGap) const {
    if (clusters_) return *clusters_;

    std::map<std::vector<std::uint32_t>, std::vector<std::uint32_t>> byIndices;
    for (const auto& [key, group] : groupsByKey_) {
        std::vector<std::uint32_t> indices;
        indices.reserve(group.records.size());
        for (const auto& record : group.records) indices.push_back(record.index);
        byIndices[std::move(indices)].push_back(key);
    }

    std::vector<std::vector<std::uint32_t>> result;
    for (auto& [signature, keys] : byIndices) {
        (void)signature;
        std::sort(keys.begin(), keys.end());
        std::vector<std::uint32_t> current;
        for (const auto key : keys) {
            if (current.empty() || key - current.back() <= maxGap) {
                current.push_back(key);
            } else {
                if (current.size() >= 2) result.push_back(current);
                current.clear();
                current.push_back(key);
            }
        }
        if (current.size() >= 2) result.push_back(std::move(current));
    }
    std::sort(result.begin(), result.end(), [](const auto& a, const auto& b) {
        if (a.front() != b.front()) return a.front() < b.front();
        return a.size() > b.size();
    });
    clusters_ = std::move(result);
    return *clusters_;
}

std::size_t SectionMap::load(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open section map CSV");

    std::string headerLine;
    if (!std::getline(input, headerLine)) throw std::runtime_error("section map CSV is empty");
    if (headerLine.size() >= 3 && static_cast<unsigned char>(headerLine[0]) == 0xEF &&
        static_cast<unsigned char>(headerLine[1]) == 0xBB && static_cast<unsigned char>(headerLine[2]) == 0xBF) {
        headerLine.erase(0, 3);
    }
    const auto headers = parseCsvLine(headerLine);
    std::unordered_map<std::string, std::size_t> positions;
    for (std::size_t i = 0; i < headers.size(); ++i) positions[headers[i]] = i;
    if (!positions.count("Key") || !positions.count("Locator Search")) {
        throw std::runtime_error("section map CSV must contain Key and Locator Search columns");
    }

    auto get = [&](const std::vector<std::string>& row, const char* name) -> std::string {
        const auto it = positions.find(name);
        return (it != positions.end() && it->second < row.size()) ? trim(row[it->second]) : std::string{};
    };

    std::unordered_map<std::uint32_t, SectionInfo> rows;
    std::string line;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        const auto row = parseCsvLine(line);
        const auto key = parseUnsigned(get(row, "Key"), 0);
        if (!key) continue;
        rows[*key] = SectionInfo{
            get(row, "Locator Search"),
            get(row, "Stable Locator"),
            get(row, "Section Name / Notes"),
            get(row, "Stride"),
            get(row, "Original Linked Group(s)"),
            get(row, "DLC Linked Group(s)"),
            get(row, "Mapping Confidence"),
            get(row, "Count Audit")
        };
    }
    rowsByKey_ = std::move(rows);
    sourcePath_ = path;
    return rowsByKey_.size();
}

const SectionInfo* SectionMap::find(std::uint32_t key) const noexcept {
    const auto it = rowsByKey_.find(key);
    return it == rowsByKey_.end() ? nullptr : &it->second;
}

CompareInfo compareKey(const SaveData* primary, const SaveData* compare, std::uint32_t key) {
    const auto* primaryGroup = primary ? primary->findGroup(key) : nullptr;
    const auto* compareGroup = compare ? compare->findGroup(key) : nullptr;
    if (!primaryGroup) {
        return {"Comparison-only key", nullptr, compareGroup, 0, 0, 0, 0,
                compareGroup ? compareGroup->records.size() : 0};
    }
    if (!compareGroup) {
        return {"Primary-only key", primaryGroup, nullptr, 0, 0, 0,
                primaryGroup->records.size(), 0};
    }

    std::size_t p = 0, c = 0;
    std::uint64_t common = 0, changed = 0, primaryOnly = 0, compareOnly = 0;
    while (p < primaryGroup->records.size() && c < compareGroup->records.size()) {
        const auto& left = primaryGroup->records[p];
        const auto& right = compareGroup->records[c];
        if (left.index == right.index) {
            ++common;
            if (!primary->payloadEqual(left, *compare, right)) ++changed;
            ++p; ++c;
        } else if (left.index < right.index) {
            ++primaryOnly; ++p;
        } else {
            ++compareOnly; ++c;
        }
    }
    primaryOnly += primaryGroup->records.size() - p;
    compareOnly += compareGroup->records.size() - c;
    const auto unchanged = common - changed;
    const bool sameIndexSet = primaryOnly == 0 && compareOnly == 0;

    std::string base;
    if (primaryGroup->vectorNumber != compareGroup->vectorNumber) base = "Vector changed";
    else if (!sameIndexSet) base = "Index set changed";
    else base = "Same index set";

    std::string status;
    if (changed) status = base + " + payload changed";
    else if (sameIndexSet && primaryGroup->vectorNumber == compareGroup->vectorNumber) status = "Unchanged";
    else status = base;

    return {status, primaryGroup, compareGroup, common, unchanged, changed, primaryOnly, compareOnly};
}

std::optional<std::uint32_t> parseKeyQuery(std::string_view queryView) {
    std::string value = trim(std::string(queryView));
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::toupper(ch));
    });
    const auto hexPrefix = value.find("0X");
    if (hexPrefix != std::string::npos) {
        std::size_t end = hexPrefix + 2;
        while (end < value.size() && std::isxdigit(static_cast<unsigned char>(value[end]))) ++end;
        return parseUnsigned(value.substr(hexPrefix, end - hexPrefix), 0);
    }
    const bool allHex = !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isxdigit(ch) != 0;
    });
    if (allHex && value.size() == 10) {
        std::array<std::uint8_t, 5> raw{};
        for (std::size_t i = 0; i < 5; ++i) {
            const auto byte = parseUnsigned(value.substr(i * 2, 2), 16);
            if (!byte) return std::nullopt;
            raw[i] = static_cast<std::uint8_t>(*byte);
        }
        if (value.rfind("FE", 0) == 0) {
            return static_cast<std::uint32_t>(raw[0]) |
                   (static_cast<std::uint32_t>(raw[1]) << 8U) |
                   (static_cast<std::uint32_t>(raw[2]) << 16U) |
                   (static_cast<std::uint32_t>(raw[3]) << 24U);
        }
        return static_cast<std::uint32_t>(raw[1]) |
               (static_cast<std::uint32_t>(raw[2]) << 8U) |
               (static_cast<std::uint32_t>(raw[3]) << 16U) |
               (static_cast<std::uint32_t>(raw[4]) << 24U);
    }
    if (allHex && value.size() <= 8 && value.find_first_of("ABCDEF") != std::string::npos) {
        return parseUnsigned(value, 16);
    }
    if (!value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char ch) { return std::isdigit(ch) != 0; })) {
        return parseUnsigned(value, 10);
    }
    return std::nullopt;
}

std::string formatHex(std::string_view bytes, std::uint32_t baseOffset, std::size_t limit) {
    const auto shown = std::min(bytes.size(), limit);
    std::ostringstream out;
    out << std::uppercase << std::hex << std::setfill('0');
    for (std::size_t row = 0; row < shown; row += 16) {
        out << std::setw(8) << (baseOffset + static_cast<std::uint32_t>(row)) << "  ";
        const auto rowLength = std::min<std::size_t>(16, shown - row);
        for (std::size_t i = 0; i < 16; ++i) {
            if (i < rowLength) out << std::setw(2) << static_cast<unsigned>(static_cast<unsigned char>(bytes[row + i])) << ' ';
            else out << "   ";
        }
        out << " ";
        for (std::size_t i = 0; i < rowLength; ++i) {
            const auto ch = static_cast<unsigned char>(bytes[row + i]);
            out << ((ch >= 32 && ch <= 126) ? static_cast<char>(ch) : '.');
        }
        out << '\n';
    }
    if (bytes.size() > shown) out << "\n... preview limited to " << std::dec << shown << " of " << bytes.size() << " bytes ...\n";
    return out.str();
}

std::string toLowerAscii(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string formatNumber(std::uint64_t value) {
    auto text = std::to_string(value);
    for (std::ptrdiff_t pos = static_cast<std::ptrdiff_t>(text.size()) - 3; pos > 0; pos -= 3) {
        text.insert(static_cast<std::size_t>(pos), 1, ',');
    }
    return text;
}

} // namespace gdtv
