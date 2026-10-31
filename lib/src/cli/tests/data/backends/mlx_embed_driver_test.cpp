#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <deque>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "platform/child_process.h"
#include "support/env_guard.h"
#include "support/mlx_model.h"
#include "transport/jsonl_framer.h"

/// The MLX embedding driver's half of the protocol (53h), on bare python3.
///
/// The shipped `assets/mlx/mlx_embed.py` -- the file, not a copy -- runs under
/// the stub `mlx` and `mlx_embeddings` packages (`scripts/stub_mlx`,
/// `scripts/stub_mlx_embeddings`): a tokenizer of one token a character and a
/// model whose vector is a text's characters counted and normalized. Enough to
/// hold the driver to what the backend relies on without Metal, weights or the
/// network: the `ready` line, a batch answered with one vector a text in its
/// order at the model's width, the batch tokenized together and cut at the
/// directory's own length, a library's print kept off the protocol, a batch
/// it cannot embed refused with the model kept, closed stdin as the end, and
/// the fatal errors naming their fix. Skipped by name on a host with no
/// python3, or one that cannot start a child (Windows).
namespace {

using nlohmann::json;

const std::filesystem::path kDriver =
    std::filesystem::path{APOGEE_ASSETS_DIR} / "mlx" / "mlx_embed.py";
const std::filesystem::path kStubs =
    std::filesystem::path{APOGEE_TESTS_DIR} / "data" / "backends" / "scripts";

std::string python3() {
    return apogee::platform::find_on_path("python3");
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);) {
        out.push_back(line);
    }
    return out;
}

#define REQUIRE_PYTHON()                                                          \
    do {                                                                          \
        if (python3().empty() || !apogee::platform::supports_child_processes()) { \
            SKIP("no python3 to run the driver with: the driver's cases skip");   \
        }                                                                         \
    } while (false)

/// A model directory mlx-embeddings built, as the driver reads it.
struct Model {
    apogee::testing::TempDir root{"mlx-embed-driver-" + std::to_string(std::random_device{}())};
    std::filesystem::path dir = root.path() / "model";
    std::filesystem::path record = root.path() / "record.txt";

    explicit Model(int max_seq_length = 0) {
        apogee::testing::write_fixture_file(dir / "config.json",
                                            R"({"model_type": "gemma3_text", "hidden_size": 768})");
        if (max_seq_length > 0) {
            apogee::testing::write_fixture_file(
                dir / "sentence_bert_config.json",
                R"({"max_seq_length": )" + std::to_string(max_seq_length) + "}");
        }
    }

    [[nodiscard]] std::vector<std::string> recorded() const {
        const std::ifstream in{record, std::ios::binary};
        std::ostringstream out;
        out << in.rdbuf();
        return lines_of(out.str());
    }
};

/// The shipped driver, under the stubs, driven line by line.
class Driver {
public:
    explicit Driver(const Model& model,
                    std::vector<std::pair<std::string, std::string>> environment = {},
                    const std::string& stubs = "stub_mlx:stub_mlx_embeddings",
                    std::vector<std::string> arguments = {}) {
        apogee::platform::ChildCommand command;
        command.program = python3();
        command.arguments =
            arguments.empty()
                ? std::vector<std::string>{kDriver.string(), "--model", model.dir.string()}
                : std::move(arguments);
        command.extra_environment = std::move(environment);
        std::string path;
        for (std::size_t start = 0; start <= stubs.size();) {
            const std::size_t end = std::min(stubs.find(':', start), stubs.size());
            path +=
                (path.empty() ? "" : ":") + (kStubs / stubs.substr(start, end - start)).string();
            start = end + 1;
        }
        command.extra_environment.emplace_back("PYTHONPATH", path);
        command.extra_environment.emplace_back("PYTHONDONTWRITEBYTECODE", "1");
        command.extra_environment.emplace_back("STUB_MLX_RECORD", model.record.string());
        std::string error;
        child_ = apogee::platform::start_child(command, error);
        REQUIRE(child_ != nullptr);
    }

    void send(const json& line) {
        REQUIRE(child_->write_stdin(line.dump() + "\n"));
    }

    /// The next protocol line, or null at end of stream. Every stdout line
    /// must be a JSON object: nothing else may share the channel.
    json next() {
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{30};
        std::string chunk;
        while (lines_.empty()) {
            drain_stderr();
            if (std::chrono::steady_clock::now() > deadline) {
                FAIL("the driver said nothing for 30 s");
            }
            const auto status = child_->read_stdout(chunk, std::chrono::milliseconds{100});
            if (status == apogee::platform::ReadStatus::Data) {
                framer_.feed(chunk, [this](std::string_view line) { lines_.emplace_back(line); });
            } else if (status != apogee::platform::ReadStatus::Timeout) {
                framer_.flush([this](std::string_view line) { lines_.emplace_back(line); });
                if (lines_.empty()) {
                    return nullptr;
                }
            }
        }
        const std::string line = lines_.front();
        lines_.pop_front();
        const json parsed = json::parse(line, nullptr, false);
        INFO("stdout carried: " << line);
        REQUIRE(parsed.is_object());
        return parsed;
    }

