#include "cli/datasets.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <iostream>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/assets.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "contracts/types.h"
#include "logger/session.h"
#include "support/env_guard.h"
#include "training/datasets.h"

/// `apogee datasets` on the scripted mock: create from the template, from
/// nothing and from the user's sessions with every filter; synth through a
/// teacher that must be named and must not be a vendor CLI, de-duplicating
/// and stopping at the bound, refusing an existing dataset; kits, list, info,
/// delete; prepare refusing on a pipe before the environment exists, naming
/// `apogee train setup`.
namespace {

using apogee::training::chat_line;

constexpr std::string_view kBatch =
    R"([{"prompt": "What is 15% of 240?", "completion": "36"},
        {"prompt": "what is 15% of 240?", "completion": "dup"},
        {"prompt": "If 3x = 21, what is x?", "completion": "7"}])";

void write(const std::filesystem::path& path, std::string_view content) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream{path, std::ios::binary} << content;
}

std::string bytes(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::string script_of(std::string_view text, bool metered = false) {
    nlohmann::json script{{"turns", nlohmann::json::array({{{"text", std::string{text}}}})}};
    if (metered) {
        script["metered"] = true;
    }
    return script.dump();
}

struct Fixture {
    apogee::testing::TempDir home{"datasets-cli-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";

    Fixture() {
        write(home.path() / "teacher.json", script_of(kBatch));
        write(home.path() / "metered.json", script_of(kBatch, true));
        write(home.path() / "prose.json", script_of("I only speak prose."));
        std::string config = "models:\n  default: teacher\nbackends:\n";
        config += "  teacher:\n    type: mock\n    model_path: " +
                  (home.path() / "teacher.json").string() + "\n";
        config +=
            "  paid:\n    type: mock\n    model_path: " + (home.path() / "metered.json").string() +
            "\n";
        config +=
            "  prose:\n    type: mock\n    model_path: " + (home.path() / "prose.json").string() +
            "\n";
        config += "  vendor:\n    type: claude-cli\n";
        write(config_path, config);
        REQUIRE(apogee::harness::seed_data_directory(home.path()).ok());
    }

    int run(const std::vector<std::string>& args, std::string* out = nullptr,
            std::string* err = nullptr) const {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        std::istringstream fed{std::string{}};
        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        int code = -1;
        try {
            apogee::commands::RootCommand root{apogee::commands::default_registry()};
            std::vector<std::string> full{"--config", config_path.string()};
            full.insert(full.end(), args.begin(), args.end());
            std::vector<const char*> argv{"apogee"};
            for (const std::string& arg : full) {
                argv.push_back(arg.c_str());
            }
            code = root.run(static_cast<int>(argv.size()), argv.data());
        } catch (...) {
            std::cout.rdbuf(old_out);
            std::cerr.rdbuf(old_err);
            std::cin.rdbuf(old_in);
            throw;
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        std::cin.rdbuf(old_in);
        if (out != nullptr) {
            *out = captured_out.str();
        }
        if (err != nullptr) {
            *err = captured_err.str();
        }
        return code;
    }

    [[nodiscard]] std::filesystem::path dataset(std::string_view name) const {
        return home.path() / "training" / "datasets" / (std::string{name} + ".jsonl");
    }

    void save_session(std::string backend, std::string started_at,
                      std::vector<apogee::harness::ChatMessage> messages) const {
        apogee::logger::Session session;
        session.chat_id = apogee::logger::new_chat_id();
        session.backend = std::move(backend);
        session.started_at = std::move(started_at);
        session.messages = std::move(messages);
        apogee::logger::save(session);
    }
};

}  // namespace

TEST_CASE("datasets create scaffolds the template, refuses a duplicate, overwrites on --force",
          "[commands][datasets][create]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"datasets", "create", "starter"}, &out, &err) == 0);
    CHECK(out.find("2 example(s)") != std::string::npos);
    const std::vector<std::string> expected = apogee::training::template_lines();
    CHECK(bytes(fixture.dataset("starter")) == expected[0] + "\n" + expected[1] + "\n");

    CHECK(fixture.run({"datasets", "create", "starter"}, &out, &err) == 1);
    CHECK(err.find("already exists") != std::string::npos);
    CHECK(err.find("--force") != std::string::npos);

    REQUIRE(fixture.run({"datasets", "create", "starter", "--from", "empty", "--force"}, &out,
                        &err) == 0);
    CHECK(bytes(fixture.dataset("starter")).empty());
    CHECK(out.find("0 example(s)") != std::string::npos);

    CHECK(fixture.run({"datasets", "create", "x", "--from", "chat-logs"}, &out, &err) == 1);
    CHECK(err.find("--from must be") != std::string::npos);
    CHECK(fixture.run({"datasets", "create", "../escape"}, &out, &err) == 1);
}

TEST_CASE("datasets create --from sessions mines completed exchanges with the filters",
          "[commands][datasets][sessions]") {
    using apogee::harness::ChatMessage;
    const Fixture fixture;
    fixture.save_session("teacher", "2026-09-19T10:00:00Z",
                         {ChatMessage::user("What is 2+2?"), ChatMessage::assistant("4"),
                          ChatMessage::user("unanswered")});
    fixture.save_session("paid", "2026-09-01T10:00:00Z",
                         {ChatMessage::user("Q"), ChatMessage::assistant("A")});
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"datasets", "create", "mine", "--from", "sessions"}, &out, &err) == 0);
    CHECK(out.find("2 example(s) from 2 session(s), 1 exchange(s) skipped") != std::string::npos);
    const std::string content = bytes(fixture.dataset("mine"));
    CHECK(content.find(chat_line("What is 2+2?", "4")) != std::string::npos);
    CHECK(content.find(chat_line("Q", "A")) != std::string::npos);

    REQUIRE(
        fixture.run({"datasets", "create", "paid-only", "--from", "sessions", "--backend", "paid"},
                    &out, &err) == 0);
    CHECK(bytes(fixture.dataset("paid-only")) == chat_line("Q", "A") + "\n");

    REQUIRE(
        fixture.run({"datasets", "create", "recent", "--from", "sessions", "--since", "2026-09-10"},
                    &out, &err) == 0);
    CHECK(bytes(fixture.dataset("recent")) == chat_line("What is 2+2?", "4") + "\n");

    CHECK(fixture.run({"datasets", "create", "bad", "--from", "sessions", "--since", "soon"}, &out,
                      &err) == 1);
    CHECK(err.find("--since: invalid date") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("bad")));
}

