#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "support/env_guard.h"

/// An entry's `adapters:` (53a): what the loader reads and refuses, and the
/// one editor writing the list in place -- a flow list or a block list
/// replaced whole, every other byte and comment kept, in both formats.
namespace {

using apogee::harness::AdapterConfig;
using apogee::harness::ConfigEditError;
using apogee::harness::ConfigError;
using apogee::harness::parse_config;
using apogee::harness::set_backend_adapters;
using apogee::harness::set_backend_field;
using Catch::Matchers::ContainsSubstring;

/// A pin: any 64 hex digits will do for the editor and the loader.
const std::string kPin(64, 'a');

/// A comment-dense YAML config, one entry with a block list of adapters.
constexpr std::string_view kYaml = R"(# My models.
backends:
  local:
    type: llamacpp   # in process
    model_path: /models/base.gguf   # the base
    context_size: 4096
  # The tuned one.
  tuned:
    type: llamacpp
    model_path: /models/base.gguf
    adapters:   # the LoRAs
      # the style
      - path: /a/style.gguf
        scale: 0.5

      - path: /a/tone.gguf
    # the window
    context_size: 8192
models:
  default: local
)";

/// Its JSONC twin.
constexpr std::string_view kJsonc = R"(// My models.
{
  "backends": {
    "local": {
      "type": "llamacpp", // in process
      "model_path": "/models/base.gguf", // the base
      "context_size": 4096
    },
    // The tuned one.
    "tuned": {
      "type": "llamacpp",
      "model_path": "/models/base.gguf",
      "adapters": [{"path": "/a/style.gguf", "scale": 0.5}, {"path": "/a/tone.gguf"}],
      // the window
      "context_size": 8192
    }
  },
  "models": {
    "default": "local" // the everyday one
  }
}
)";

[[nodiscard]] std::vector<AdapterConfig> read(std::string_view text, std::string_view backend) {
    return parse_config(text, "<test>").find_backend(backend)->adapters;
}

[[nodiscard]] std::string with_entry(std::string_view fields) {
    return "backends:\n  e:\n" + std::string{fields};
}

}  // namespace

TEST_CASE("the loader reads an entry's adapters in order with their scales and pins",
          "[config][adapters]") {
    const std::vector<AdapterConfig> tuned = read(kYaml, "tuned");
    REQUIRE(tuned.size() == 2);
    CHECK(tuned[0] == AdapterConfig{.path = "/a/style.gguf", .scale = 0.5});
    // A scale unset is 1; no pin is an empty one, refused only at load.
    CHECK(tuned[1] == AdapterConfig{.path = "/a/tone.gguf", .scale = 1.0});
    CHECK(read(kYaml, "local").empty());
    // JSONC means what its YAML twin means.
    CHECK(read(kJsonc, "tuned") == tuned);
    CHECK(read(kJsonc, "local").empty());

    // `~` and `${VAR}` expanded as every path is; a pin read in any case,
    // kept in lower.
    const apogee::testing::EnvGuard guard{"APOGEE_ADAPTER_TEST_DIR", "/srv/loras"};
    const std::string upper(64, 'A');
    const std::vector<AdapterConfig> expanded =
        read(with_entry("    type: mlx\n    model_path: /m\n    adapters:\n      - path: "
                        "${APOGEE_ADAPTER_TEST_DIR}/one\n        base_sha256: " +
                        upper + "\n"),
             "e");
    REQUIRE(expanded.size() == 1);
    CHECK(expanded[0].path == "/srv/loras/one");
    CHECK(expanded[0].base_sha256 == kPin);
}

