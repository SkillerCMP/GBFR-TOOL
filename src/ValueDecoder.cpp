#include "ValueDecoder.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <limits>
#include <sstream>

namespace gdtv {
namespace {

std::uint16_t readU16(const unsigned char* p) noexcept {
    return static_cast<std::uint16_t>(p[0]) |
           static_cast<std::uint16_t>(static_cast<std::uint16_t>(p[1]) << 8U);
}

std::uint32_t readU32(const unsigned char* p) noexcept {
    return static_cast<std::uint32_t>(p[0]) |
           (static_cast<std::uint32_t>(p[1]) << 8U) |
           (static_cast<std::uint32_t>(p[2]) << 16U) |
           (static_cast<std::uint32_t>(p[3]) << 24U);
}

std::uint64_t readU64(const unsigned char* p) noexcept {
    return static_cast<std::uint64_t>(readU32(p)) |
           (static_cast<std::uint64_t>(readU32(p + 4)) << 32U);
}

template <typename To, typename From>
To bitCopy(From value) noexcept {
    static_assert(sizeof(To) == sizeof(From));
    To result{};
    std::memcpy(&result, &value, sizeof(result));
    return result;
}

std::string hexValue(std::uint64_t value, int width) {
    std::ostringstream out;
    out << "0x" << std::uppercase << std::hex << std::setw(width) << std::setfill('0') << value;
    return out.str();
}

} // namespace

DecodedValueSummary decodeValues(std::string_view payload, ValueType type,
                                 const HashDatabase* hashes, std::size_t maxElements,
                                 const HashResolutionFilter* filter) {
    DecodedValueSummary result;
    const auto size = valueTypeSize(type);
    if (size == 0U) return result;
    const auto total = payload.size() / size;
    result.shown = std::min(total, maxElements);

    std::ostringstream out;
    out << "Type: " << valueTypeName(type) << "\r\n"
        << "Element size: " << size << " bytes\r\n"
        << "Elements: " << total << "\r\n";
    if (total > result.shown) out << "Showing first " << result.shown << " elements\r\n";
    out << "\r\n";

    const auto* data = reinterpret_cast<const unsigned char*>(payload.data());
    for (std::size_t i = 0; i < result.shown; ++i) {
        const auto* p = data + i * size;
        out << '[' << std::setw(6) << std::setfill('0') << std::dec << i << "] ";
        switch (type) {
        case ValueType::Bool:
            out << (p[0] == 0U ? "false" : "true") << " (" << static_cast<unsigned>(p[0]) << ')';
            break;
        case ValueType::Byte:
            out << static_cast<int>(static_cast<std::int8_t>(p[0])) << " / " << hexValue(p[0], 2);
            break;
        case ValueType::UByte:
            out << static_cast<unsigned>(p[0]) << " / " << hexValue(p[0], 2);
            break;
        case ValueType::Short: {
            const auto raw = readU16(p);
            out << bitCopy<std::int16_t>(raw) << " / " << hexValue(raw, 4);
            break;
        }
        case ValueType::UShort: {
            const auto raw = readU16(p);
            out << raw << " / " << hexValue(raw, 4);
            break;
        }
        case ValueType::Int: {
            const auto raw = readU32(p);
            out << bitCopy<std::int32_t>(raw) << " / " << hexValue(raw, 8);
            break;
        }
        case ValueType::UInt: {
            const auto raw = readU32(p);
            out << raw << " / " << hexValue(raw, 8) << " / raw " << hashRawLittleEndian(raw);
            if (hashes) {
                const bool nullOrEmptySlot = raw == 0U || isGlobalEmptySlotHash(raw) ||
                                             raw == std::numeric_limits<std::uint32_t>::max();
                const bool hasFilter = filter && !nullOrEmptySlot &&
                                       (!filter->category.empty() || !filter->internalPrefix.empty());
                const auto* preferred = hasFilter
                    ? hashes->preferredMatching(raw, filter->category, filter->internalPrefix)
                    : hashes->preferred(raw);
                bool mismatch = false;
                if (!preferred && hasFilter) {
                    preferred = hashes->preferred(raw);
                    mismatch = preferred != nullptr;
                }
                if (preferred) {
                    ++result.resolvedHashes;
                    if (mismatch) ++result.filterMismatches;
                    out << "  =>  ";
                    if (!preferred->displayName.empty()) out << preferred->displayName;
                    const auto ids = hasFilter && !mismatch
                        ? hashes->idsForHashMatching(raw, filter->category, filter->internalPrefix)
                        : hashes->idsForHash(raw);
                    if (!ids.empty()) {
                        if (!preferred->displayName.empty()) out << " - ";
                        for (std::size_t idIndex = 0; idIndex < ids.size(); ++idIndex) {
                            if (idIndex) out << " | ";
                            out << ids[idIndex];
                        }
                    } else if (!preferred->id.empty()) {
                        if (!preferred->displayName.empty()) out << " - ";
                        out << preferred->id;
                    } else if (preferred->displayName.empty()) {
                        out << "<named hash>";
                    }
                    if (!preferred->category.empty()) out << " [" << preferred->category << ']';
                    if (mismatch) out << " [unexpected for this section]";
                } else if (raw != 0U && raw != std::numeric_limits<std::uint32_t>::max()) {
                    ++result.unresolvedHashes;
                }
            }
            break;
        }
        case ValueType::Long: {
            const auto raw = readU64(p);
            out << bitCopy<std::int64_t>(raw) << " / " << hexValue(raw, 16);
            break;
        }
        case ValueType::ULong: {
            const auto raw = readU64(p);
            out << raw << " / " << hexValue(raw, 16);
            break;
        }
        case ValueType::Float: {
            const auto raw = readU32(p);
            const auto value = bitCopy<float>(raw);
            out << std::setprecision(9) << value << " / " << hexValue(raw, 8);
            break;
        }
        }
        out << "\r\n";
    }
    if (total > result.shown) out << "\r\n... " << (total - result.shown) << " additional elements not shown.\r\n";
    if (type == ValueType::UInt && hashes) {
        out << "\r\nResolved hash values shown: " << result.resolvedHashes
            << "\r\nUnknown nonzero UInt values: " << result.unresolvedHashes << "\r\n";
        if (filter && (!filter->category.empty() || !filter->internalPrefix.empty())) {
            out << "Expected hash category: "
                << (filter->category.empty() ? "<any>" : filter->category) << "\r\n"
                << "Expected internal prefix: "
                << (filter->internalPrefix.empty() ? "<any>" : filter->internalPrefix) << "\r\n"
                << "Resolved values outside the expected section filter: "
                << result.filterMismatches << "\r\n";
        }
    }
    result.text = out.str();
    return result;
}

} // namespace gdtv
