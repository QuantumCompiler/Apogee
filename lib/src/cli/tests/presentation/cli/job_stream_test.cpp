#include "cli/job_stream.h"

#include <CLI/CLI.hpp>
#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <random>
#include <sstream>
#include <stdexcept>
#include <string>
#include <vector>

#include "contracts/sha256.h"
#include "support/cli_home.h"
#include "support/env_guard.h"
#include "support/gguf_builder.h"

/// A long job's machine face (52b): stdout's prose relayed as `notice`
/// events, and `models pull --output-format stream-json` end to end over a
/// local Ollama store -- the job, the bytes, each stored set's document --
/// the same words the text run prints, by the same producer.
namespace {

using apogee::testing::CliHome;

[[nodiscard]] std::vector<nlohmann::json> events_of(const std::string& text) {
    std::vector<nlohmann::json> out;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);) {
        const nlohmann::json event = nlohmann::json::parse(line, nullptr, false);
        INFO(line);
        REQUIRE(event.is_object());
        REQUIRE(event.contains("type"));
        out.push_back(event);
    }
    return out;
}

[[nodiscard]] std::vector<std::string> nonblank_lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);) {
        if (line.find_first_not_of(" \t") != std::string::npos) {
            out.push_back(line);
        }
    }
    return out;
}

/// An Ollama store holding one small model, its digest the bytes' own.
struct OllamaStore {
    apogee::testing::TempDir dir{"job-ollama-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"OLLAMA_MODELS", dir.path().string()};

    OllamaStore() {
        const std::string gguf = apogee::testing::minimal_gguf("llama");
        const std::string digest = apogee::models::sha256_hex(gguf);
        const std::filesystem::path blob = dir.path() / "blobs" / ("sha256-" + digest);
        const std::filesystem::path manifest =
            dir.path() / "manifests" / "registry.ollama.ai" / "library" / "tiny" / "latest";
        std::filesystem::create_directories(blob.parent_path());
        std::filesystem::create_directories(manifest.parent_path());
        std::ofstream{blob, std::ios::binary} << gguf;
        std::ofstream{manifest, std::ios::binary}
            << R"({"schemaVersion":2,"layers":[{"mediaType":"application/vnd.ollama.image.model",)"
            << R"("digest":"sha256:)" << digest << R"(","size":)" << gguf.size() << "}]}";
    }
};

constexpr const char* kConfig = R"(backends:
  local:
    type: mock
    model: mock-1
models:
  default: local
)";

}  // namespace

TEST_CASE("an open stream relays stdout's prose as notices, and gives stdout back",
          "[commands][machine][job]") {
    std::ostringstream captured;
    std::streambuf* const before = std::cout.rdbuf(captured.rdbuf());
    {
        apogee::commands::JobStream job{"models pull"};
        CHECK(apogee::commands::JobStream::current() == &job);
        std::cout << "downloading x\n\n   \n";
        std::cout << "a line in " << 2 << " pieces\r\n";
        std::cout << "no newline at the end";
    }
    CHECK(apogee::commands::JobStream::current() == nullptr);
    CHECK(std::cout.rdbuf() == captured.rdbuf());
    std::cout.rdbuf(before);
    const std::vector<nlohmann::json> events = events_of(captured.str());
    REQUIRE(events.size() == 4);
    CHECK(events[0].at("type") == "job");
    CHECK(events[0].at("verb") == "models pull");
    CHECK(events[1] == nlohmann::json{{"type", "notice"}, {"text", "downloading x"}});
    CHECK(events[2].at("text") == "a line in 2 pieces");
    CHECK(events[3].at("text") == "no newline at the end");
}

TEST_CASE("a stream's first failure is its error, said once", "[commands][machine][job]") {
    std::ostringstream captured;
    std::streambuf* const before = std::cout.rdbuf(captured.rdbuf());
    {
        apogee::commands::JobStream job{"voice say"};
        job.failed("cancelled -- nothing was written");
        job.failed("a later failure");
        CHECK(job.has_failed());
    }
    std::cout.rdbuf(before);
    const std::vector<nlohmann::json> events = events_of(captured.str());
    REQUIRE(events.size() == 2);
    CHECK(events[1] ==
          nlohmann::json{{"type", "error"}, {"message", "cancelled -- nothing was written"}});
}

TEST_CASE("a stream's error is its last event, whatever the run says after it fails",
          "[commands][machine][job]") {
    // 55c: a chain says where it stopped and how to resume after its stage
    // failed; the stream still ends on the error.
    std::ostringstream captured;
    std::streambuf* const before = std::cout.rdbuf(captured.rdbuf());
    {
        apogee::commands::JobStream job{"models pull"};
        job.failed("cancelled -- nothing was written");
        std::cout << "stopped at [1/3] pull\nresume with:\n  apogee models pull org/m";
    }
    std::cout.rdbuf(before);
    const std::vector<nlohmann::json> events = events_of(captured.str());
    REQUIRE(events.size() == 5);
    CHECK(events[1].at("text") == "stopped at [1/3] pull");
    CHECK(events[3].at("text") == "  apogee models pull org/m");
    CHECK(events[4] ==
          nlohmann::json{{"type", "error"}, {"message", "cancelled -- nothing was written"}});
}

