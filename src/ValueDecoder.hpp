#pragma once

#include "GameDataCore.hpp"
#include "HashDatabase.hpp"

#include <cstddef>
#include <string>
#include <string_view>

namespace gdtv {

struct HashResolutionFilter {
    std::string category;
    std::string internalPrefix;
};

struct DecodedValueSummary {
    std::string text;
    std::size_t shown{};
    std::size_t resolvedHashes{};
    std::size_t unresolvedHashes{};
    std::size_t filterMismatches{};
};

[[nodiscard]] DecodedValueSummary decodeValues(std::string_view payload, ValueType type,
                                               const HashDatabase* hashes = nullptr,
                                               std::size_t maxElements = 4000,
                                               const HashResolutionFilter* filter = nullptr);

} // namespace gdtv
