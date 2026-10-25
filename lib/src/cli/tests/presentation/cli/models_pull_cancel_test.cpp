#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <csignal>
#include <filesystem>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "cli/helpers.h"
#include "cli/models_pull.h"
#include "contracts/sha256.h"
#include "modelstore/store.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"
#include "transport/http_client.h"

/// Every `models pull` path stops on Ctrl-C (55c): the command's own path
/// (`run_pull`) over a scripted Hugging Face -- the API's listing and tree,
/// each file's bytes -- whose download of one file is cut halfway by a
/// SIGINT raised in the process, as a person's Ctrl-C or the GUI's stop
/// arrives. The pull's one `InterruptScope` turns it into a cancel: what
/// was staged removed, exit 130, and the 52b stream ending on its `error`.
/// Without the scope the SIGINT ends the test run, as it ended the pull.
/// The copy from the Ollama store, a local file, is `cli.machine_mode`'s, a
/// real SIGINT to the binary.
namespace {

using apogee::backends::HttpRequest;
using apogee::backends::HttpResponse;
using apogee::testing::CliHome;

/// A Hugging Face repository in memory: the model card's listing, the tree
/// with its sizes and LFS digests, and each file -- the one named
/// `interrupted` met by a Ctrl-C halfway through its bytes, or the listing
/// itself when `interrupted` is `listing`.
class ScriptedHub final : public apogee::backends::HttpTransport {
public:
    ScriptedHub(std::string repo, std::map<std::string, std::string> files, std::string interrupted)
        : repo_{std::move(repo)}, files_{std::move(files)}, interrupted_{std::move(interrupted)} {}

    HttpResponse send(const HttpRequest& request, const apogee::backends::BodySink& sink,
                      const apogee::harness::CancellationToken& cancellation) override {
        cancellation.throw_if_cancelled();
        HttpResponse response;
        response.status = 200;
        const std::string api = "https://huggingface.co/api/models/" + repo_;
        if (request.url == api) {
            if (interrupted_ == "listing") {
                (void)std::raise(SIGINT);
                cancellation.throw_if_cancelled();
            }
            response.body = listing();
            return response;
        }
        if (request.url == api + "/tree/main?recursive=true") {
            response.body = tree();
            return response;
        }
        const std::string prefix = "https://huggingface.co/" + repo_ + "/resolve/main/";
        if (request.url.starts_with(prefix)) {
            const auto file = files_.find(request.url.substr(prefix.size()));
            if (file == files_.end()) {
                response.status = 404;
                return response;
            }
            const std::string& bytes = file->second;
            if (file->first == interrupted_) {
                (void)sink(std::string_view{bytes}.substr(0, bytes.size() / 2));
                (void)std::raise(SIGINT);
                // The transport notices between chunks, as libcurl's does.
                cancellation.throw_if_cancelled();
            }
            (void)sink(bytes);
            return response;
        }
        response.status = 404;
        return response;
    }

private:
    [[nodiscard]] std::string listing() const {
        nlohmann::json siblings = nlohmann::json::array();
        for (const auto& [path, bytes] : files_) {
            siblings.push_back({{"rfilename", path}});
        }
        return nlohmann::json{{"id", repo_}, {"tags", {"gguf"}}, {"siblings", siblings}}.dump();
    }

    [[nodiscard]] std::string tree() const {
        nlohmann::json out = nlohmann::json::array();
        for (const auto& [path, bytes] : files_) {
            nlohmann::json item{{"type", "file"}, {"path", path}, {"size", bytes.size()}};
            if (path.ends_with(".safetensors")) {
                item["lfs"] = {{"oid", apogee::models::sha256_hex(bytes)}, {"size", bytes.size()}};
            }
            out.push_back(item);
        }
        return out.dump();
    }

    std::string repo_;
    std::map<std::string, std::string> files_;
    std::string interrupted_;
};

/// One client's handle onto the one hub: a pull makes a client per use.
class HubHandle final : public apogee::backends::HttpTransport {
public:
    explicit HubHandle(std::shared_ptr<ScriptedHub> hub) : hub_{std::move(hub)} {}