TEST_CASE("a failure no stream was told still ends it on error, a told one standing",
          "[commands][machine][job]") {
    // M29: training's backstop (55h), shared with the media verbs -- a
    // refusal raised below the verb said on stderr alone, the stream ending
    // on its `job` line.
    const auto streamed = [](const std::function<void()>& body) {
        std::ostringstream captured;
        std::streambuf* const before = std::cout.rdbuf(captured.rdbuf());
        bool rethrown = false;
        {
            apogee::commands::JobStream job{"video generate"};
            try {
                apogee::commands::run_job_body(body);
            } catch (const std::exception&) {
                rethrown = true;
            }
        }
        std::cout.rdbuf(before);
        CHECK(rethrown);
        return events_of(captured.str());
    };
    std::vector<nlohmann::json> events = streamed([] { throw CLI::RuntimeError(1); });
    REQUIRE(events.size() == 2);
    CHECK(events[1] ==
          nlohmann::json{{"type", "error"}, {"message", "the run failed; stderr says why"}});

    // Told before it threw, its own words stand.
    events = streamed([] {
        apogee::commands::tell_job_failure("torch and diffusers are not installed");
        throw CLI::RuntimeError(1);
    });
    REQUIRE(events.size() == 2);
    CHECK(events[1].at("message") == "torch and diffusers are not installed");

    // Anything else, in its own words.
    events = streamed([] { throw std::runtime_error("the disk is full"); });
    REQUIRE(events.size() == 2);
    CHECK(events[1].at("message") == "the disk is full");

    // With no stream open, the body alone: telling nothing, thrown on.
    std::ostringstream captured;
    std::streambuf* const before = std::cout.rdbuf(captured.rdbuf());
    CHECK_THROWS_AS(apogee::commands::run_job_body([] {
                        apogee::commands::tell_job_failure("nobody hears this");
                        throw CLI::RuntimeError(1);
                    }),
                    CLI::RuntimeError);
    std::cout.rdbuf(before);
    CHECK(captured.str().empty());
}

TEST_CASE("models pull as a stream: the bytes, the same words, the stored set's document",
          "[commands][models][job]") {
    const OllamaStore store;
    const CliHome home{kConfig};

    std::string out;
    std::string err;
    INFO(err);
    REQUIRE(home.run({"models", "pull", "tiny:latest", "--output-format", "stream-json"}, &out,
                     &err) == 0);
    const std::vector<nlohmann::json> events = events_of(out);
    REQUIRE(events.size() >= 3);
    CHECK(events.front().at("type") == "job");
    CHECK(events.front().at("verb") == "models pull");

    // The copy's bytes, each of the total, the last of it all.
    std::vector<nlohmann::json> bytes;
    std::vector<std::string> notices;
    for (const nlohmann::json& event : events) {
        if (event.at("type") == "bytes") {
            bytes.push_back(event);
        } else if (event.at("type") == "notice") {
            notices.push_back(event.at("text").get<std::string>());
        }
    }
    REQUIRE_FALSE(bytes.empty());
    CHECK(bytes.back().at("done") == bytes.back().at("total"));

    // The stored set, as `models info <it> --output-format json` says it.
    const nlohmann::json& last = events.back();
    REQUIRE(last.at("type") == "document");
    const std::string handle = last.at("document").at("name").get<std::string>();
    CHECK(handle.find("/gguf/") != std::string::npos);
    std::string info;
    REQUIRE(home.run({"models", "info", handle, "--output-format", "json"}, &info, &err) == 0);
    CHECK(last.at("document") == nlohmann::json::parse(info));

    // Pulled again as text: the same lines, by the same producer -- the
    // stream's notices are the people's words, no more and no fewer.
    std::filesystem::remove_all(home.models());
    std::string text;
    REQUIRE(home.run({"models", "pull", "tiny:latest"}, &text, &err) == 0);
    CHECK(text.find("{\"") == std::string::npos);
    CHECK(nonblank_lines(text) == notices);
}

TEST_CASE("models pull has no document to print alone", "[commands][models][job]") {
    const CliHome home{kConfig};
    std::string out;
    std::string err;
    CHECK(home.run({"models", "pull", "tiny:latest", "--output-format", "json"}, &out, &err) != 0);
    CHECK(err.find("expected 'text' or 'stream-json'") != std::string::npos);
    CHECK(out.empty());
}
