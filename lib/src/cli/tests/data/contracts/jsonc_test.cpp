#include "contracts/jsonc.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>
#include <nlohmann/json.hpp>

#include <string>
#include <string_view>

/// JSONC read with its places kept, and edited in place (28i): the engine
/// under the config's one editor. The config-shaped goldens live in
/// config_jsonc_test.cpp; these hold the engine to its own contract.
namespace {

using Json = nlohmann::ordered_json;
using apogee::harness::jsonc::JsoncError;
using apogee::harness::jsonc::looks_like_jsonc;
using apogee::harness::jsonc::parse_json;
using apogee::harness::jsonc::patch;
using apogee::harness::jsonc::render;

constexpr std::string_view kDocument = R"(// A header.
{
  // Above a.
  "a": 1, // after a
  /* a block comment */
  "b": {
    "c": "x" // after c
  },
  "d": [1, 2]
}
)";

}  // namespace

TEST_CASE("JSONC is told from YAML by its first token", "[jsonc]") {
    CHECK(looks_like_jsonc("{}"));
    CHECK(looks_like_jsonc("  \n{\"a\": 1}"));
    CHECK(looks_like_jsonc("// comment\n{}"));
    CHECK(looks_like_jsonc("/* c */ {}"));
    CHECK(looks_like_jsonc("\xEF\xBB\xBF{}"));
    CHECK_FALSE(looks_like_jsonc("backends:\n  a: 1\n"));
    CHECK_FALSE(looks_like_jsonc("# yaml comment\nmodels:\n"));
    CHECK_FALSE(looks_like_jsonc(""));
    CHECK_FALSE(looks_like_jsonc("  \n"));
}

TEST_CASE("JSONC reads comments as whitespace and nothing else", "[jsonc]") {
    const Json value = parse_json(kDocument);
    CHECK(value == Json::parse(R"({"a": 1, "b": {"c": "x"}, "d": [1, 2]})"));
    // Key order is the file's.
    CHECK(value.begin().key() == "a");
    CHECK(parse_json("// only a comment\n").is_null());
    CHECK(parse_json(R"({"s": "a\"b\\c\u00e9\ud83d\ude00\n"})")["s"] ==
          "a\"b\\c\xC3\xA9\xF0\x9F\x98\x80\n");
    CHECK(parse_json(R"({"n": -0.5e+2, "i": 12, "t": true, "f": false, "z": null})") ==
          Json::parse(R"({"n": -50.0, "i": 12, "t": true, "f": false, "z": null})"));
}

TEST_CASE("JSONC refuses what strict JSON refuses, naming the line and column", "[jsonc]") {
    using Catch::Matchers::ContainsSubstring;
    CHECK_THROWS_WITH(parse_json("{\n  \"a\": 1,\n}"),
                      ContainsSubstring("line 3, column 1") && ContainsSubstring("trailing comma"));
    CHECK_THROWS_WITH(parse_json("{\"a\": 1, \"a\": 2}"), ContainsSubstring("appears twice"));
    CHECK_THROWS_WITH(parse_json("{'a': 1}"), ContainsSubstring("double quotes"));
    CHECK_THROWS_WITH(parse_json("{\"a\": 01}"), ContainsSubstring("expected ','"));
    CHECK_THROWS_WITH(parse_json("{\"a\": 1} x"), ContainsSubstring("after the document"));
    CHECK_THROWS_WITH(parse_json("{\"a\": /* open"), ContainsSubstring("never closes"));
    CHECK_THROWS_WITH(parse_json("{\"a\": \"tab\there\"}"), ContainsSubstring("control"));
    CHECK_THROWS_WITH(parse_json("[1, 2,]"), ContainsSubstring("trailing comma"));
    CHECK_THROWS_AS(parse_json("{\"a\": tru}"), JsoncError);
    CHECK_THROWS_AS(parse_json("{\"a\": \"\\ud800\"}"), JsoncError);
}

