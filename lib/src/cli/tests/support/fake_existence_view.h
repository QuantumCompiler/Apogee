#pragma once

#include <cstdint>
#include <filesystem>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <string_view>

#include "backends/provider_status.h"

namespace apogee::testing {

/// The filesystem as provider detection sees it (28a, 28c), scripted: which
/// programs PATH finds, which paths exist, what each binary's identity is,
/// where home is. Nothing on the real machine is looked at, so a case runs
/// the same on a host with every vendor CLI installed and on one with none.
class FakeExistenceView final : public backends::ExistenceView {
public:
    /// Program name -> where PATH finds it.
    std::map<std::string, std::string, std::less<>> on_path;
    /// Paths that exist.
    std::set<std::string, std::less<>> files;
    /// Binary path -> its identity.
    std::map<std::string, backends::BinaryFingerprint, std::less<>> identities;
    std::optional<std::filesystem::path> home_dir = std::filesystem::path{"/home/u"};

    /// Puts `program` on PATH at `/bin/<program>`, last changed at `modified`.
    void install(const std::string& program, std::int64_t modified = 1) {
        const std::string path = "/bin/" + program;
        on_path[program] = path;
        identities[path] = backends::BinaryFingerprint{path, modified};
    }

    [[nodiscard]] std::string find_program(std::string_view program) const override {
        const auto found = on_path.find(program);
        return found == on_path.end() ? std::string{} : found->second;
    }

    [[nodiscard]] bool exists(const std::filesystem::path& path) const override {
        return files.contains(path.generic_string());
    }

    [[nodiscard]] std::optional<backends::BinaryFingerprint> fingerprint(
        const std::filesystem::path& path) const override {
        const auto found = identities.find(path.generic_string());
        return found == identities.end() ? std::nullopt : std::optional{found->second};
    }

    [[nodiscard]] std::optional<std::filesystem::path> home() const override {
        return home_dir;
    }
};

}  // namespace apogee::testing
