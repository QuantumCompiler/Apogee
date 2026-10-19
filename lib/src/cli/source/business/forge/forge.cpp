#include "forge/forge.h"

#include <algorithm>
#include <cctype>

namespace apogee::forge {
namespace {

constexpr std::string_view kForms =
    "give a merge request's URL, owner/repo#12 (GitHub), group/project!4 (GitLab), or either "
    "prefixed github: or gitlab:";

[[nodiscard]] std::string_view trimmed(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

[[nodiscard]] std::vector<std::string> segments_of(std::string_view path) {
    std::vector<std::string> out;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t slash = path.find('/', start);
        const std::string_view piece = path.substr(
            start, slash == std::string_view::npos ? std::string_view::npos : slash - start);
        if (!piece.empty()) {
            out.emplace_back(piece);
        }
        if (slash == std::string_view::npos) {
            break;
        }
        start = slash + 1;
    }
    return out;
}

/// A path segment a forge allows in a repository's name.
[[nodiscard]] bool name_segment(std::string_view segment) {
    return !segment.empty() && segment != "." && segment != ".." &&
           std::ranges::all_of(segment, [](char c) {
               return std::isalnum(static_cast<unsigned char>(c)) != 0 || c == '.' || c == '_' ||
                      c == '-';
           });
}

[[nodiscard]] std::optional<std::uint64_t> number_of(std::string_view text) {
    if (text.empty() || text.size() > 18 ||
        !std::ranges::all_of(text, [](char c) { return c >= '0' && c <= '9'; })) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : text) {
        value = value * 10 + static_cast<std::uint64_t>(c - '0');
    }
    if (value == 0) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::string joined(const std::vector<std::string>& parts, std::size_t count) {
    std::string out;
    for (std::size_t i = 0; i < count; ++i) {
        out += (i == 0 ? "" : "/") + parts[i];
    }
    return out;
}

/// The project is a forge's shape: exactly `owner/repo` on GitHub, at least
/// `group/project` on GitLab, every segment a name.
[[nodiscard]] bool project_shape(Forge forge, const std::vector<std::string>& parts) {
    const bool counted = forge == Forge::GitHub ? parts.size() == 2 : parts.size() >= 2;
    return counted && std::ranges::all_of(parts, name_segment);
}

[[nodiscard]] std::optional<MergeRequestRef> refused(std::string_view text, std::string& error,
                                                     std::string_view why = {}) {
    error = "'" + std::string{text} + "' is not a merge request" +
            (why.empty() ? std::string{} : " (" + std::string{why} + ")") + ": " +
            std::string{kForms};
    return std::nullopt;
}

[[nodiscard]] std::optional<MergeRequestRef> parse_url(std::string_view text, std::string_view rest,
                                                       std::string& error) {
    const std::size_t slash = rest.find('/');
    if (slash == std::string_view::npos || slash == 0) {
        return refused(text, error, "no path after the host");
    }
    std::string host{rest.substr(0, slash)};
    std::ranges::transform(host, host.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    std::string_view path = rest.substr(slash);
    if (const std::size_t cut = path.find_first_of("?#"); cut != std::string_view::npos) {
        path = path.substr(0, cut);
    }
    const std::vector<std::string> parts = segments_of(path);

    // GitLab: <project...>/-/merge_requests/<iid>[/...]
    for (std::size_t i = 0; i + 2 < parts.size(); ++i) {
        if (parts[i] == "-" && parts[i + 1] == "merge_requests") {
            const std::vector<std::string> project{parts.begin(),
                                                   parts.begin() + static_cast<std::ptrdiff_t>(i)};
            const std::optional<std::uint64_t> number = number_of(parts[i + 2]);
            if (!number.has_value() || !project_shape(Forge::GitLab, project)) {
                return refused(text, error);
            }
            return MergeRequestRef{.forge = Forge::GitLab,
                                   .host = host,
                                   .project = joined(project, project.size()),
                                   .number = *number};
        }
    }
    // GitHub: <owner>/<repo>/pull/<n>[/...]
    if (parts.size() >= 4 && parts[2] == "pull") {
        const std::vector<std::string> project{parts[0], parts[1]};
        const std::optional<std::uint64_t> number = number_of(parts[3]);
        if (!number.has_value() || !project_shape(Forge::GitHub, project)) {
            return refused(text, error);
        }
        return MergeRequestRef{
            .forge = Forge::GitHub, .host = host, .project = joined(project, 2), .number = *number};
    }
    return refused(text, error, "not a pull request's or merge request's page");
}

}  // namespace

std::string_view to_string(Forge forge) noexcept {
    return forge == Forge::GitHub ? "github" : "gitlab";
}

std::string_view default_host(Forge forge) noexcept {
    return forge == Forge::GitHub ? "github.com" : "gitlab.com";
}

std::optional<MergeRequestRef> parse_ref(std::string_view text, std::string& error) {
    const std::string_view given = trimmed(text);
    if (given.empty()) {
        return refused(text, error, "empty");
    }
    for (const std::string_view scheme :
         {std::string_view{"https://"}, std::string_view{"http://"}}) {
        if (given.starts_with(scheme)) {
            return parse_url(given, given.substr(scheme.size()), error);
        }
    }
    std::optional<Forge> prefixed;
    std::string_view rest = given;
    if (rest.starts_with("github:")) {
        prefixed = Forge::GitHub;
        rest.remove_prefix(7);
    } else if (rest.starts_with("gitlab:")) {
        prefixed = Forge::GitLab;
        rest.remove_prefix(7);
    }
    const std::size_t separator = rest.find_last_of("#!");
    if (separator == std::string_view::npos) {
        return refused(given, error, "no #number or !iid");
    }
    const Forge forge = prefixed.value_or(rest[separator] == '#' ? Forge::GitHub : Forge::GitLab);
    const std::vector<std::string> project = segments_of(rest.substr(0, separator));
    const std::optional<std::uint64_t> number = number_of(rest.substr(separator + 1));
    if (!number.has_value()) {
        return refused(given, error, "the number is not a positive whole number");
    }
    if (!project_shape(forge, project)) {
        return refused(given, error,
                       forge == Forge::GitHub ? "a GitHub repository is owner/repo"
                                              : "a GitLab project is group/project");
    }
    return MergeRequestRef{.forge = forge,
                           .host = std::string{default_host(forge)},
                           .project = joined(project, project.size()),
                           .number = *number};
}

std::string to_string(const MergeRequestRef& ref) {
    return ref.host + "/" + ref.project + (ref.forge == Forge::GitHub ? "#" : "!") +
           std::to_string(ref.number);
}

ForgeError::ForgeError(Kind kind, const std::string& message, std::string remedy)
    : std::runtime_error{message}, kind_{kind}, remedy_{std::move(remedy)} {}

}  // namespace apogee::forge