TEST_CASE("render writes the house shape", "[jsonc]") {
    const Json value = Json::parse(
        R"({"name": "x", "list": ["a", "b"], "empty": {}, "none": [], "nested": {"k": [{"v": 1}]}})");
    CHECK(render(value, "", "\n") ==
          "{\n"
          "  \"name\": \"x\",\n"
          "  \"list\": [\"a\", \"b\"],\n"
          "  \"empty\": {},\n"
          "  \"none\": [],\n"
          "  \"nested\": {\n"
          "    \"k\": [\n"
          "      {\n"
          "        \"v\": 1\n"
          "      }\n"
          "    ]\n"
          "  }\n"
          "}");
    CHECK(render(Json("é\n"), "", "\n") == "\"é\\n\"");
}

TEST_CASE("patch changes a value in place and nothing else", "[jsonc][patch]") {
    Json target = parse_json(kDocument);
    target["b"]["c"] = "y";
    std::string expected{kDocument};
    expected.replace(expected.find("\"x\""), 3, "\"y\"");
    CHECK(patch(kDocument, target) == expected);
    // Equal: the text comes back as it was, byte for byte.
    CHECK(patch(kDocument, parse_json(kDocument)) == kDocument);
    // A scalar becoming an object is rendered at the line's indentation.
    target = parse_json(kDocument);
    target["a"] = Json::parse(R"({"z": 1})");
    CHECK(patch(kDocument, target).find("\"a\": {\n    \"z\": 1\n  }, // after a") !=
          std::string::npos);
}

TEST_CASE("an inserted member and its removal are exact inverses", "[jsonc][patch]") {
    const Json base = parse_json(kDocument);
    for (const std::string_view after : {"", "a", "b", "d"}) {
        INFO("after '" << after << "'");
        Json target = Json::object();
        if (after.empty()) {
            target["new"] = Json::parse(R"({"k": "v"})");
        }
        for (const auto& [key, value] : base.items()) {
            target[key] = value;
            if (key == after) {
                target["new"] = Json::parse(R"({"k": "v"})");
            }
        }
        const std::string added = patch(kDocument, target);
        CHECK(parse_json(added) == target);
        // Every comment kept, in its place.
        for (const std::string_view comment :
             {"// A header.", "// Above a.", "// after a", "/* a block comment */", "// after c"}) {
            CHECK(added.find(comment) != std::string::npos);
        }
        CHECK(patch(added, base) == kDocument);
    }
    // The new member sits at its sibling's indentation, the comma after the
    // sibling's value and before its comment.
    Json target = base;
    target["e"] = true;
    CHECK(patch(kDocument, target).find("  \"d\": [1, 2],\n  \"e\": true\n}") != std::string::npos);
    Json first = Json::object();
    first["z"] = 0;
    for (const auto& [key, value] : base.items()) {
        first[key] = value;
    }
    CHECK(patch(kDocument, first).find("{\n  \"z\": 0,\n  // Above a.") != std::string::npos);
    Json after_a = Json::object();
    for (const auto& [key, value] : base.items()) {
        after_a[key] = value;
        if (key == "a") {
            after_a["a2"] = 2;
        }
    }
    CHECK(patch(kDocument, after_a).find("\"a\": 1, // after a\n  \"a2\": 2,\n") !=
          std::string::npos);
    Json in_b = base;
    in_b["b"]["c2"] = "w";
    CHECK(patch(kDocument, in_b).find("\"c\": \"x\", // after c\n    \"c2\": \"w\"\n  }") !=
          std::string::npos);
}

