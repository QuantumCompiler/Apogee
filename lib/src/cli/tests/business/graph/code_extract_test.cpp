#include "graph/code_extract.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <optional>
#include <random>
#include <string>
#include <string_view>
#include <vector>

#include "embedstore/store.h"
#include "graph/code_build.h"
#include "graph/code_languages.h"
#include "graph/code_parser.h"
#include "support/env_guard.h"

/// The code graph's extraction, golden per vendored grammar (27k): each
/// language's fixture mini-repo under tests/fixtures/code_graph/<lang>/ --
/// committed source, no network -- is built into a fresh store, and the
/// whole graph it holds (every node with its description, metadata and
/// `file:line` mentions, every edge with its origin and sites) must equal
/// `<lang>.golden` byte for byte.
///
/// Regenerating after a deliberate change -- a query edited, a grammar pin
/// moved -- writes the dumps elsewhere for review, never into the tree:
///   APOGEE_CODE_GRAPH_GOLDENS=<dir> apogee_tests "[graph][code][golden]"
/// then diff `<dir>/<lang>.golden` against the committed one and copy it in.
namespace {

using apogee::graph::CodeLanguage;
using apogee::graph::SourceBuildOptions;
using apogee::graph::SourceBuildResult;
using apogee::graph::SourceMember;

[[nodiscard]] std::filesystem::path fixtures() {
    return std::filesystem::path{APOGEE_TEST_FIXTURES} / "code_graph";
}

[[nodiscard]] std::optional<std::string> slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// A fixture directory as a source member: every file under it, relative,
/// sorted -- what the walk hands a build -- and a reader rooted there.
[[nodiscard]] SourceMember member_of(const std::filesystem::path& root, std::string label) {
    SourceMember member;
    member.label = std::move(label);
    for (const auto& entry : std::filesystem::recursive_directory_iterator{root}) {
        if (entry.is_regular_file()) {
            member.files.push_back(entry.path().lexically_relative(root).generic_string());
        }
    }
    std::ranges::sort(member.files);
    member.read = [root](std::string_view path, std::string& error) -> std::optional<std::string> {
        std::optional<std::string> content = slurp(root / std::filesystem::path{std::string{path}});
        if (!content.has_value()) {
            error = "cannot open";
        }
        return content;
    };
    return member;
}

struct Built {
    SourceBuildResult result;
    std::string dump;
};

[[nodiscard]] Built build_fixture(const CodeLanguage& language) {
    apogee::testing::TempDir dir{"code-golden-" + std::to_string(std::random_device{}())};
    apogee::embedstore::Store store{dir.path() / "g.db"};
    const std::vector<SourceMember> members{
        member_of(fixtures() / std::string{language.name}, std::string{language.name})};
    Built out;
    out.result = apogee::graph::build_source(store, members, SourceBuildOptions{});
    out.dump = store.graph_dump();
    return out;
}

}  // namespace

TEST_CASE("every vendored grammar's fixture builds to its golden graph", "[graph][code][golden]") {
    const char* regenerate = std::getenv("APOGEE_CODE_GRAPH_GOLDENS");
    for (const CodeLanguage& language : apogee::graph::code_languages()) {
        const std::string name{language.name};
        DYNAMIC_SECTION(name) {
            // Every grammar the roster vendors has a fixture, and the fixture
            // actually exercises it -- a golden over zero files of its
            // language would pass for any grammar at all.
            REQUIRE(std::filesystem::is_directory(fixtures() / name));
            const Built built = build_fixture(language);
            CHECK(built.result.files_by_language.contains(name));
            CHECK(built.result.skipped.empty());
            CHECK(built.result.partial.empty());
            if (regenerate != nullptr) {
                std::ofstream{std::filesystem::path{regenerate} / (name + ".golden"),
                              std::ios::binary}
                    << built.dump;
            }
            const std::optional<std::string> golden = slurp(fixtures() / (name + ".golden"));
            REQUIRE(golden.has_value());
            CHECK(built.dump == *golden);
        }
    }
}