TEST_CASE("the loader refuses adapters an entry cannot apply, naming the key",
          "[config][adapters]") {
    const auto refused = [](const std::string& fields) {
        return [fields] { (void)parse_config(with_entry(fields), "<test>"); };
    };
    // Only the local runtimes apply adapters.
    CHECK_THROWS_WITH(refused("    type: anthropic\n    adapters: [{path: /a.gguf}]\n")(),
                      ContainsSubstring("backends.e.adapters: a backend of type anthropic "
                                        "applies no adapters -- only llamacpp and mlx"));
    // An empty list asks nothing of anyone.
    CHECK_NOTHROW(refused("    type: anthropic\n    adapters: []\n")());
    CHECK_THROWS_WITH(refused("    type: llamacpp\n    adapters: /a.gguf\n")(),
                      ContainsSubstring("backends.e.adapters: expected a list of adapters"));
    CHECK_THROWS_WITH(refused("    type: llamacpp\n    adapters: [/a.gguf]\n")(),
                      ContainsSubstring("backends.e.adapters[0]: expected {path, scale}"));
    CHECK_THROWS_WITH(refused("    type: llamacpp\n    adapters: [{scale: 0.5}]\n")(),
                      ContainsSubstring("backends.e.adapters[0]: an adapter needs a path"));
    CHECK_THROWS_WITH(refused("    type: llamacpp\n    adapters: [{path: /a.gguf, scal: 1}]\n")(),
                      ContainsSubstring("unknown key 'scal'"));
    for (const char* scale : {"0", "-1", "2.5", ".nan"}) {
        INFO(scale);
        CHECK_THROWS_WITH(
            refused(std::string{"    type: llamacpp\n    adapters: [{path: /a.gguf, scale: "} +
                    scale + "}]\n")(),
            ContainsSubstring("backends.e.adapters[0].scale: must be above 0 and at most 2"));
    }
    CHECK_NOTHROW(refused("    type: llamacpp\n    adapters: [{path: /a.gguf, scale: 2}]\n")());
    CHECK_THROWS_WITH(
        refused("    type: llamacpp\n    adapters: [{path: /a.gguf, base_sha256: abc}]\n")(),
        ContainsSubstring("base_sha256: 'abc' is not a sha256"));
    CHECK_THROWS_WITH(
        refused("    type: llamacpp\n    adapters: [{path: /a.gguf}, {path: /a.gguf}]\n")(),
        ContainsSubstring("adapters[1].path: /a.gguf is already in the list"));
    // mlx-lm loads one adapter directory, at the scale its training wrote.
    CHECK_THROWS_WITH(refused("    type: mlx\n    adapters: [{path: /a}, {path: /b}]\n")(),
                      ContainsSubstring("an mlx entry applies one adapter"));
    CHECK_THROWS_WITH(refused("    type: mlx\n    adapters: [{path: /a, scale: 0.5}]\n")(),
                      ContainsSubstring("adapters[0].scale: mlx-lm applies an adapter at the "
                                        "scale its training wrote"));
    CHECK_NOTHROW(refused("    type: mlx\n    adapters: [{path: /a, scale: 1}]\n")());
    CHECK_THROWS_AS(refused("    type: llamacpp\n    adapters: [{path: /a.gguf, scale: x}]\n")(),
                    ConfigError);
}

TEST_CASE("the editor writes adapters in place, each kept comment where it stood",
          "[config_edit][adapters]") {
    // Inserted under the base it applies over, the pin quoted.
    const std::string added = set_backend_adapters(
        kYaml, "local", {{.path = "/a/x.gguf", .scale = 0.8, .base_sha256 = kPin}});
    std::string expected{kYaml};
    const std::string under = "    model_path: /models/base.gguf   # the base\n";
    expected.insert(
        expected.find(under) + under.size(),
        "    adapters: [{path: /a/x.gguf, scale: 0.8, base_sha256: \"" + kPin + "\"}]\n");
    CHECK(added == expected);
    CHECK(read(added, "local") ==
          std::vector<AdapterConfig>{{.path = "/a/x.gguf", .scale = 0.8, .base_sha256 = kPin}});
    // Replaced on its own line, its comment kept; cleared, the file it was.
    const std::string moved = set_backend_adapters(added, "local", {{.path = "/a/y.gguf"}});
    std::string again{kYaml};
    again.insert(again.find(under) + under.size(), "    adapters: [{path: /a/y.gguf, scale: 1}]\n");
    CHECK(moved == again);
    CHECK(set_backend_adapters(moved, "local", {}) == kYaml);
    CHECK(set_backend_field(moved, "local", "adapters", "") == kYaml);

    // A block list is replaced whole: every line of its value, the comment
    // among its items with it; the comment after its key, and the next
    // field's, stay.
    const std::string flowed = set_backend_adapters(kYaml, "tuned", {{.path = "/a/z.gguf"}});
    std::string block{kYaml};
    const std::string from = "    adapters:   # the LoRAs\n";
    const std::string to = "      - path: /a/tone.gguf\n";
    const std::size_t start = block.find(from);
    block.replace(start, block.find(to) + to.size() - start,
                  "    adapters: [{path: /a/z.gguf, scale: 1}]   # the LoRAs\n");
    CHECK(flowed == block);
    // Cleared, nothing of it is left.
    std::string cleared{kYaml};
    cleared.erase(start, cleared.find(to) + to.size() - start);
    CHECK(set_backend_field(kYaml, "tuned", "adapters", "") == cleared);
    CHECK(read(cleared, "tuned").empty());
    // Nothing to clear leaves every byte.
    CHECK(set_backend_adapters(kYaml, "local", {}) == kYaml);
    CHECK_THROWS_AS(set_backend_adapters(kYaml, "nope", {{.path = "/a"}}), ConfigEditError);
    // A comment at the field's own indent between its key and its items is
    // inside its value; one after the last item is the next field's.
    const std::string between = with_entry(
        "    type: llamacpp\n    adapters:\n    # first\n      - path: /a.gguf\n"
        "    # the base\n    model_path: /m.gguf\n");
    CHECK(set_backend_adapters(between, "e", {}) ==
          with_entry("    type: llamacpp\n    # the base\n    model_path: /m.gguf\n"));
}

