#include "graph/code_extract.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
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

namespace {

/// The imports a Rust file's `use` declarations expand to, as `kind path
/// [as alias]`.
[[nodiscard]] std::vector<std::string> rust_imports(const std::string& source) {
    const CodeLanguage& rust = apogee::graph::code_language(apogee::graph::CodeLanguageId::Rust);
    apogee::graph::CodeParser parser{rust.id};
    const apogee::graph::FileFacts facts =
        apogee::graph::extract_code_facts(rust, "src/lib.rs", source, parser.parse(source));
    std::vector<std::string> out;
    for (const apogee::graph::CodeImport& item : facts.imports) {
        out.push_back(item.kind + " " + item.path +
                      (item.alias.empty() ? "" : " as " + item.alias));
    }
    return out;
}

}  // namespace

TEST_CASE("a Rust use tree expands to its leaves: groups, aliases, self and globs",
          "[graph][code][extract][rust][57t]") {
    CHECK(rust_imports("use a::b;\n") == std::vector<std::string>{"symbol a::b"});
    CHECK(rust_imports("use crate::shapes::{Circle, Shape as S};\n") ==
          std::vector<std::string>{"symbol crate::shapes::Circle",
                                   "symbol crate::shapes::Shape as S"});
    CHECK(rust_imports("use std::{io::{self, Read}, fmt::*, collections::HashMap};\n") ==
          std::vector<std::string>{"namespace std::fmt", "symbol std::collections::HashMap",
                                   "symbol std::io", "symbol std::io::Read"});
    CHECK(rust_imports("use a::{\n    b,\n    c::{d, e as f},\n};\n") ==
          std::vector<std::string>{"symbol a::b", "symbol a::c::d", "symbol a::c::e as f"});
}

TEST_CASE("a Rust use tree nested 100000 deep expands to its one leaf in linear time",
          "[graph][code][extract][rust][57t]") {
    // 500 KB: the depth that ran 90 s and more, and 40000, which crashed.
    for (const std::size_t depth : {std::size_t{40000}, std::size_t{100000}}) {
        std::string source = "use a::";
        std::string expected = "symbol a";
        for (std::size_t level = 0; level < depth; ++level) {
            source += "{b::";
            expected += "::b";
        }
        source += "c";
        expected += "::c";
        source += std::string(depth, '}') + ";\n";
        const auto started = std::chrono::steady_clock::now();
        const std::vector<std::string> imports = rust_imports(source);
        const auto took = std::chrono::steady_clock::now() - started;
        REQUIRE(imports.size() == 1);
        CHECK(imports.front() == expected);
        // Bounded far above what it takes (well under a second on the dev
        // Mac with tree-sitter's own parse), far below the minutes the
        // quadratic walk took.
        CHECK(took < std::chrono::seconds{15});
    }
}