TEST_CASE("datasets synth distils through a named teacher, de-duplicates, and stops at the bound",
          "[commands][datasets][synth]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"datasets", "synth", "maths", "--teacher", "teacher", "--kit", "reasoning",
                         "--count", "2"},
                        &out, &err) == 0);
    INFO(err);
    CHECK(out.find("2 example(s)") != std::string::npos);
    CHECK(bytes(fixture.dataset("maths")) == chat_line("What is 15% of 240?", "36") + "\n" +
                                                 chat_line("If 3x = 21, what is x?", "7") + "\n");
    // The mock is not an API backend: one batch in flight.
    CHECK(err.find("1 batch(es) in flight") != std::string::npos);

    // The same teacher can only ever produce these two: asked for more, the
    // run ends at the call bound with what it got rather than spinning.
    REQUIRE(fixture.run({"datasets", "synth", "more", "--teacher", "teacher", "--kit", "reasoning",
                         "--count", "5"},
                        &out, &err) == 0);
    CHECK(out.find("2 example(s)") != std::string::npos);
    CHECK(out.find("batch(es) skipped") == std::string::npos);

    CHECK(fixture.run({"datasets", "synth", "maths", "--teacher", "teacher", "--kit", "reasoning"},
                      &out, &err) == 1);
    CHECK(err.find("already exists") != std::string::npos);
    REQUIRE(fixture.run({"datasets", "synth", "maths", "--teacher", "teacher", "--kit", "reasoning",
                         "--count", "1", "--force"},
                        &out, &err) == 0);
    CHECK(bytes(fixture.dataset("maths")) == chat_line("What is 15% of 240?", "36") + "\n");
}

