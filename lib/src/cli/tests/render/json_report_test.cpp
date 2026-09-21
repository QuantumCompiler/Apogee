#include "render/json_report.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <string>

/// The in-process renderer: `human_summary` last, everything else
/// alphabetical, nested depth, arrays of objects, and raw passthrough.
namespace {

using apogee::render::key_to_title;
using apogee::render::render_markdown;
using apogee::render::render_report;
using apogee::render::render_text;
using apogee::render::ReportFormat;
using apogee::render::strip_code_fence;

}  // namespace

TEST_CASE("human_summary renders last and every other top-level key alphabetically",
          "[render][report]") {
    const nlohmann::json report = nlohmann::json::parse(
        R"({"human_summary":"HUMAN-READABLE-WRAP","understanding":"u","notes":"n","ticket":"t"})");
    const std::string out = render_markdown(report);
    const std::size_t human = out.find("HUMAN-READABLE-WRAP");
    REQUIRE(human != std::string::npos);
    CHECK(out.find("## Human Summary") != std::string::npos);
    const std::size_t notes = out.find("## Notes");
    const std::size_t ticket = out.find("## Ticket");
    const std::size_t understanding = out.find("## Understanding");
    REQUIRE(notes != std::string::npos);
    REQUIRE(ticket != std::string::npos);
    REQUIRE(understanding != std::string::npos);
    // Alphabetical: Notes < Ticket < Understanding, and all before the summary.
    CHECK(notes < ticket);
    CHECK(ticket < understanding);
    CHECK(understanding < human);

    // The text renderer keeps the same order under `Key:` labels.
    const std::string text = render_text(report);
    CHECK(text.find("Notes:") < text.find("Ticket:"));
    CHECK(text.find("Understanding:") < text.find("HUMAN-READABLE-WRAP"));
    // Without the reserved key it is plain alphabetical, nothing appended.
    const std::string plain = render_markdown(nlohmann::json::parse(R"({"b":"1","a":"2"})"));
    CHECK(plain.find("## A") < plain.find("## B"));
    CHECK(plain.find("Human Summary") == std::string::npos);
}

TEST_CASE("nested objects step down a heading level, then bold labels", "[render][report]") {
    const nlohmann::json report = nlohmann::json::parse(
        R"({"adr_review":{"notes":"none","compliance":{"deep":"value"}},"verdict":"approve"})");
    const std::string out = render_markdown(report);
    CHECK(out.find("## Adr Review") != std::string::npos);
    CHECK(out.find("### Compliance") != std::string::npos);
    CHECK(out.find("**Deep**: value") != std::string::npos);
    CHECK(out.find("### Notes") != std::string::npos);
    CHECK(out.find("## Verdict\n\napprove") != std::string::npos);
}

TEST_CASE("an array of objects becomes bullets keyed by the longest string, with metadata",
          "[render][report]") {
    const nlohmann::json report = nlohmann::json::parse(
        R"({"findings":[{"severity":"high","title":"A much longer title wins","count":2,"blocking":true},"plain string"]})");
    const std::string out = render_markdown(report);
    CHECK(out.find("- A much longer title wins -- Blocking: **Yes**, Count: **2**, Severity: "
                   "**high**") != std::string::npos);
    CHECK(out.find("- plain string") != std::string::npos);
    const std::string text = render_text(report);
    CHECK(text.find("- A much longer title wins (Severity: high)") != std::string::npos);
}

TEST_CASE("booleans and numbers render as words and digits", "[render][report]") {
    const std::string out =
        render_markdown(nlohmann::json::parse(R"({"ok":true,"no":false,"n":3,"f":1.5})"));
    CHECK(out.find("## Ok\n\nYes") != std::string::npos);
    CHECK(out.find("## No\n\nNo") != std::string::npos);
    CHECK(out.find("## N\n\n3") != std::string::npos);
    CHECK(out.find("## F\n\n1.5") != std::string::npos);
}

TEST_CASE("keys become Title Case from snake, kebab and camel", "[render][report]") {
    CHECK(key_to_title("human_summary") == "Human Summary");
    CHECK(key_to_title("verdict-rationale") == "Verdict Rationale");
    CHECK(key_to_title("scopeDrift") == "Scope Drift");
    CHECK(key_to_title("ADR") == "Adr");
}

TEST_CASE("unparseable input is returned unchanged, fences are stripped", "[render][report]") {
    CHECK(render_report("just prose, no json", ReportFormat::Markdown) == "just prose, no json");
    CHECK(render_report("```json\n{\"a\":\"b\"}\n```", ReportFormat::Markdown) == "## A\n\nb");
    CHECK(strip_code_fence("```\n{}\n```") == "{}");
    CHECK(strip_code_fence("  {}  ") == "{}");
    // An empty render (an empty object) also falls back to the input.
    CHECK(render_report("{}", ReportFormat::Text) == "{}");
}