TEST_CASE("an adapters edit keeps the file's line endings and its unterminated end",
          "[config_edit][adapters]") {
    const std::string crlf =
        "backends:\r\n  e:\r\n    type: llamacpp\r\n    model_path: /m.gguf\r\n";
    const std::string set = set_backend_adapters(crlf, "e", {{.path = "/a.gguf"}});
    CHECK(set == crlf + "    adapters: [{path: /a.gguf, scale: 1}]\r\n");
    CHECK(set_backend_adapters(set, "e", {}) == crlf);
    // A file that ends without a newline still does.
    const std::string open = "backends:\n  e:\n    type: llamacpp\n    model_path: /m.gguf";
    const std::string opened = set_backend_adapters(open, "e", {{.path = "/a.gguf"}});
    CHECK(opened == open + "\n    adapters: [{path: /a.gguf, scale: 1}]");
    CHECK(read(opened, "e").size() == 1);
    // A path YAML would misread is quoted, and read back as written.
    const std::string quoted =
        set_backend_adapters(open, "e", {{.path = "C:\\loras\\a, b.gguf", .scale = 1.5}});
    CHECK_THAT(quoted, ContainsSubstring("{path: \"C:\\\\loras\\\\a, b.gguf\", scale: 1.5}"));
    CHECK(read(quoted, "e") ==
          std::vector<AdapterConfig>{{.path = "C:\\loras\\a, b.gguf", .scale = 1.5}});
    // A `#` inside a quoted path is no comment; the one after the list is,
    // and stays.
    const std::string hashed =
        with_entry("    type: llamacpp\n    adapters: [{path: \"/a #1's.gguf\"}]   # the LoRA\n");
    CHECK(
        set_backend_adapters(hashed, "e", {{.path = "/b.gguf"}}) ==
        with_entry("    type: llamacpp\n    adapters: [{path: /b.gguf, scale: 1}]   # the LoRA\n"));
}

TEST_CASE("the editor writes adapters in a JSONC config in place, its comments kept",
          "[config_edit][adapters][jsonc]") {
    const std::vector<AdapterConfig> one{{.path = "/a/x.gguf", .scale = 0.8, .base_sha256 = kPin}};
    const std::string added = set_backend_adapters(kJsonc, "local", one);
    CHECK(read(added, "local") == one);
    // Every comment of the file, where it was.
    for (const char* comment : {"// My models.", "// in process", "// the base",
                                "// The tuned one.", "// the window", "// the everyday one"}) {
        INFO(comment);
        CHECK_THAT(added, ContainsSubstring(comment));
    }
    CHECK(read(added, "tuned") == read(kJsonc, "tuned"));
    // Cleared, the file it was, byte for byte.
    CHECK(set_backend_adapters(added, "local", {}) == kJsonc);

    // An existing list replaced where it stands.
    const std::string replaced = set_backend_adapters(kJsonc, "tuned", one);
    CHECK(read(replaced, "tuned") == one);
    CHECK_THAT(replaced, ContainsSubstring("// the window"));
    std::string untouched{kJsonc};
    const std::size_t at = untouched.find("      \"adapters\"");
    const std::size_t end = untouched.find('\n', at) + 1;
    CHECK(replaced.substr(0, at) == untouched.substr(0, at));
    CHECK(replaced.substr(replaced.size() - (untouched.size() - end)) == untouched.substr(end));
}

TEST_CASE("set-backend's adapters value reads <path>[:<scale>], comma-separated",
          "[config_edit][adapters]") {
    using apogee::harness::parse_adapter_list;
    CHECK(parse_adapter_list("/a/one.gguf") ==
          std::vector<AdapterConfig>{{.path = "/a/one.gguf", .scale = 1.0}});
    CHECK(parse_adapter_list("/a/one.gguf:0.5, /a/two.gguf") ==
          std::vector<AdapterConfig>{{.path = "/a/one.gguf", .scale = 0.5},
                                     {.path = "/a/two.gguf", .scale = 1.0}});
    // Only a number after the last colon is a scale.
    CHECK(parse_adapter_list("C:\\loras\\a.gguf") ==
          std::vector<AdapterConfig>{{.path = "C:\\loras\\a.gguf", .scale = 1.0}});
    CHECK(parse_adapter_list("C:\\loras\\a.gguf:2") ==
          std::vector<AdapterConfig>{{.path = "C:\\loras\\a.gguf", .scale = 2.0}});
    CHECK_THROWS_WITH(parse_adapter_list("/a.gguf:3"),
                      ContainsSubstring("adapters: /a.gguf:3: its scale 3 is outside (0, 2]"));
    CHECK_THROWS_WITH(parse_adapter_list("/a.gguf:0"), ContainsSubstring("is outside (0, 2]"));
    CHECK_THROWS_WITH(parse_adapter_list("/a.gguf,,/b.gguf"),
                      ContainsSubstring("names an empty adapter"));
    CHECK_THROWS_WITH(parse_adapter_list(":0.5"), ContainsSubstring("names an empty adapter"));
    // Through the field: the list as given, pinned to nothing.
    const std::string set =
        set_backend_field(kYaml, "local", "adapters", "/a/one.gguf:0.5,/a/two.gguf");
    CHECK_THAT(set, ContainsSubstring("    adapters: [{path: /a/one.gguf, scale: 0.5}, "
                                      "{path: /a/two.gguf, scale: 1}]\n"));
}