TEST_CASE(
    "datasets synth refuses an unknown teacher, a vendor CLI, an unknown kit, and a "
    "teacher that never produces; a metered teacher is fine because it was named",
    "[commands][datasets][synth][policy]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    CHECK(fixture.run({"datasets", "synth", "a", "--teacher", "nope", "--kit", "reasoning"}, &out,
                      &err) == 1);
    CHECK(err.find("'nope' is not a configured backend") != std::string::npos);

    CHECK(fixture.run({"datasets", "synth", "a", "--teacher", "vendor", "--kit", "reasoning"}, &out,
                      &err) == 1);
    CHECK(err.find("vendor-CLI backend") != std::string::npos);

    CHECK(fixture.run({"datasets", "synth", "a", "--teacher", "teacher", "--kit", "nokit"}, &out,
                      &err) == 1);
    CHECK(err.find("no kit named 'nokit'") != std::string::npos);

    // A kit that parses but cannot gate is refused by name, before any call.
    write(fixture.home.path() / "training" / "kits" / "noeval.yaml",
          "name: noeval\nsynth:\n  system: teach\neval: []\n");
    CHECK(fixture.run({"datasets", "synth", "a", "--teacher", "teacher", "--kit", "noeval"}, &out,
                      &err) == 1);
    CHECK(err.find("kit 'noeval': at least one eval item") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("a")));

    CHECK(fixture.run({"datasets", "synth", "a", "--teacher", "prose", "--kit", "reasoning",
                       "--count", "1"},
                      &out, &err) == 1);
    CHECK(err.find("no usable examples") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("a")));

    REQUIRE(fixture.run({"datasets", "synth", "a", "--teacher", "paid", "--kit", "reasoning",
                         "--count", "1"},
                        &out, &err) == 0);
    CHECK(std::filesystem::exists(fixture.dataset("a")));
}

TEST_CASE("datasets kits, list, info and delete", "[commands][datasets][listing]") {
    const Fixture fixture;
    std::string out;
    std::string err;
    REQUIRE(fixture.run({"datasets", "kits"}, &out, &err) == 0);
    CHECK(out.find("instruction-following") != std::string::npos);
    CHECK(out.find("reasoning") != std::string::npos);
    CHECK(out.find("structured-output") != std::string::npos);
    CHECK(out.find("summarization") != std::string::npos);
    CHECK(out.find("tool-use") == std::string::npos);

    REQUIRE(fixture.run({"datasets", "list"}, &out, &err) == 0);
    CHECK(out.find("no datasets") != std::string::npos);
    REQUIRE(fixture.run({"datasets", "create", "one"}, &out, &err) == 0);
    REQUIRE(fixture.run({"datasets", "list"}, &out, &err) == 0);
    CHECK(out.find("one") != std::string::npos);
    CHECK(out.find("chat") != std::string::npos);
    REQUIRE(fixture.run({"datasets", "info", "one"}, &out, &err) == 0);
    CHECK(out.find("lines:  2") != std::string::npos);
    CHECK(fixture.run({"datasets", "info", "two"}, &out, &err) == 1);

    REQUIRE(fixture.run({"datasets", "delete", "one"}, &out, &err) == 0);
    CHECK(out.find("--yes") != std::string::npos);
    CHECK(std::filesystem::exists(fixture.dataset("one")));
    REQUIRE(fixture.run({"datasets", "delete", "one", "-y"}, &out, &err) == 0);
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("one")));
    CHECK(fixture.run({"datasets", "delete", "one", "-y"}, &out, &err) == 1);
}

TEST_CASE("datasets prepare refuses on a pipe until the environment exists, naming the command",
          "[commands][datasets][prepare][python]") {
    const Fixture fixture;
    write(fixture.home.path() / "raw.jsonl", "{\"prompt\": \"p\", \"completion\": \"c\"}\n");
    std::string out;
    std::string err;
    CHECK(fixture.run({"datasets", "prepare", (fixture.home.path() / "raw.jsonl").string()}, &out,
                      &err) == 1);
    CHECK(err.find("apogee train setup") != std::string::npos);
    CHECK(err.find("never touching the system Python") != std::string::npos);
    CHECK_FALSE(std::filesystem::exists(fixture.dataset("raw")));

    CHECK(fixture.run({"datasets", "prepare", (fixture.home.path() / "missing.csv").string()}, &out,
                      &err) == 1);
    CHECK(err.find("no such file") != std::string::npos);
}

