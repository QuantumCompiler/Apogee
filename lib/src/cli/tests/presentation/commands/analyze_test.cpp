#include "commands/analyze.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

/// `analyze`'s pure rules: the quiet-save truth table, the saved filename's
/// shape, and which rendering a run gets.
namespace {

using apogee::commands::extension_for;
using apogee::commands::Rendering;
using apogee::commands::report_filename;
using apogee::commands::resolve_rendering;
using apogee::commands::should_print_report;

}  // namespace

TEST_CASE("the quiet-save rule: a saved run at a terminal is quiet, every other case prints",
          "[commands][analyze][save]") {
    struct Case {
        const char* name;
        bool show;
        bool json;
        bool tty;
        bool will_save;
        bool want;
    };

    const Case cases[] = {
        {"default: tty + saving -> quiet", false, false, true, true, false},
        {"--show forces print", true, false, true, true, true},
        {"--json forces print", false, true, true, true, true},
        {"piped/redirected stdout prints", false, false, false, true, true},
        {"no save target prints (never lost)", false, false, true, false, true},
        {"non-tty + no save still prints", false, false, false, false, true},
        {"show wins even when saving to a tty", true, false, true, true, true},
    };
    for (const Case& c : cases) {
        INFO(c.name);
        CHECK(should_print_report(c.show, c.json, c.tty, c.will_save) == c.want);
    }
}

TEST_CASE("the saved filename is <base>-YYYYMMDD-HHMMSS.<ext>, with no colon",
          "[commands][analyze][save]") {
    const auto when = std::chrono::system_clock::from_time_t(0) + std::chrono::hours{24 * 400};
    const std::string name = report_filename("security-review", ".md", when);
    CHECK(name.starts_with("security-review-"));
    CHECK(name.ends_with(".md"));
    CHECK(name.find(':') == std::string::npos);
    // base-8digits-6digits.ext
    const std::string stem = name.substr(std::string{"security-review-"}.size(),
                                         name.size() - std::string{"security-review-"}.size() - 3);
    REQUIRE(stem.size() == 15);
    CHECK(stem[8] == '-');
    for (std::size_t i = 0; i < stem.size(); ++i) {
        if (i != 8) {
            CHECK(std::isdigit(static_cast<unsigned char>(stem[i])) != 0);
        }
    }
    CHECK(report_filename("", ".json", when).starts_with("apogee-analyze-"));
    CHECK(extension_for(Rendering::Json) == ".json");
    CHECK(extension_for(Rendering::Text) == ".txt");
    CHECK(extension_for(Rendering::Markdown) == ".md");
    CHECK(extension_for(Rendering::Raw) == ".md");
}

TEST_CASE("rendering precedence: --json, --text, --markdown, then markdown for a schema's JSON",
          "[commands][analyze][render]") {
    CHECK(resolve_rendering(true, true, true, true, "{}") == Rendering::Json);
    CHECK(resolve_rendering(false, true, true, true, "{}") == Rendering::Text);
    CHECK(resolve_rendering(false, false, true, false, "prose") == Rendering::Markdown);
    // A schema and a JSON-looking answer render; a schema with prose stays raw.
    CHECK(resolve_rendering(false, false, false, true, "{\"a\":1}") == Rendering::Markdown);
    CHECK(resolve_rendering(false, false, false, true, "```json\n[1]\n```") == Rendering::Markdown);
    CHECK(resolve_rendering(false, false, false, true, "I refuse") == Rendering::Raw);
    // No schema, no flags: the answer as it came, JSON or not.
    CHECK(resolve_rendering(false, false, false, false, "{\"a\":1}") == Rendering::Raw);
}
