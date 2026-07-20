#include "CharacterSections.hpp"

#include <charconv>
#include <cctype>
#include <fstream>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace gdtv {
namespace {

constexpr std::string_view kHeader = "[CHARACTERGROUP|INGAMENAME|INTERNALNAME|NOTE]";

std::string trim(std::string value) {
    const auto first = value.find_first_not_of(" \t\r\n");
    if (first == std::string::npos) return {};
    const auto last = value.find_last_not_of(" \t\r\n");
    return value.substr(first, last - first + 1U);
}

std::vector<std::string> splitPipe(const std::string& line) {
    std::vector<std::string> fields;
    std::size_t start = 0U;
    while (start <= line.size()) {
        const auto separator = line.find('|', start);
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

} // namespace

std::size_t CharacterSectionMap::load(const std::filesystem::path& path, bool clearExisting) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("could not open character section text file");
    if (clearExisting) entries_.clear();

    std::string line;
    std::size_t loaded = 0U;
    bool firstLine = true;
    while (std::getline(input, line)) {
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (firstLine && line.size() >= 3U && static_cast<unsigned char>(line[0]) == 0xEFU &&
            static_cast<unsigned char>(line[1]) == 0xBBU && static_cast<unsigned char>(line[2]) == 0xBFU) {
            line.erase(0, 3);
        }
        firstLine = false;
        line = trim(std::move(line));
        if (line.empty() || line == kHeader || line.rfind("//", 0U) == 0U || line[0] == '#') continue;

        const auto fields = splitPipe(line);
        if (fields.size() != 4U) continue;
        std::uint32_t group = 0U;
        const auto result = std::from_chars(fields[0].data(), fields[0].data() + fields[0].size(), group, 10);
        if (result.ec != std::errc{} || result.ptr != fields[0].data() + fields[0].size()) continue;

        CharacterSectionEntry entry{group, decodeField(fields[1]), decodeField(fields[2]),
                                    decodeField(fields[3])};
        entries_[group] = std::move(entry);
        ++loaded;
    }
    sourcePath_ = path;
    return loaded;
}

const CharacterSectionEntry* CharacterSectionMap::find(std::uint32_t group) const noexcept {
    const auto iterator = entries_.find(group);
    return iterator == entries_.end() ? nullptr : &iterator->second;
}

} // namespace gdtv