TEST_CASE("the teacher resolution and the parallel rule", "[commands][datasets][teacher]") {
    apogee::harness::Config config = apogee::harness::parse_config(
        "backends:\n  api:\n    type: openai\n    api_key: k\n  local:\n    type: mock\n  cli:\n"
        "    type: gemini-cli\n",
        "<test>");
    apogee::commands::TeacherResolution api = apogee::commands::resolve_teacher(config, "api");
    CHECK(api.key == "api");
    CHECK(api.parallel_safe);
    CHECK(apogee::commands::effective_parallel(api, 4) == 4);
    CHECK(apogee::commands::effective_parallel(api, 0) == 1);
    CHECK(apogee::commands::effective_parallel(api, 99) == 16);
    apogee::commands::TeacherResolution local = apogee::commands::resolve_teacher(config, "local");
    CHECK(local.key == "local");
    CHECK_FALSE(local.parallel_safe);
    CHECK(apogee::commands::effective_parallel(local, 4) == 1);
    CHECK(apogee::commands::resolve_teacher(config, "cli").key.empty());
    CHECK(apogee::commands::resolve_teacher(config, "").error.find("--teacher") !=
          std::string::npos);
}

// --- M30: `datasets pull` streams as a job ------------------------------------------
//
// The pull's progress as events, the text run's words as notices, and where
// the dataset landed last -- over a scripted Hugging Face, the pull's own
// transport seam. A Ctrl-C ends the stream on its error and leaves nothing.

#include <CLI/CLI.hpp>

#include <algorithm>
#include <csignal>
#include <map>
#include <memory>
#include <utility>

#include "cli/helpers.h"
#include "transport/http_client.h"

namespace {

using apogee::backends::HttpRequest;
using apogee::backends::HttpResponse;

/// A Hugging Face dataset repository in memory: its tree and each file --
/// the one named `interrupted` met by a Ctrl-C halfway through its bytes, or
/// the listing itself when `interrupted` is `listing`.
class ScriptedDatasets final : public apogee::backends::HttpTransport {
public:
    ScriptedDatasets(std::string repo, std::map<std::string, std::string> files,
                     std::string interrupted = {})
        : repo_{std::move(repo)}, files_{std::move(files)}, interrupted_{std::move(interrupted)} {}

    HttpResponse send(const HttpRequest& request, const apogee::backends::BodySink& sink,
                      const apogee::harness::CancellationToken& cancellation) override {
        cancellation.throw_if_cancelled();
        HttpResponse response;
        response.status = 200;
        if (request.url ==
            "https://huggingface.co/api/datasets/" + repo_ + "/tree/main?recursive=true") {
            if (interrupted_ == "listing") {
                (void)std::raise(SIGINT);
                cancellation.throw_if_cancelled();
            }
            nlohmann::json tree = nlohmann::json::array();
            for (const auto& [path, bytes] : files_) {
                tree.push_back({{"type", "file"}, {"path", path}, {"size", bytes.size()}});
            }
            response.body = tree.dump();
            return response;
        }
        const std::string prefix = "https://huggingface.co/datasets/" + repo_ + "/resolve/main/";
        const auto file = request.url.starts_with(prefix)
                              ? files_.find(request.url.substr(prefix.size()))
                              : files_.end();
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

private:
    std::string repo_;
    std::map<std::string, std::string> files_;
    std::string interrupted_;
};

/// One client's handle onto the one repository.
class DatasetsHandle final : public apogee::backends::HttpTransport {
public:
    explicit DatasetsHandle(std::shared_ptr<ScriptedDatasets> hub) : hub_{std::move(hub)} {}

    HttpResponse send(const HttpRequest& request, const apogee::backends::BodySink& sink,
                      const apogee::harness::CancellationToken& cancellation) override {
        return hub_->send(request, sink, cancellation);
    }

private:
    std::shared_ptr<ScriptedDatasets> hub_;
};

/// What a pull ended with: its exit code (0 when it returned) and its streams.
struct Pulled {
    int code = 0;
    std::string out{};
    std::string err{};

