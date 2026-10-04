#include "commands/config_cmd.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "contracts/config.h"
#include "support/cli_home.h"
#include "support/gguf_builder.h"

/// `config add-backend` filling itself from the model store (M7): a stored
/// GGUF's name is enough to register it, and what it writes is what the
/// hand-typed command would have.
namespace {

using apogee::testing::CliHome;

/// A config whose comments are most of its documentation -- what the one
/// editor exists to keep.
constexpr const char* kConfig = R"(# my models
backends:
  # the cloud one
  claude:
    type: anthropic
    model: claude-sonnet-5
)";

void write_file(const std::filesystem::path& path, const std::string& bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << bytes;
}

/// `<models>/org--m/gguf/<id>/<name>`, with a projector beside it when asked.
std::filesystem::path store_gguf(const CliHome& home, const std::string& id,
                                 const std::string& name, bool projector = false) {
    const std::filesystem::path file = home.models() / "org--m" / "gguf" / id / name;
    write_file(file, apogee::testing::minimal_gguf("llama"));
    if (projector) {
        write_file(file.parent_path() / (file.stem().string() + "-mmproj.gguf"), "projector");
    }
    return file;
}

}  // namespace

TEST_CASE("a stored GGUF's name registers it, as the hand-typed command would, comments kept",
          "[commands][config][add-backend][store]") {
    const CliHome filled{kConfig};
    const std::filesystem::path file = store_gguf(filled, "111111111111", "m-F16.gguf", true);
    const std::filesystem::path projector = file.parent_path() / "m-F16-mmproj.gguf";
    std::string out;
    REQUIRE(filled.run({"config", "add-backend", "m-F16"}, &out) == 0);
    INFO(out);
    // It says what it filled, as the arguments it stands for.
    CHECK(out.find("filled from the store: org--m/gguf/111111111111\n  --type llamacpp "
                   "--model-path " +
                   file.string() + " --mmproj-path " + projector.string() + "\n") !=
          std::string::npos);

    // Byte for byte the entry a hand-typed add-backend writes, in a twin.
    const CliHome typed{kConfig};
    REQUIRE(typed.run({"config", "add-backend", "m-F16", "--type", "llamacpp", "--model-path",
                       file.string(), "--mmproj-path", projector.string()},
                      &out) == 0);
    CHECK(filled.config_text() == typed.config_text());
    CHECK(filled.config_text().starts_with("# my models\nbackends:\n  # the cloud one\n"));
}

TEST_CASE("a flag given wins over the store", "[commands][config][add-backend][store]") {
    const CliHome home{kConfig};
    (void)store_gguf(home, "111111111111", "m-F16.gguf", true);
    std::string out;

    // A model path given: nothing is filled, the projector included.
    REQUIRE(home.run({"config", "add-backend", "m-F16", "--type", "llamacpp", "--model-path",
                      "/elsewhere/m.gguf"},
                     &out) == 0);
    CHECK(out.find("filled from the store") == std::string::npos);
    apogee::harness::Config config = apogee::harness::load_config(home.config_path());
    CHECK(config.backends.at("m-F16").model_path == "/elsewhere/m.gguf");
    CHECK(config.backends.at("m-F16").mmproj_path.empty());

    // Another type: the name is just a name.
    REQUIRE(home.run({"config", "add-backend", "m-F16", "--type", "anthropic", "--model", "x",
                      "--force"},
                     &out) == 0);
    CHECK(out.find("filled from the store") == std::string::npos);
    config = apogee::harness::load_config(home.config_path());
    CHECK(config.backends.at("m-F16").type == apogee::harness::BackendType::Anthropic);

    // The type given as the store's: the path still fills, a projector given stays.
    REQUIRE(home.run({"config", "add-backend", "m-F16", "--type", "llamacpp", "--mmproj-path",
                      "/mine/p.gguf", "--force"},
                     &out) == 0);
    config = apogee::harness::load_config(home.config_path());
    CHECK(config.backends.at("m-F16").type == apogee::harness::BackendType::LlamaCpp);
    CHECK(config.backends.at("m-F16").model_path.ends_with("m-F16.gguf"));
    CHECK(config.backends.at("m-F16").mmproj_path == "/mine/p.gguf");
}

TEST_CASE("a name the store does not know is refused exactly as before",
          "[commands][config][add-backend][store]") {
    const CliHome home{kConfig};
    (void)store_gguf(home, "111111111111", "m-F16.gguf");
    std::string out;
    // The parser's own words and code, as when --type was required by it.
    CHECK(home.run({"config", "add-backend", "something-else"}, &out) == 106);
    CHECK(out == "--type is required\nRun with --help for more information.\n");
    CHECK(home.config_text() == kConfig);
}

TEST_CASE("a name two stored GGUFs share is refused with both listed",
          "[commands][config][add-backend][store]") {
    const CliHome home{kConfig};
    const std::filesystem::path one = store_gguf(home, "111111111111", "m.gguf");
    const std::filesystem::path two = store_gguf(home, "222222222222", "m.gguf");
    std::string out;
    CHECK(home.run({"config", "add-backend", "m"}, &out) == 1);
    CHECK(out.find("'m' is the name of 2 stored GGUFs -- pass --model-path with one of:") !=
          std::string::npos);
    CHECK(out.find("  " + one.string()) != std::string::npos);
    CHECK(out.find("  " + two.string()) != std::string::npos);
    CHECK(home.config_text() == kConfig);
}