    /// Closes stdin and returns the exit code.
    int close() {
        child_->close_stdin();
        while (!next().is_null()) {
        }
        return exit_code();
    }

    /// Reads to the end and returns the exit code.
    int exit_code() {
        while (!next().is_null()) {
        }
        const std::optional<int> code = child_->wait_for_exit(std::chrono::seconds{10});
        drain_stderr();
        REQUIRE(code.has_value());
        return code.value_or(-1);
    }

    [[nodiscard]] std::string stderr_text() {
        drain_stderr();
        return stderr_;
    }

private:
    void drain_stderr() {
        std::string chunk;
        while (child_->read_stderr(chunk, std::chrono::milliseconds{0}) ==
               apogee::platform::ReadStatus::Data) {
            stderr_ += chunk;
        }
    }

    std::unique_ptr<apogee::platform::ChildProcess> child_;
    apogee::backends::JsonlFramer framer_;
    std::deque<std::string> lines_;
    std::string stderr_;
};

json embed(std::int64_t id, std::vector<std::string> texts) {
    return {{"type", "embed"}, {"id", id}, {"texts", std::move(texts)}};
}

}  // namespace

TEST_CASE("the embedding driver loads once and answers each batch with its texts' vectors",
          "[mlx][embedding][driver]") {
    REQUIRE_PYTHON();
    const Model model{2048};
    Driver driver{model, {{"STUB_MLX_NOISE", "loading noise"}}};
    const json ready = driver.next();
    CHECK(ready.at("type") == "ready");
    CHECK(ready.at("protocol") == 1);
    CHECK(ready.at("embedding") == true);
    CHECK(ready.at("model_type") == "gemma3_text");
    CHECK(ready.at("mlx_embeddings") == "0.0-embeddings-stub");
    // The model's own length, from its sentence-transformers configuration.
    CHECK(ready.at("max_length") == 2048);

    driver.send(embed(1, {"the cat sat", "dogs bark", "the cat sat"}));
    const json answer = driver.next();
    CHECK(answer.at("type") == "vectors");
    CHECK(answer.at("id") == 1);
    const auto vectors = answer.at("vectors").get<std::vector<std::vector<double>>>();
    REQUIRE(vectors.size() == 3);
    for (const std::vector<double>& vector : vectors) {
        // The stub's width, pooled and normalized.
        REQUIRE(vector.size() == 768);
        double norm = 0;
        for (const double value : vector) {
            norm += value * value;
        }
        CHECK(std::abs(norm - 1.0) < 1e-6);
    }
    CHECK(vectors[0] == vectors[2]);  // the same text, the same vector
    CHECK(vectors[0] != vectors[1]);

    driver.send(embed(2, {}));
    const json empty = driver.next();
    CHECK(empty.at("type") == "vectors");
    CHECK(empty.at("vectors").empty());

    CHECK(driver.close() == 0);
    // One load; the batch tokenized together, cut at the model's length.
    const std::vector<std::string> record = model.recorded();
    CHECK(std::ranges::count(record, std::string{"embeddings load model"}) == 1);
    CHECK(std::ranges::find(record, std::string{"embeddings batch 3 max_length=2048"}) !=
          record.end());
    // The library's print reached stderr, never the protocol.
    CHECK(driver.stderr_text().find("loading noise") != std::string::npos);
}

TEST_CASE(
    "the embedding driver cuts at 512 when the model states no length, and keeps its model "
    "through a bad request",
    "[mlx][embedding][driver]") {
    REQUIRE_PYTHON();
    const Model model;
    Driver driver{model, {{"STUB_MLX_EMBEDDINGS_FAIL", "poison"}}};
    CHECK(driver.next().at("max_length") == 512);

    driver.send(embed(1, {"fine", "poison pill"}));
    const json refused = driver.next();
    CHECK(refused.at("type") == "error");
    CHECK(refused.at("id") == 1);
    CHECK(refused.at("kind") == "request");
    CHECK(refused.at("message").get<std::string>().find("refused a text") != std::string::npos);

    driver.send({{"type", "embed"}, {"id", 2}, {"texts", {1, 2}}});
    const json malformed = driver.next();
    CHECK(malformed.at("kind") == "request");
    CHECK(malformed.at("id") == 2);

    driver.send({{"type", "generate"}, {"id", 3}});
    const json unknown = driver.next();
    CHECK(unknown.at("kind") == "protocol");
    CHECK(unknown.at("id") == 3);

    // Still loaded: the next batch is answered, nothing loaded again.
    driver.send(embed(4, {"fine"}));
    CHECK(driver.next().at("type") == "vectors");
    CHECK(driver.close() == 0);
    const std::vector<std::string> record = model.recorded();
    CHECK(std::ranges::count(record, std::string{"embeddings load model"}) == 1);
    CHECK(std::ranges::find(record, std::string{"embeddings batch 1 max_length=512"}) !=
          record.end());
}

