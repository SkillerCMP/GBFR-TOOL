#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace gdtv {

constexpr std::size_t kDefaultPageSize = 200;
constexpr std::size_t kHexPreviewLimit = 0x4000;

class FbError final : public std::runtime_error {
public:
    using std::runtime_error::runtime_error;
};

enum class ValueType { Bool, Byte, UByte, Short, UShort, Int, UInt, Long, ULong, Float };

[[nodiscard]] ValueType valueTypeForVector(std::uint32_t vectorNumber);
[[nodiscard]] std::string_view valueTypeName(ValueType type) noexcept;
[[nodiscard]] std::size_t valueTypeSize(ValueType type) noexcept;

struct Record {
    std::uint32_t vectorIndex{};
    std::uint32_t index{};
    bool indexFieldPresent{};
    std::uint32_t recordOffset{};
    std::optional<std::uint32_t> payloadOffset;
    std::uint32_t elementCount{};
    std::uint64_t payloadByteLength{};
};

struct KeyGroup {
    std::uint32_t vectorNumber{};
    std::uint32_t key{};
    std::vector<Record> records;

    [[nodiscard]] std::string keyHex() const;
    [[nodiscard]] std::string stableLocator() const;
    [[nodiscard]] std::string indexRanges(std::size_t maxRanges = 30) const;
    [[nodiscard]] ValueType valueType() const;
    [[nodiscard]] std::size_t elementSize() const;
    [[nodiscard]] std::string elementCounts() const;
    [[nodiscard]] std::string payloadByteLengths() const;
};

struct UIntValueOccurrence {
    std::uint32_t key{};
    std::size_t recordOrdinal{};
    std::uint32_t unitId{};
    std::uint32_t elementIndex{};
};

struct UIntValueSummary {
    std::uint32_t value{};
    std::uint64_t occurrences{};
    std::vector<UIntValueOccurrence> examples;
};

struct SaveIntegrityInfo {
    bool supported{};
    std::uint32_t selector{};
    std::uint32_t activeIndex{};
    std::uint32_t hashTableOffset{};
    std::uint32_t hashEntryOffset{};
    std::uint64_t oldHash{};
    std::uint64_t newHash{};
};

struct VectorInfo {
    std::uint32_t number{};
    bool present{};
    std::optional<std::uint32_t> offset;
    std::uint32_t count{};
    std::vector<std::uint32_t> keys;
};

class SaveData {
public:
    explicit SaveData(const std::filesystem::path& path);

    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }
    [[nodiscard]] std::string filename() const;
    [[nodiscard]] std::uint64_t fileSize() const noexcept { return data_.size(); }
    [[nodiscard]] std::uint32_t rootOffset() const noexcept { return rootOffset_; }
    [[nodiscard]] std::uint32_t rootVtableOffset() const noexcept { return rootVtableOffset_; }
    [[nodiscard]] std::uint32_t rootScalar0() const noexcept { return rootScalar0_; }
    [[nodiscard]] std::uint64_t recordCount() const noexcept;
    [[nodiscard]] std::size_t keyCount() const noexcept { return groupsByKey_.size(); }
    [[nodiscard]] const std::array<VectorInfo, 10>& vectors() const noexcept { return vectors_; }
    [[nodiscard]] const std::map<std::uint32_t, KeyGroup>& groupsByKey() const noexcept { return groupsByKey_; }
    [[nodiscard]] const KeyGroup* findGroup(std::uint32_t key) const noexcept;
    [[nodiscard]] std::string_view payloadView(const Record& record) const;
    [[nodiscard]] bool payloadEqual(const Record& first, const SaveData& other, const Record& second) const;
    [[nodiscard]] std::vector<UIntValueOccurrence> findUIntValue(std::uint32_t value,
                                                                 std::size_t maxResults = 100000) const;
    [[nodiscard]] std::map<std::uint32_t, UIntValueSummary> collectUIntValues(
        std::size_t maxExamplesPerValue = 8) const;
    [[nodiscard]] const std::vector<std::vector<std::uint32_t>>& linkedClusters(std::uint32_t maxGap = 4) const;
    [[nodiscard]] const Record* findRecord(std::uint32_t key, std::uint32_t unitId,
                                           std::size_t* ordinal = nullptr) const noexcept;
    [[nodiscard]] std::optional<std::uint32_t> elementOffset(std::uint32_t key, std::uint32_t unitId,
                                                             std::uint32_t elementIndex) const;
    [[nodiscard]] std::uint64_t elementBits(std::uint32_t key, std::uint32_t unitId,
                                             std::uint32_t elementIndex) const;
    void setElementBits(std::uint32_t key, std::uint32_t unitId, std::uint32_t elementIndex,
                        std::uint64_t bits);
    [[nodiscard]] bool dirty() const noexcept { return dirty_; }
    [[nodiscard]] std::uint64_t editCount() const noexcept { return editCount_; }
    [[nodiscard]] SaveIntegrityInfo repairIntegrityHash();
    [[nodiscard]] SaveIntegrityInfo saveAs(const std::filesystem::path& path,
                                           bool repairIntegrity = true);

private:
    std::filesystem::path path_;
    std::vector<std::uint8_t> data_;
    std::uint32_t rootOffset_{};
    std::uint32_t rootVtableOffset_{};
    std::uint32_t rootScalar0_{};
    std::array<VectorInfo, 10> vectors_{};
    std::map<std::uint32_t, KeyGroup> groupsByKey_;
    mutable std::optional<std::vector<std::vector<std::uint32_t>>> clusters_;
    bool dirty_{};
    std::uint64_t editCount_{};

    void parse();
};

struct SectionInfo {
    std::string locator;
    std::string stableLocator;
    std::string name;
    std::string stride;
    std::string originalGroup;
    std::string dlcGroup;
    std::string mappingConfidence;
    std::string countAudit;
};

class SectionMap {
public:
    std::size_t load(const std::filesystem::path& path);
    [[nodiscard]] const SectionInfo* find(std::uint32_t key) const noexcept;
    [[nodiscard]] std::size_t size() const noexcept { return rowsByKey_.size(); }
    [[nodiscard]] const std::filesystem::path& sourcePath() const noexcept { return sourcePath_; }

private:
    std::unordered_map<std::uint32_t, SectionInfo> rowsByKey_;
    std::filesystem::path sourcePath_;
};

struct CompareInfo {
    std::string status;
    const KeyGroup* primary{};
    const KeyGroup* compare{};
    std::uint64_t commonRecords{};
    std::uint64_t unchangedPayloads{};
    std::uint64_t changedPayloads{};
    std::uint64_t primaryOnlyRecords{};
    std::uint64_t compareOnlyRecords{};
};

[[nodiscard]] CompareInfo compareKey(const SaveData* primary, const SaveData* compare, std::uint32_t key);
[[nodiscard]] std::optional<std::uint32_t> parseKeyQuery(std::string_view query);
[[nodiscard]] std::string formatHex(std::string_view bytes, std::uint32_t baseOffset = 0,
                                    std::size_t limit = kHexPreviewLimit);
[[nodiscard]] std::string toLowerAscii(std::string value);
[[nodiscard]] std::string formatNumber(std::uint64_t value);

} // namespace gdtv