    HttpResponse send(const HttpRequest& request, const apogee::backends::BodySink& sink,
                      const apogee::harness::CancellationToken& cancellation) override {
        return hub_->send(request, sink, cancellation);
    }

private:
    std::shared_ptr<ScriptedHub> hub_;
};

/// A safetensors shard's bytes: its header, then its data.
[[nodiscard]] std::string shard(std::size_t bytes) {
    const std::string header = R"({"w":{"dtype":"F16","shape":[1],"data_offsets":[0,2]}})";
    std::string out(8, '\0');
    const std::size_t size = header.size();
    for (std::size_t at = 0; at < 8; ++at) {
        out[at] = static_cast<char>((size >> (8 * at)) & 0xFFU);
    }
    return out + header + std::string(bytes, '\0');
}

/// What a pull ended with: its exit code (0 when it returned), and what it
/// said on each stream.
struct Ended {
    int code = 0;
    std::string out{};
    std::string err{};

    /// stdout's events, each line one.
    [[nodiscard]] std::vector<nlohmann::json> events() const {
        std::vector<nlohmann::json> found;
        std::istringstream in{out};
        for (std::string line; std::getline(in, line);) {
            const nlohmann::json event = nlohmann::json::parse(line, nullptr, false);
            INFO(line);
            REQUIRE(event.is_object());
            found.push_back(event);
        }
        return found;
    }
};

/// A home, an empty Ollama store, and the pull of `ref` on it over `hub`.
struct Home {
    CliHome home{"models:\n  default: m\nbackends:\n  m:\n    type: mock\n"};
    apogee::testing::TempDir ollama{"pull-cancel-ollama-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard store{"OLLAMA_MODELS", ollama.path().string()};
    apogee::testing::EnvUnsetGuard token{"HF_TOKEN"};

    [[nodiscard]] Ended pull(apogee::commands::PullRequest request,
                             std::unique_ptr<ScriptedHub> hub) const {
        request.roots = apogee::models::StoreRoots::at(home.models());
        request.config_path = home.config_path();
        std::shared_ptr<ScriptedHub> shared{std::move(hub)};
        apogee::commands::ChainTools tools;
        tools.transport = [shared]() -> std::unique_ptr<apogee::backends::HttpTransport> {
            return std::make_unique<HubHandle>(shared);
        };
        const std::ostringstream out;
        const std::ostringstream err;
        std::streambuf* const old_out = std::cout.rdbuf(out.rdbuf());
        std::streambuf* const old_err = std::cerr.rdbuf(err.rdbuf());
        Ended ended;
        try {
            apogee::commands::run_pull(request, tools);
        } catch (const CLI::RuntimeError& e) {
            ended.code = e.get_exit_code();
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        ended.out = out.str();
        ended.err = err.str();
        return ended;
    }

    /// Every file the store holds: none, after a pull that wrote nothing --
    /// no set, no staging, no owner marker.
    [[nodiscard]] std::vector<std::string> stored() const {
        std::vector<std::string> out;
        std::error_code code;
        if (std::filesystem::exists(home.models(), code)) {
            for (const auto& entry : std::filesystem::recursive_directory_iterator(home.models())) {
                out.push_back(entry.path().lexically_relative(home.models()).generic_string());
            }
        }
        return out;
    }
};

constexpr const char* kCancelSaid = "cancelled -- nothing was written";

/// The stream a Ctrl-C'd pull leaves: the job first, every line whole, and
/// its last word the cancel.
void check_cancelled_stream(const Ended& ended) {
    INFO(ended.out);
    INFO(ended.err);
    CHECK(ended.code == apogee::commands::kCancelled);
    CHECK(ended.err.find(std::string{"apogee models: "} + kCancelSaid) != std::string::npos);
    const std::vector<nlohmann::json> events = ended.events();
    REQUIRE(events.size() >= 2);
    CHECK(events.front().at("type") == "job");
    CHECK(events.front().at("verb") == "models pull");
    CHECK(events.back() == nlohmann::json{{"type", "error"}, {"message", kCancelSaid}});
    std::size_t errors = 0;
    for (const nlohmann::json& event : events) {
        errors += event.at("type") == "error" ? 1U : 0U;
        CHECK(event.at("type") != "document");
    }
    CHECK(errors == 1);
}

}  // namespace

TEST_CASE("a plain pull Ctrl-C'd mid-download ends its stream on the cancel, nothing kept",
          "[commands][models][job][interrupt]") {
    const Home home;
    const std::string gguf = apogee::testing::minimal_gguf("llama") + std::string(4096, '\0');
    const Ended ended = home.pull(
        {.ref = "org/tiny-GGUF:tiny.gguf", .stream = true},
        std::make_unique<ScriptedHub>(
            "org/tiny-GGUF", std::map<std::string, std::string>{{"tiny.gguf", gguf}}, "tiny.gguf"));
    check_cancelled_stream(ended);
    // The bytes moved before the Ctrl-C were said; nothing of them stays.
    bool moved = false;
    for (const nlohmann::json& event : ended.events()) {
        moved = moved || (event.at("type") == "bytes" && event.at("done").get<int>() > 0);
    }
    CHECK(moved);
    CHECK(home.stored().empty());
}

TEST_CASE("a plain pull Ctrl-C'd in its listing request is a cancel, not a failure",
          "[commands][models][job][interrupt]") {
    const Home home;
    const Ended ended = home.pull(
        {.ref = "org/tiny-GGUF", .stream = true},
        std::make_unique<ScriptedHub>(
            "org/tiny-GGUF", std::map<std::string, std::string>{{"tiny.gguf", "GGUF"}}, "listing"));
    check_cancelled_stream(ended);
    CHECK(home.stored().empty());

    // As text: the same exit and words, stdout no stream.
    const Ended text = home.pull(
        {.ref = "org/tiny-GGUF"},
        std::make_unique<ScriptedHub>(
            "org/tiny-GGUF", std::map<std::string, std::string>{{"tiny.gguf", "GGUF"}}, "listing"));
    CHECK(text.code == apogee::commands::kCancelled);
    CHECK(text.err == std::string{"apogee models: "} + kCancelSaid + "\n");
    CHECK(text.out.find("{\"") == std::string::npos);
}

TEST_CASE("a --safetensors pull Ctrl-C'd on its second shard ends its stream on the cancel",
          "[commands][models][job][interrupt]") {
    const Home home;
    const Ended ended =
        home.pull({.ref = "org/tiny", .safetensors = true, .stream = true},
                  std::make_unique<ScriptedHub>(
                      "org/tiny",
                      std::map<std::string, std::string>{
                          {"config.json", R"({"architectures": ["LlamaForCausalLM"]})"},
                          {"model-00001-of-00002.safetensors", shard(2048)},
                          {"model-00002-of-00002.safetensors", shard(4096)}},
                      "model-00002-of-00002.safetensors"));
    check_cancelled_stream(ended);
    // The first shard landed in the tree's staging before the Ctrl-C, and
    // went with it.
    CHECK(home.stored().empty());
}

TEST_CASE("the register chain's download Ctrl-C'd: where it stopped said, the cancel last",
          "[commands][models][job][interrupt][chain]") {
    const Home home;
    const Ended ended =
        home.pull({.ref = "org/tiny", .safetensors = true, .registers = true, .stream = true},
                  std::make_unique<ScriptedHub>(
                      "org/tiny",
                      std::map<std::string, std::string>{
                          {"config.json", R"({"architectures": ["LlamaForCausalLM"]})"},
                          {"model.safetensors", shard(4096)}},
                      "model.safetensors"));
    check_cancelled_stream(ended);
    const std::vector<nlohmann::json> events = ended.events();
    std::vector<std::string> stages;
    std::vector<std::string> notices;
    for (const nlohmann::json& event : events) {
        if (event.at("type") == "stage") {
            stages.push_back(event.at("label").get<std::string>() + " " +
                             event.at("state").get<std::string>());
        } else if (event.at("type") == "notice") {
            notices.push_back(event.at("text").get<std::string>());
        }
    }
    CHECK(stages == std::vector<std::string>{"pull org/tiny's full weights started",
                                             "pull org/tiny's full weights failed"});
    // The chain's own words come before the stream's last: the cancel.
    REQUIRE(notices.size() >= 3);
    CHECK(notices[notices.size() - 3] ==
          "stopped at [1/3] pull org/tiny's full weights -- what the stages before it made is "
          "kept.");
    CHECK(notices[notices.size() - 2] == "resume with:");
    CHECK(notices.back() == "  apogee models pull org/tiny --safetensors --register");
    CHECK(home.stored().empty());
}