TEST_CASE("an empty object opens onto lines of its own, and closes back", "[jsonc][patch]") {
    constexpr std::string_view kEmpty = "{\n  \"backends\": {}\n}\n";
    Json target = parse_json(kEmpty);
    target["backends"]["local"] = Json::parse(R"({"type": "mock"})");
    const std::string added = patch(kEmpty, target);
    CHECK(added ==
          "{\n  \"backends\": {\n    \"local\": {\n      \"type\": \"mock\"\n    }\n  }\n}\n");
    CHECK(patch(added, parse_json(kEmpty)) == kEmpty);

    // Braces apart, holding comments: the member goes above the closing one,
    // and its removal leaves the comments exactly as they were.
    constexpr std::string_view kCommented =
        "{\n  \"backends\": {\n    // \"example\": {}\n  }\n}\n";
    target = parse_json(kCommented);
    target["backends"]["local"] = Json::parse(R"({"type": "mock"})");
    const std::string inside = patch(kCommented, target);
    CHECK(inside ==
          "{\n  \"backends\": {\n    // \"example\": {}\n    \"local\": {\n      \"type\": "
          "\"mock\"\n    }\n  }\n}\n");
    CHECK(patch(inside, parse_json(kCommented)) == kCommented);
}

TEST_CASE("a list gains and loses items in place", "[jsonc][patch]") {
    constexpr std::string_view kList = "{\n  \"hosts\": [] // none yet\n}\n";
    Json target = parse_json(kList);
    target["hosts"].push_back("a.example");
    const std::string one = patch(kList, target);
    CHECK(one == "{\n  \"hosts\": [\"a.example\"] // none yet\n}\n");
    target["hosts"].push_back("b.example");
    const std::string two = patch(one, target);
    CHECK(two == "{\n  \"hosts\": [\"a.example\", \"b.example\"] // none yet\n}\n");
    // Removing any one leaves the rest as written; every order round-trips.
    Json without_a = parse_json(two);
    without_a["hosts"].erase(0);
    CHECK(patch(two, without_a) == "{\n  \"hosts\": [\"b.example\"] // none yet\n}\n");
    CHECK(patch(two, parse_json(one)) == one);
    CHECK(patch(one, parse_json(kList)) == kList);

    // One item a line: the new one on a line of its own.
    constexpr std::string_view kBlock =
        "{\n  \"hosts\": [\n    \"a\", // first\n    \"b\"\n  ]\n}\n";
    target = parse_json(kBlock);
    target["hosts"].push_back("c");
    const std::string grown = patch(kBlock, target);
    CHECK(grown == "{\n  \"hosts\": [\n    \"a\", // first\n    \"b\",\n    \"c\"\n  ]\n}\n");
    CHECK(patch(grown, parse_json(kBlock)) == kBlock);
}

TEST_CASE("a one-line object stays on one line", "[jsonc][patch]") {
    constexpr std::string_view kInline = "{\"a\": {\"x\": 1, \"y\": 2}}\n";
    Json target = parse_json(kInline);
    target["a"]["z"] = 3;
    CHECK(patch(kInline, target) == "{\"a\": {\"x\": 1, \"y\": 2, \"z\": 3}}\n");
    target = parse_json(kInline);
    target["a"].erase("x");
    CHECK(patch(kInline, target) == "{\"a\": {\"y\": 2}}\n");
    target["a"].erase("y");
    CHECK(patch(kInline, target) == "{\"a\": {}}\n");
}

TEST_CASE("a CRLF file gets CRLF lines", "[jsonc][patch]") {
    constexpr std::string_view kWindows = "{\r\n  \"a\": 1\r\n}\r\n";
    Json target = parse_json(kWindows);
    target["b"] = 2;
    const std::string added = patch(kWindows, target);
    CHECK(added == "{\r\n  \"a\": 1,\r\n  \"b\": 2\r\n}\r\n");
    CHECK(patch(added, parse_json(kWindows)) == kWindows);
}

TEST_CASE("a file holding only comments gains its value after them", "[jsonc][patch]") {
    CHECK(patch("// nothing yet\n", Json::parse(R"({"a": 1})")) ==
          "// nothing yet\n{\n  \"a\": 1\n}\n");
}