    [[nodiscard]] std::vector<nlohmann::json> events() const {
        std::vector<nlohmann::json> found;
        std::istringstream in{out};
        for (std::string line; std::getline(in, line);) {
            INFO(line);
            const nlohmann::json event = nlohmann::json::parse(line, nullptr, false);
            REQUIRE(event.is_object());
            found.push_back(event);
        }
        return found;
    }
};

[[nodiscard]] Pulled pull_dataset(const apogee::commands::DatasetPullRequest& request,
                                  std::shared_ptr<ScriptedDatasets> hub) {
    const std::ostringstream out;
    const std::ostringstream err;
    std::streambuf* const old_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf* const old_err = std::cerr.rdbuf(err.rdbuf());
    Pulled pulled;
    try {
        apogee::commands::run_dataset_pull(
            request, [hub]() -> std::unique_ptr<apogee::backends::HttpTransport> {
                return std::make_unique<DatasetsHandle>(hub);
            });
    } catch (const CLI::RuntimeError& e) {
        pulled.code = e.get_exit_code();
    }
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    pulled.out = out.str();
    pulled.err = err.str();
    return pulled;
}

/// Every entry under the raw datasets folder, relative to it.
[[nodiscard]] std::vector<std::string> raw_entries(const std::filesystem::path& raw) {
    std::vector<std::string> out;
    std::error_code code;
    if (std::filesystem::exists(raw, code)) {
        for (const auto& entry : std::filesystem::recursive_directory_iterator(raw)) {
            out.push_back(entry.path().lexically_relative(raw).generic_string());
        }
    }
    std::ranges::sort(out);
    return out;
}

[[nodiscard]] std::vector<std::string> said_lines(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in{text};
    for (std::string line; std::getline(in, line);) {
        if (line.find_first_not_of(" \t") != std::string::npos) {
            out.push_back(line);
        }
    }
    return out;
}

const std::map<std::string, std::string> kDatasetFiles{
    {"README.md", "# a card\n"},
    {"data/train.jsonl", "{\"text\": \"one\"}\n{\"text\": \"two\"}\n"},
    {"data/test.jsonl", std::string(4096, 'x') + "\n"}};

}  // namespace

TEST_CASE("datasets pull refused as a stream is its job and its error, the words on stderr too",
          "[commands][datasets][pull][job]") {
    // M30: `datasets pull` had no stream -- the flag was refused as an
    // argument nobody expected.
    const Fixture fixture;
    std::string out;
    std::string err;
    CHECK(fixture.run({"datasets", "pull", "nope", "--output-format", "stream-json"}, &out, &err) ==
          1);
    INFO(out << err);
    const Pulled refused{.code = 1, .out = out, .err = err};
    const std::vector<nlohmann::json> events = refused.events();
    REQUIRE(events.size() == 2);
    CHECK(events[0].at("type") == "job");
    CHECK(events[0].at("verb") == "datasets pull");
    const std::string said = "'nope' is not a Hugging Face dataset ref (expected owner/name)";
    CHECK(events[1] == nlohmann::json{{"type", "error"}, {"message", said}});
    CHECK(err == "apogee datasets: " + said + "\n");
    // A pull has no document to print alone.
    CHECK(fixture.run({"datasets", "pull", "org/ds", "--output-format", "json"}, &out, &err) != 0);
    CHECK(err.find("expected 'text' or 'stream-json'") != std::string::npos);
    CHECK(out.empty());
}

TEST_CASE("datasets pull streams as a job: its bytes, the text run's words, where it landed last",
          "[commands][datasets][pull][job]") {
    const Fixture fixture;
    const std::filesystem::path raw = fixture.home.path() / "training" / "datasets" / "raw";
    const auto hub = std::make_shared<ScriptedDatasets>("org/ds", kDatasetFiles);
    const Pulled pulled = pull_dataset({.ref = "org/ds", .stream = true}, hub);
    INFO(pulled.out << pulled.err);
    REQUIRE(pulled.code == 0);
    CHECK(pulled.err.empty());
    const std::vector<nlohmann::json> events = pulled.events();
    REQUIRE(events.size() >= 3);
    CHECK(events.front() == nlohmann::json{{"type", "job"},
                                           {"verb", "datasets pull"},
                                           {"protocol_version", 1},
                                           {"schema", events.front().at("schema")}});
    std::vector<nlohmann::json> moved;
    std::vector<std::string> notices;
    for (const nlohmann::json& event : events) {
        if (event.at("type") == "bytes") {
            moved.push_back(event);
        } else if (event.at("type") == "notice") {
            notices.push_back(event.at("text").get<std::string>());
        }
    }
    // Each file's bytes to its total, said as the files move.
    REQUIRE_FALSE(moved.empty());
    CHECK(moved.back().at("done") == moved.back().at("total"));
    CHECK(moved.back().at("count") == 2);
    // Last: where the dataset landed, the folder `datasets prepare` takes.
    const std::filesystem::path landed = raw / "org--ds";
    CHECK(events.back() ==
          nlohmann::json{{"type", "document"},
                         {"document",
                          {{"object", "dataset.pull"},
                           {"ref", "org/ds"},
                           {"path", landed.string()},
                           {"files", 2},
                           {"bytes", kDatasetFiles.at("data/train.jsonl").size() +
                                         kDatasetFiles.at("data/test.jsonl").size()}}}});
    CHECK(raw_entries(raw) == std::vector<std::string>{"org--ds", "org--ds/data",
                                                       "org--ds/data/test.jsonl",
                                                       "org--ds/data/train.jsonl"});
    CHECK(bytes(landed / "data" / "train.jsonl") == kDatasetFiles.at("data/train.jsonl"));

    // Pulled again as text: the same lines, by the same producers -- the
    // stream's notices are the person's words, and the line naming each file
    // as it moves is that file's bytes.
    std::filesystem::remove_all(landed);
    const Pulled text = pull_dataset({.ref = "org/ds"}, hub);
    REQUIRE(text.code == 0);
    CHECK(text.out.find("{\"") == std::string::npos);
    std::vector<std::string> people = said_lines(text.out);
    std::vector<std::string> files_said;
    std::erase_if(people, [&files_said](const std::string& line) {
        if (line.starts_with("  [")) {
            files_said.push_back(line);
            return true;
        }
        return false;
    });
    CHECK(people == notices);
    CHECK(people.back() == "Convert it with:  apogee datasets prepare " + landed.string());
    std::vector<std::string> files_moved;
    for (const nlohmann::json& event : moved) {
        const std::string line = "  [" + std::to_string(event.at("index").get<int>()) + "/" +
                                 std::to_string(event.at("count").get<int>()) + "] " +
                                 event.at("file").get<std::string>();
        if (files_moved.empty() || files_moved.back() != line) {
            files_moved.push_back(line);
        }
    }
    CHECK(files_moved == files_said);
}

TEST_CASE("datasets pull Ctrl-C'd mid-download ends its stream on the cancel, nothing kept",
          "[commands][datasets][pull][job][interrupt]") {
    const Fixture fixture;
    const std::filesystem::path raw = fixture.home.path() / "training" / "datasets" / "raw";
    const std::string said = "cancelled -- nothing was written";
    for (const std::string interrupted : {"data/train.jsonl", "listing"}) {
        INFO(interrupted);
        const Pulled pulled =
            pull_dataset({.ref = "org/ds", .stream = true},
                         std::make_shared<ScriptedDatasets>("org/ds", kDatasetFiles, interrupted));
        INFO(pulled.out << pulled.err);
        CHECK(pulled.code == apogee::commands::kCancelled);
        CHECK(pulled.err == "apogee datasets: " + said + "\n");
        const std::vector<nlohmann::json> events = pulled.events();
        REQUIRE(events.size() >= 2);
        CHECK(events.front().at("verb") == "datasets pull");
        CHECK(events.back() == nlohmann::json{{"type", "error"}, {"message", said}});
        for (const nlohmann::json& event : events) {
            CHECK(event.at("type") != "document");
        }
        CHECK(raw_entries(raw).empty());
    }

    // As text: the same exit and words, nothing kept, stdout no stream.
    const Pulled text = pull_dataset(
        {.ref = "org/ds"},
        std::make_shared<ScriptedDatasets>("org/ds", kDatasetFiles, "data/train.jsonl"));
    CHECK(text.code == apogee::commands::kCancelled);
    CHECK(text.err == "apogee datasets: " + said + "\n");
    CHECK(text.out.find("{\"") == std::string::npos);
    CHECK(raw_entries(raw).empty());
}

TEST_CASE("datasets pull's failures end its stream on their words, and nothing is kept",
          "[commands][datasets][pull][job]") {
    const Fixture fixture;
    const std::filesystem::path raw = fixture.home.path() / "training" / "datasets" / "raw";
    const Pulled empty = pull_dataset(
        {.ref = "org/ds", .stream = true},
        std::make_shared<ScriptedDatasets>(
            "org/ds", std::map<std::string, std::string>{{"README.md", "# a card\n"}}));
    CHECK(empty.code == 1);
    const std::string said =
        "'org/ds' holds no data files (Parquet, JSONL, JSON, CSV, Arrow, text)";
    CHECK(empty.err == "apogee datasets: " + said + "\n");
    CHECK(empty.events().back() == nlohmann::json{{"type", "error"}, {"message", said}});
    CHECK(raw_entries(raw).empty());
}
