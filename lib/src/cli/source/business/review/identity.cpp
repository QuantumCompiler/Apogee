#include "review/identity.h"

#include <algorithm>
#include <cctype>
#include <charconv>

#include "contracts/sha256.h"

namespace apogee::review {
namespace {

constexpr std::string_view kFindingOpen = "<!-- apogee-review:finding ";
constexpr std::string_view kSummaryOpen = "<!-- apogee-review:summary head=";
constexpr std::string_view kClose = " -->";

[[nodiscard]] bool hex(std::string_view text, std::size_t low, std::size_t high) {
    return text.size() >= low && text.size() <= high && std::ranges::all_of(text, [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f') || (c >= 'A' && c <= 'F');
           });
}

}  // namespace

std::optional<std::uint32_t> location_line(std::string_view location) {
    const auto digit = std::ranges::find_if(
        location, [](char c) { return std::isdigit(static_cast<unsigned char>(c)) != 0; });
    if (digit == location.end()) {
        return std::nullopt;
    }
    std::uint32_t value = 0;
    const char* first = location.data() + (digit - location.begin());
    const auto [end, error] = std::from_chars(first, location.data() + location.size(), value);
    if (error != std::errc{} || end == first || value == 0) {
        return std::nullopt;
    }
    return value;
}

std::string normalized_location(std::string_view location) {
    if (const std::optional<std::uint32_t> line = location_line(location); line.has_value()) {
        return "L" + std::to_string(*line);
    }
    std::string out;
    for (const char c : location) {
        if (std::isspace(static_cast<unsigned char>(c)) != 0) {
            if (!out.empty() && out.back() != ' ') {
                out += ' ';
            }
        } else {
            out += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
        }
    }
    while (!out.empty() && out.back() == ' ') {
        out.pop_back();
    }
    return out;
}

std::string finding_identity(std::string_view file, std::string_view location,
                             std::string_view kind) {
    const std::string key =
        std::string{file} + "\n" + normalized_location(location) + "\n" + std::string{kind};
    return models::sha256_hex(key).substr(0, 16);
}

std::string finding_marker(std::string_view identity) {
    return std::string{kFindingOpen} + std::string{identity} + std::string{kClose};
}

std::string summary_marker(std::string_view head) {
    return std::string{kSummaryOpen} + std::string{head} + std::string{kClose};
}

Markers read_markers(std::string_view body) {
    Markers markers;
    for (std::size_t at = body.find(kFindingOpen); at != std::string_view::npos;
         at = body.find(kFindingOpen, at + 1)) {
        const std::size_t start = at + kFindingOpen.size();
        const std::size_t end = body.find(kClose, start);
        if (end != std::string_view::npos && hex(body.substr(start, end - start), 16, 16)) {
            markers.findings.emplace_back(body.substr(start, end - start));
        }
    }
    if (const std::size_t at = body.rfind(kSummaryOpen); at != std::string_view::npos) {
        const std::size_t start = at + kSummaryOpen.size();
        const std::size_t end = body.find(kClose, start);
        if (end != std::string_view::npos && hex(body.substr(start, end - start), 7, 64)) {
            markers.head = std::string{body.substr(start, end - start)};
        }
    }
    return markers;
}

}  // namespace apogee::review
