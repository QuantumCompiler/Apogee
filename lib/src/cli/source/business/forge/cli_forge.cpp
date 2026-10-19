#include "forge/cli_forge.h"

#include <algorithm>
#include <cctype>
#include <filesystem>
#include <sstream>
#include <system_error>

#include "platform/child_process.h"

namespace apogee::forge {

std::optional<nlohmann::json> concatenated_arrays(const std::string& out) {
    nlohmann::json all = nlohmann::json::array();
    std::istringstream in{out};
    while ((in >> std::ws) && in.peek() != std::char_traits<char>::eof()) {
        nlohmann::json page;
        try {
            in >> page;
        } catch (const nlohmann::json::exception&) {
            return std::nullopt;
        }
        if (!page.is_array()) {
            return std::nullopt;
        }
        for (nlohmann::json& item : page) {
            all.push_back(std::move(item));
        }
    }
    return all;
}

bool contains_folded(std::string_view haystack, std::string_view needle) {
    if (needle.empty()) {
        return true;
    }
    const auto folded = [](char a, char b) {
        return std::tolower(static_cast<unsigned char>(a)) ==
               std::tolower(static_cast<unsigned char>(b));
    };
    return std::search(haystack.begin(), haystack.end(), needle.begin(), needle.end(), folded) !=
           haystack.end();
}

CliForgeClient::CliForgeClient(Cli cli, CliTransportOptions options)
    : cli_{cli}, options_{std::move(options)} {}

void CliForgeClient::require_forge(const MergeRequestRef& ref) const {
    if (ref.forge != forge()) {
        throw ForgeError{ForgeError::Kind::Malformed,
                         to_string(ref) + " is a " + std::string{to_string(ref.forge)} +
                             " merge request, and " + std::string{cli_.name} + " speaks to " +
                             std::string{to_string(forge())},
                         ""};
    }
}

std::string CliForgeClient::resolve() const {
    const std::string name =
        options_.executable.empty() ? std::string{cli_.name} : options_.executable;
    const std::string install = "install " + std::string{cli_.product} + " (" +
                                std::string{cli_.install_url} +
                                "), then: " + std::string{cli_.name} + " auth login";
    if (name.find('/') != std::string::npos || name.find('\\') != std::string::npos) {
        std::error_code code;
        if (!std::filesystem::exists(name, code)) {
            throw ForgeError{
                ForgeError::Kind::Missing,
                std::string{cli_.name} + " is configured as " + name + ", which does not exist",
                install};
        }
        return name;
    }
    const std::string found =
        options_.find_on_path ? options_.find_on_path(name) : platform::find_on_path(name);
    if (found.empty()) {
        throw ForgeError{ForgeError::Kind::Missing,
                         std::string{cli_.name} + " is not installed, or not on PATH (" +
                             std::string{cli_.product} + " is how Apogee reaches " +
                             std::string{to_string(forge())} + ", under its own login)",
                         install};
    }
    return found;
}

std::string CliForgeClient::api(const MergeRequestRef& ref, std::vector<std::string> arguments,
                                std::string input) const {
    CliCall call;
    call.program = resolve();
    call.arguments = {"api"};
    if (ref.host != default_host(ref.forge)) {
        call.arguments.emplace_back("--hostname");
        call.arguments.push_back(ref.host);
    }
    call.arguments.insert(call.arguments.end(), arguments.begin(), arguments.end());
    call.input = std::move(input);
    const CliResult result = run_cli(call, options_.spawn, options_.timeout);

    const std::string cli{cli_.name};
    const std::string login =
        cli + " auth login" +
        (ref.host != default_host(ref.forge) ? " --hostname " + ref.host : std::string{});
    const std::string words = result.err.empty() ? std::string{} : ": " + result.err;
    if (!result.start_error.empty()) {
        throw ForgeError{ForgeError::Kind::Missing,
                         cli + " could not be started: " + result.start_error,
                         "install " + std::string{cli_.product} + " (" +
                             std::string{cli_.install_url} + "), then: " + cli + " auth login"};
    }
    if (result.timed_out) {
        throw ForgeError{ForgeError::Kind::Api,
                         cli + " did not answer for " + to_string(ref) + " within " +
                             std::to_string(options_.timeout.count() / 1000) + " s" + words,
                         ""};
    }
    if (result.too_large) {
        throw ForgeError{ForgeError::Kind::Api,
                         cli + "'s answer for " + to_string(ref) + " passed " +
                             std::to_string(kMaxResponseBytes / (1024 * 1024)) +
                             " MiB, so it was refused rather than cut short",
                         ""};
    }
    if (result.exit_code.value_or(1) == 0) {
        return result.out;
    }
    if (logged_out(result)) {
        throw ForgeError{ForgeError::Kind::LoggedOut,
                         cli + " is not logged in to " + ref.host + words, login};
    }
    if (not_found(result)) {
        throw ForgeError{
            ForgeError::Kind::NotFound,
            to_string(ref) + " was not found, or this " + cli + " login cannot see it" + words,
            cli + " auth status"};
    }
    throw ForgeError{ForgeError::Kind::Api,
                     cli + " api failed for " + to_string(ref) + " (exit " +
                         std::to_string(result.exit_code.value_or(-1)) + ")" + words,
                     ""};
}

}  // namespace apogee::forge