TEST_CASE("the embedding driver applies an adapter through mlx-embeddings' adapter_path",
          "[mlx][embedding][driver][adapters]") {
    // 55e: `--adapter` handed to the loader as its own adapter_path, as the
    // generation driver hands mlx-lm's (53a), and named in `ready` only then.
    REQUIRE_PYTHON();
    const Model model;
    const std::filesystem::path tone = model.root.path() / "tone";
    apogee::testing::write_fixture_file(tone / "adapter_config.json", R"({"rank": 8})");
    apogee::testing::write_fixture_file(tone / "adapters.safetensors", "delta");

    SECTION("applied, and said") {
        Driver driver{
            model,
            {},
            "stub_mlx:stub_mlx_embeddings",
            {kDriver.string(), "--model", model.dir.string(), "--adapter", tone.string()}};
        const json ready = driver.next();
        CHECK(ready.at("type") == "ready");
        CHECK(ready.at("adapter") == tone.string());
        driver.send(embed(1, {"the cat sat"}));
        CHECK(driver.next().at("type") == "vectors");
        CHECK(driver.close() == 0);
        const std::vector<std::string> record = model.recorded();
        const auto load = std::ranges::find(record, std::string{"embeddings load model"});
        REQUIRE(load != record.end());
        REQUIRE(std::next(load) != record.end());
        CHECK(*std::next(load) == "embeddings adapter tone");
    }
    SECTION("none asked, none applied, none said") {
        Driver driver{model};
        CHECK_FALSE(driver.next().contains("adapter"));
        CHECK(driver.close() == 0);
        const std::vector<std::string> record = model.recorded();
        CHECK(std::ranges::none_of(record, [](const std::string& line) {
            return line.starts_with("embeddings adapter");
        }));
    }
    SECTION("a directory short of its weights is refused by name, never loaded") {
        std::filesystem::remove(tone / "adapters.safetensors");
        Driver driver{
            model,
            {},
            "stub_mlx:stub_mlx_embeddings",
            {kDriver.string(), "--model", model.dir.string(), "--adapter", tone.string()}};
        const json error = driver.next();
        CHECK(error.at("kind") == "load");
        CHECK(error.at("message").get<std::string>().find(
                  "is not an mlx-lm adapter directory: it has no adapters.safetensors") !=
              std::string::npos);
        CHECK(driver.exit_code() == 4);
        CHECK(model.recorded().empty());
    }
    SECTION("--adapter with nothing after it is a usage error") {
        Driver driver{model,
                      {},
                      "stub_mlx:stub_mlx_embeddings",
                      {kDriver.string(), "--model", model.dir.string(), "--adapter"}};
        CHECK(driver.next().at("kind") == "protocol");
        CHECK(driver.exit_code() == 2);
    }
}

TEST_CASE("the embedding driver's fatal errors name their fix", "[mlx][embedding][driver]") {
    REQUIRE_PYTHON();
    SECTION("no mlx-embeddings") {
        const Model model;
        Driver driver{model, {}, "stub_mlx_missing:stub_mlx"};
        const json error = driver.next();
        CHECK(error.at("type") == "error");
        CHECK(error.at("kind") == "missing_dependency");
        CHECK(error.at("message").get<std::string>().find(
                  "apogee train setup --with mlx-embeddings") != std::string::npos);
        CHECK(driver.exit_code() == 3);
    }
    SECTION("a load mlx-embeddings refuses") {
        const Model model;
        Driver driver{model,
                      {{"STUB_MLX_EMBEDDINGS_LOAD_ERROR", "Model type qwen9 not supported."}}};
        const json error = driver.next();
        CHECK(error.at("kind") == "load");
        CHECK(error.at("message").get<std::string>().find(
                  "through mlx-embeddings: ValueError: Model type qwen9 not supported.") !=
              std::string::npos);
        CHECK(driver.exit_code() == 4);
    }
    SECTION("a directory that is no model") {
        const Model model;
        std::filesystem::remove(model.dir / "config.json");
        Driver driver{model};
        CHECK(driver.next().at("kind") == "load");
        CHECK(driver.exit_code() == 4);
    }
    SECTION("no --model") {
        const Model model;
        Driver driver{model, {}, "stub_mlx:stub_mlx_embeddings", {kDriver.string()}};
        const json error = driver.next();
        CHECK(error.at("kind") == "protocol");
        CHECK(error.at("message").get<std::string>().find("usage: mlx_embed.py") !=
              std::string::npos);
        CHECK(driver.exit_code() == 2);
    }
}
