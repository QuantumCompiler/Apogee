#include "platform/xcode.h"

#include <charconv>
#include <system_error>
#include <utility>
#include <vector>

namespace apogee::platform {

namespace {

/// Where Xcode installs itself: what the remedy for a Command Line Tools
/// selection names, when it is there.
constexpr std::string_view kXcodeApp = "/Applications/Xcode.app";

/// `text` without the white space around it.
[[nodiscard]] std::string trimmed(std::string_view text) {
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\n' ||
                             text.front() == '\r')) {
        text.remove_prefix(1);
    }
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t' || text.back() == '\n' ||
                             text.back() == '\r')) {
        text.remove_suffix(1);
    }
    return std::string{text};
}

/// One of Xcode's commands, run captured under `limits`.
[[nodiscard]] CapturedRun ask(std::string program, std::vector<std::string> arguments,
                              const CapturedLimits& limits) {
    ChildCommand command;
    command.program = std::move(program);
    command.arguments = std::move(arguments);
    return run_captured(command, limits);
}

/// The installed Xcode a person could select, or empty.
[[nodiscard]] std::filesystem::path installed_xcode_app() {
    std::error_code code;
    const std::filesystem::path app{kXcodeApp};
    return std::filesystem::is_directory(app, code) ? app : std::filesystem::path{};
}

}  // namespace

std::optional<XcodeVersion> parse_xcodebuild_version(std::string_view text) {
    XcodeVersion out;
    while (!text.empty()) {
        const std::size_t end = text.find('\n');
        const std::string line = trimmed(text.substr(0, end));
        text = end == std::string_view::npos ? std::string_view{} : text.substr(end + 1);
        constexpr std::string_view kXcode = "Xcode ";
        constexpr std::string_view kBuild = "Build version ";
        if (line.starts_with(kXcode) && out.version.empty()) {
            out.version = trimmed(std::string_view{line}.substr(kXcode.size()));
        } else if (line.starts_with(kBuild) && out.build.empty()) {
            out.build = trimmed(std::string_view{line}.substr(kBuild.size()));
        }
    }
    if (out.version.empty() || !xcode_at_least(out.version, 0, 0)) {
        return std::nullopt;
    }
    return out;
}

bool xcode_at_least(std::string_view version, int major, int minor) {
    const auto number = [&version](int& value) {
        const char* begin = version.data();
        const char* end = version.data() + version.size();
        const auto [at, error] = std::from_chars(begin, end, value);
        if (error != std::errc{} || at == begin) {
            return false;
        }
        version.remove_prefix(static_cast<std::size_t>(at - begin));
        return true;
    };
    int have_major = 0;
    if (!number(have_major)) {
        return false;
    }
    int have_minor = 0;
    if (version.starts_with('.')) {
        version.remove_prefix(1);
        if (!number(have_minor)) {
            have_minor = 0;
        }
    }
    return have_major != major ? have_major > major : have_minor >= minor;
}

bool is_xcode_developer_dir(const std::filesystem::path& developer_dir) {
    std::filesystem::path dir = developer_dir.lexically_normal();
    if (!dir.has_filename()) {
        dir = dir.parent_path();  // a trailing separator
    }
    const std::filesystem::path contents = dir.parent_path();
    return dir.filename() == "Developer" && contents.filename() == "Contents" &&
           contents.parent_path().extension() == ".app";
}

std::optional<XcodeFacts> xcode_facts(const CapturedLimits& limits, std::string& error) {
    XcodeFacts facts;
    const CapturedRun selected = ask("xcode-select", {"-p"}, limits);
    if (!selected.status.has_value()) {
        error = selected.error;
        return std::nullopt;
    }
    if (*selected.status != 0) {
        // No developer directory: an absent Xcode, not a failed probe.
        facts.install = XcodeInstall::None;
        return facts;
    }
    facts.developer_dir = trimmed(selected.out);
    std::error_code code;
    if (facts.developer_dir.empty()) {
        facts.install = XcodeInstall::None;
        return facts;
    }
    if (!std::filesystem::is_directory(facts.developer_dir, code)) {
        // Never `xcrun` on a directory that is not there: its shims would
        // offer to install the tools.
        facts.install = XcodeInstall::Missing;
        facts.xcode_app = installed_xcode_app();
        return facts;
    }
    if (!is_xcode_developer_dir(facts.developer_dir)) {
        facts.install = XcodeInstall::CommandLineTools;
        facts.xcode_app = installed_xcode_app();
        return facts;
    }

    const CapturedRun version = ask("xcrun", {"xcodebuild", "-version"}, limits);
    if (!version.error.empty()) {
        error = version.error;
        return std::nullopt;
    }
    const std::optional<XcodeVersion> read = parse_xcodebuild_version(version.out);
    if (!read.has_value()) {
        error = "xcodebuild -version named no version";
        return std::nullopt;
    }
    facts.install = XcodeInstall::Xcode;
    facts.version = read->version;
    facts.build = read->build;

    const CapturedRun bridge = ask("xcrun", {"--find", "mcpbridge"}, limits);
    if (!bridge.status.has_value()) {
        error = bridge.error;
        return std::nullopt;
    }
    if (*bridge.status == 0) {
        facts.mcpbridge = trimmed(bridge.out);
    }
    return facts;
}

}  // namespace apogee::platform
