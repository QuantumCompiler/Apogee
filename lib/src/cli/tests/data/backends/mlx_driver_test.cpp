#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

#include "platform/child_process.h"
#include "support/env_guard.h"
#include "transport/jsonl_framer.h"

/// The MLX driver's half of the protocol (27a), on bare python3.
///
/// The shipped `assets/mlx/mlx_generate.py` -- the file, not a copy -- runs
/// under stub `mlx` and `mlx_lm` packages (`scripts/stub_mlx/`): one
/// character per token, a chat template that writes each message on a line,
/// a cache that is an offset. That is enough to hold the driver to everything
/// the backend relies on without Metal, weights or the network: the `ready`
/// line, text streamed and `done` counted, the session cache reused by the
/// prefix a turn shares and a side request touching none of it, calls read
/// in the model's own format (mlx_lm's parser, or Llama 3's JSON reply) and
/// a span that is not one given back as text, reasoning split by the
/// tokenizer's markers, stop strings, the sampling and the thinking switch
/// passed as sent, a cancel that leaves the model loaded, closed stdin as
/// the end, and the fatal errors naming their fix. Skipped by name on a host
/// with no python3, or one that cannot start a child (Windows).
namespace {

using nlohmann::json;

const std::filesystem::path kDriver =
    std::filesystem::path{APOGEE_ASSETS_DIR} / "mlx" / "mlx_generate.py";
const std::filesystem::path kStubs =
    std::filesystem::path{APOGEE_TESTS_DIR} / "data" / "backends" / "scripts";
const std::filesystem::path kModel = std::filesystem::path{APOGEE_TEST_FIXTURES} / "mlx" / "model";

std::string python3() {
    return apogee::platform::find_on_path("python3");
}

std::string read(const std::filesystem::path& path) {
    const std::ifstream in{path, std::ios::binary};
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

std::vector<std::string> lines_of(const std::string& text) {
    std::vector<std::string> out;
    std::istringstream in{text};
    std::string line;
    while (std::getline(in, line)) {
        out.push_back(line);
    }
    return out;
}

/// The shipped driver, running under the stubs, driven line by line.
class Driver {
public:
    /// `stubs` names the stub packages' directories, `:`-separated --
    /// `stub_mlx:stub_mlx_vlm` lays mlx-vlm beside mlx-lm (27c).
    explicit Driver(const apogee::testing::TempDir& dir,
                    std::vector<std::pair<std::string, std::string>> environment = {},
                    const std::vector<std::string>& replies = {"stub reply"},
                    const std::filesystem::path& model = kModel,
                    const std::string& stubs = "stub_mlx",
                    const std::vector<std::string>& arguments = {})
        : record_{dir.path() / "record.txt"} {
        const std::filesystem::path replies_file = dir.path() / "replies.json";
        std::ofstream{replies_file, std::ios::binary} << json(replies).dump();
        apogee::platform::ChildCommand command;
        command.program = python3();
        command.arguments = {kDriver.string(), "--model", model.string()};
        command.arguments.insert(command.arguments.end(), arguments.begin(), arguments.end());
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
        command.extra_environment.emplace_back("STUB_MLX_REPLIES", replies_file.string());
        command.extra_environment.emplace_back("STUB_MLX_RECORD", record_.string());
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

    /// Events up to and including request `id`'s `done` or `error`.
    std::vector<json> until_done(std::int64_t id) {
        std::vector<json> events;
        for (;;) {
            json event = next();
            REQUIRE_FALSE(event.is_null());
            events.push_back(event);
            const std::string type = event.value("type", "");
            if ((type == "done" || type == "error") && event.value("id", json{}) == json(id)) {
                return events;
            }
        }
    }

    /// Closes stdin and returns the exit code.
    int close() {
        child_->close_stdin();
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

    [[nodiscard]] std::vector<std::string> record() const {
        return lines_of(read(record_));
    }

private:
    void drain_stderr() {
        std::string chunk;
        while (child_->read_stderr(chunk, std::chrono::milliseconds{0}) ==
               apogee::platform::ReadStatus::Data) {
            stderr_ += chunk;
        }
    }

    std::filesystem::path record_;
    std::unique_ptr<apogee::platform::ChildProcess> child_;
    apogee::backends::JsonlFramer framer_;
    std::deque<std::string> lines_;
    std::string stderr_;
};

json generate(std::int64_t id, json messages, json tools = json::array(), const json& extra = {}) {
    json request{{"type", "generate"},
                 {"id", id},
                 {"messages", std::move(messages)},
                 {"tools", std::move(tools)},
                 {"sampling", {{"temperature", 0.0}}},
                 {"max_tokens", 400},
                 {"stop", json::array()},
                 {"thinking", true},
                 {"session", true}};
    if (extra.is_object()) {
        request.update(extra);
    }
    return request;
}

json user(const std::string& text) {
    return {{"role", "user"}, {"content", text}};
}

json read_file_tool() {
    return json::array({{{"type", "function"},
                         {"function",
                          {{"name", "read_file"},
                           {"description", "Read a file"},
                           {"parameters", {{"type", "object"}}}}}}});
}

std::string joined(const std::vector<json>& events, const std::string& type) {
    std::string out;
    for (const json& event : events) {
        if (event.value("type", "") == type) {
            out += event.value("text", "");
        }
    }
    return out;
}

std::vector<json> of_type(const std::vector<json>& events, const std::string& type) {
    std::vector<json> out;
    for (const json& event : events) {
        if (event.value("type", "") == type) {
            out.push_back(event);
        }
    }
    return out;
}

std::size_t count(const std::vector<std::string>& lines, const std::string& prefix) {
    std::size_t n = 0;
    for (const std::string& line : lines) {
        n += line.starts_with(prefix) ? 1 : 0;
    }
    return n;
}

// Both halves, as training's script cases ask them: python3 is on PATH on
// the Windows runner images, while child_process cannot start it there, so
// the interpreter alone let every case below reach start_child and fail.
#define REQUIRE_PYTHON()                                               \
    do {                                                               \
        if (!apogee::platform::supports_child_processes()) {           \
            SKIP("no child processes on this platform");               \
        }                                                              \
        if (python3().empty()) {                                       \
            SKIP("no python3 on PATH: the driver's suite cannot run"); \
        }                                                              \
    } while (false)

}  // namespace

TEST_CASE("the driver says ready with what it found, then streams a turn and counts it",
          "[backends][mlx][driver]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    Driver driver{dir, {}, {"Paris."}};

    const json ready = driver.next();
    CHECK(ready["type"] == "ready");
    CHECK(ready["protocol"] == 1);
    CHECK(ready["model_type"] == "llama");
    CHECK(ready["chat_template"] == true);
    CHECK(ready["tool_parser"].is_null());
    CHECK(ready["thinking"] == false);
    CHECK(ready["mlx_lm"] == "0.0-stub");

    driver.send(generate(1, json::array({user("Capital of France?")})));
    const std::vector<json> events = driver.until_done(1);
    CHECK(joined(events, "text") == "Paris.");
    const json& done = events.back();
    CHECK(done["type"] == "done");
    CHECK(done["finish"] == "stop");
    // "<|user|>Capital of France?\n<|assistant|>": one token per character.
    CHECK(done["prompt_tokens"] == 40);
    CHECK(done["cached_tokens"] == 0);
    CHECK(done["completion_tokens"] == 6);
    CHECK(driver.close() == 0);
}

TEST_CASE(
    "a turn that extends the last reuses the session's cache by the prefix they share, and a "
    "side request touches none of it",
    "[backends][mlx][driver][session]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    Driver driver{dir, {}, {"Paris.", "A title", "Madrid."}};
    REQUIRE(driver.next()["type"] == "ready");

    driver.send(generate(1, json::array({user("hi")})));
    const json first = driver.until_done(1).back();
    const std::int64_t prompt = first["prompt_tokens"];  // "<|user|>hi\n<|assistant|>"
    CHECK(prompt == 24);

    // A side request -- a title -- runs on a cache of its own.
    driver.send(
        generate(2, json::array({user("title this")}), json::array(), {{"session", false}}));
    const json side = driver.until_done(2).back();
    CHECK(side["cached_tokens"] == 0);

    driver.send(generate(
        3, json::array(
               {user("hi"), {{"role", "assistant"}, {"content", "Paris."}}, user("and Spain?")})));
    const json third = driver.until_done(3).back();
    // Kept: the first prompt and its answer; the end-of-turn token it ended
    // on is not what the template writes there, so it is trimmed.
    CHECK(third["cached_tokens"] == prompt + 6);
    // "\n<|user|>and Spain?\n<|assistant|>" is what it read.
    CHECK(third["prompt_tokens"] == prompt + 6 + 33);
    CHECK(driver.close() == 0);

    const std::vector<std::string> record = driver.record();
    // One session cache, one side cache -- never a second session cache.
    CHECK(count(record, "cache new") == 2);
    CHECK(count(record, "trim 1") == 1);
    // A session prompt is read up to its last token, which the step reads --
    // in two pieces, cut where the history ends and the generation prompt
    // ("<|assistant|>", 13 tokens) begins: where a checkpoint would sit.
    CHECK(count(record, "prefill 11 cached=0") == 1);
    CHECK(count(record, "prefill 12 cached=11") == 1);
    CHECK(count(record, "step fed=1 cached=23") == 1);
    // The side request, whole, on its own cache.
    CHECK(count(record, "step fed=32 cached=0") == 1);
    CHECK(count(record, "prefill 20 cached=30") == 1);
    CHECK(count(record, "prefill 12 cached=50") == 1);
    CHECK(count(record, "step fed=1 cached=62") == 1);
}

TEST_CASE(
    "a cache that cannot be cut back restarts from the checkpoint its last prompt left, and "
    "from nothing past a divergence before it",
    "[backends][mlx][driver][session]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    // A sliding window past its size, a recurrent state: no trim.
    Driver driver{dir, {{"STUB_MLX_NOTRIM", "1"}}, {"Paris.", "Madrid.", "Hello.", "Again."}};
    REQUIRE(driver.next()["type"] == "ready");
    driver.send(generate(1, json::array({user("hi")})));
    (void)driver.until_done(1);
    driver.send(generate(
        2, json::array(
               {user("hi"), {{"role", "assistant"}, {"content", "Paris."}}, user("and Spain?")})));
    // The checkpoint: the first prompt's history, before the generation
    // prompt the template may write differently next time.
    CHECK(driver.until_done(2).back()["cached_tokens"] == 11);
    driver.send(
        generate(3, json::array({{{"role", "system"}, {"content", "new rules"}}, user("hi")})));
    CHECK(driver.until_done(3).back()["cached_tokens"] == 0);
    // Longer than the checkpoint and diverging inside it: not a prefix, so
    // never restored from.
    driver.send(generate(4, json::array({{{"role", "system"}, {"content", "other rules"}},
                                         user("a question long enough to outrun it all"),
                                         {{"role", "assistant"}, {"content", "Sure."}},
                                         user("go on")})));
    CHECK(driver.until_done(4).back()["cached_tokens"] == 0);
    CHECK(driver.close() == 0);
    const std::vector<std::string> record = driver.record();
    CHECK(count(record, "prefill 39 cached=11") == 1);
    CHECK(count(record, "trim ") == 0);
    CHECK(count(record, "cache new") == 3);
}

TEST_CASE(
    "a call in the model's own format is parsed by mlx_lm's parser, and a span that does not "
    "parse comes back as text",
    "[backends][mlx][driver][tools]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    Driver driver{
        dir,
        {{"STUB_MLX_TOOLS", "json_tools"}},
        {R"(Let me look.<tool_call>{"name": "read_file", "arguments": {"path": "a.txt"}}</tool_call>)",
         "<tool_call>not json</tool_call>", "<tool_call>plain</tool_call>"}};
    const json ready = driver.next();
    CHECK(ready["tool_parser"] == "json_tools");

    driver.send(generate(1, json::array({user("read a.txt")}), read_file_tool()));
    const std::vector<json> called = driver.until_done(1);
    CHECK(joined(called, "text") == "Let me look.");
    const std::vector<json> calls = of_type(called, "tool_call");
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]["name"] == "read_file");
    CHECK(calls[0]["arguments"] == json{{"path", "a.txt"}});
    CHECK(called.back()["finish"] == "tool_calls");

    // The safety net: no call, no answer and no error is the least debuggable
    // outcome there is.
    driver.send(generate(2, json::array({user("again")}), read_file_tool()));
    const std::vector<json> broken = driver.until_done(2);
    CHECK(of_type(broken, "tool_call").empty());
    CHECK(joined(broken, "text") == "<tool_call>not json</tool_call>");
    CHECK(broken.back()["finish"] == "stop");

    // No tools offered: the markers are only text.
    driver.send(generate(3, json::array({user("no tools")})));
    CHECK(joined(driver.until_done(3), "text") == "<tool_call>plain</tool_call>");
    CHECK(driver.close() == 0);
}

TEST_CASE(
    "a reply that is a JSON call to an offered tool is one call, Llama 3's format; anything "
    "else is text",
    "[backends][mlx][driver][tools]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    Driver driver{
        dir,
        {},
        {R"({"name": "read_file", "parameters": {"path": "a.txt"}}; {"name": "read_file", "parameters": {"path": "b.txt"}})",
         R"(<|python_tag|>{"name": "read_file", "arguments": "{\"path\": \"c.txt\"}"})",
         R"({"name": "delete_everything", "parameters": {}})",
         R"(Sure: {"name": "read_file", "parameters": {"path": "d.txt"}})",
         R"([{"name": "read_file", "parameters": {"path": "e.txt"}}, {"name": "read_file", "parameters": {"path": "f.txt"}}])"}};
    REQUIRE(driver.next()["type"] == "ready");

    driver.send(generate(1, json::array({user("read")}), read_file_tool()));
    std::vector<json> events = driver.until_done(1);
    // One call: the format holds one, and the template refuses a second back.
    std::vector<json> calls = of_type(events, "tool_call");
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]["arguments"] == json{{"path", "a.txt"}});
    CHECK(joined(events, "text").empty());
    CHECK(events.back()["finish"] == "tool_calls");

    driver.send(generate(2, json::array({user("read")}), read_file_tool()));
    calls = of_type(driver.until_done(2), "tool_call");
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]["arguments"] == json{{"path", "c.txt"}});

    driver.send(generate(3, json::array({user("read")}), read_file_tool()));
    events = driver.until_done(3);
    CHECK(of_type(events, "tool_call").empty());
    CHECK(joined(events, "text") == R"({"name": "delete_everything", "parameters": {}})");

    driver.send(generate(4, json::array({user("read")}), read_file_tool()));
    events = driver.until_done(4);
    CHECK(of_type(events, "tool_call").empty());
    CHECK(joined(events, "text") ==
          R"(Sure: {"name": "read_file", "parameters": {"path": "d.txt"}})");

    // A list of calls is still one: the first.
    driver.send(generate(5, json::array({user("read")}), read_file_tool()));
    calls = of_type(driver.until_done(5), "tool_call");
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]["arguments"] == json{{"path", "e.txt"}});
    CHECK(driver.close() == 0);
}

TEST_CASE("reasoning is split out by the tokenizer's own markers, an opened block included",
          "[backends][mlx][driver][thinking]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    {
        Driver driver{dir, {{"STUB_MLX_THINK", "1"}}, {"<think>pondering</think>Answer."}};
        CHECK(driver.next()["thinking"] == true);
        driver.send(generate(1, json::array({user("q")})));
        const std::vector<json> events = driver.until_done(1);
        CHECK(joined(events, "reasoning") == "pondering");
        CHECK(joined(events, "text") == "Answer.");
        CHECK(driver.close() == 0);
    }
    {
        // The template's generation prompt opened the block itself.
        Driver driver{dir,
                      {{"STUB_MLX_THINK", "1"}, {"STUB_MLX_OPEN_THINK", "1"}},
                      {"still thinking</think>Done."}};
        REQUIRE(driver.next()["type"] == "ready");
        driver.send(generate(1, json::array({user("q")})));
        const std::vector<json> events = driver.until_done(1);
        CHECK(joined(events, "reasoning") == "still thinking");
        CHECK(joined(events, "text") == "Done.");
        CHECK(driver.close() == 0);
    }
}

TEST_CASE("a stop string ends the reply, and a base model stops at the next user turn",
          "[backends][mlx][driver][stop]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    {
        Driver driver{dir, {}, {"abcENDxyz"}};
        REQUIRE(driver.next()["type"] == "ready");
        driver.send(
            generate(1, json::array({user("q")}), json::array(), {{"stop", json::array({"END"})}}));
        const std::vector<json> events = driver.until_done(1);
        CHECK(joined(events, "text") == "abc");
        CHECK(events.back()["finish"] == "stop");
        CHECK(driver.close() == 0);
    }
    {
        Driver driver{dir, {{"STUB_MLX_TEMPLATE", "0"}}, {"Hi.\nUser: and more"}};
        CHECK(driver.next()["chat_template"] == false);
        driver.send(generate(1, json::array({user("hello")})));
        CHECK(joined(driver.until_done(1), "text") == "Hi.");
        CHECK(driver.close() == 0);
    }
}

TEST_CASE("the sampling, the seed and the thinking switch reach mlx_lm as sent",
          "[backends][mlx][driver][sampling]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    Driver driver{dir, {}, {"ok"}};
    REQUIRE(driver.next()["type"] == "ready");
    driver.send(generate(1, json::array({user("q")}), json::array(),
                         {{"sampling",
                           {{"temperature", 0.7},
                            {"top_p", 0.8},
                            {"top_k", 20},
                            {"min_p", 0.05},
                            {"repetition_penalty", 1.1},
                            {"presence_penalty", 0.5},
                            {"seed", 42}}},
                          {"thinking", false}}));
    (void)driver.until_done(1);
    CHECK(driver.close() == 0);
    const std::vector<std::string> record = driver.record();
    CHECK(count(record, "sampler temp=0.7 top_p=0.8 min_p=0.05 top_k=20") == 1);
    CHECK(count(record, "processors repetition=1.1 presence=0.5") == 1);
    CHECK(count(record, "seed 42") == 1);
    // Twice: the prompt, and its history without the generation prompt.
    CHECK(count(record, "template messages=1 tools=0 thinking=False") == 2);
}

TEST_CASE("a cancel ends the generation and the model stays loaded for the next turn",
          "[backends][mlx][driver][cancel]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    Driver driver{dir, {{"STUB_MLX_DELAY_MS", "20"}}, {std::string(400, 'x'), "short"}};
    REQUIRE(driver.next()["type"] == "ready");
    driver.send(generate(1, json::array({user("go on")})));
    REQUIRE(driver.next()["type"] == "text");
    driver.send({{"type", "cancel"}, {"id", 1}});
    const json done = driver.until_done(1).back();
    CHECK(done["finish"] == "cancelled");
    CHECK(done["completion_tokens"] < 400);

    driver.send(generate(2, json::array({user("again")})));
    CHECK(joined(driver.until_done(2), "text") == "short");
    CHECK(driver.close() == 0);
    CHECK(count(driver.record(), "load ") == 1);
}

TEST_CASE("closing stdin ends the driver: in flight is cancelled and nothing queued starts",
          "[backends][mlx][driver][cancel]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    Driver driver{dir, {{"STUB_MLX_DELAY_MS", "20"}}, {std::string(400, 'x'), "never"}};
    REQUIRE(driver.next()["type"] == "ready");
    driver.send(generate(1, json::array({user("go on")})));
    driver.send(generate(2, json::array({user("queued")})));
    REQUIRE(driver.next()["type"] == "text");
    CHECK(driver.close() == 0);
    // Request 2 never started: one generation ran.
    CHECK(count(driver.record(), "step ") == 1);
}

TEST_CASE("a request the template refuses is reported, and the driver lives on",
          "[backends][mlx][driver][errors]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    Driver driver{dir, {}, {"fine"}};
    REQUIRE(driver.next()["type"] == "ready");
    driver.send(generate(1, json::array({{{"role", "refused"}, {"content", "x"}}})));
    const json error = driver.until_done(1).back();
    CHECK(error["type"] == "error");
    CHECK(error["kind"] == "request");
    CHECK(error["message"].get<std::string>().find("refuses role") != std::string::npos);

    driver.send({{"type", "summon"}, {"id", 2}});
    const json unknown = driver.until_done(2).back();
    CHECK(unknown["kind"] == "protocol");

    driver.send(generate(3, json::array({user("q")})));
    CHECK(joined(driver.until_done(3), "text") == "fine");
    CHECK(driver.close() == 0);
}

TEST_CASE("a missing mlx-lm and a failed load are fatal, each naming what to do",
          "[backends][mlx][driver][errors]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    {
        Driver driver{dir, {}, {"x"}, kModel, "stub_mlx_missing"};
        const json error = driver.next();
        CHECK(error["type"] == "error");
        CHECK(error["kind"] == "missing_dependency");
        CHECK(error["message"].get<std::string>().find("apogee train setup --with mlx") !=
              std::string::npos);
        CHECK(driver.close() == 3);
    }
    {
        Driver driver{dir, {{"STUB_MLX_LOAD_ERROR", "Model type gemma9 not supported."}}};
        const json error = driver.next();
        CHECK(error["kind"] == "load");
        CHECK(error["message"].get<std::string>().find("Model type gemma9 not supported.") !=
              std::string::npos);
        CHECK(driver.close() == 4);
    }
    {
        const std::filesystem::path empty = dir.path() / "not-a-model";
        std::filesystem::create_directories(empty);
        Driver driver{dir, {}, {"x"}, empty};
        const json error = driver.next();
        CHECK(error["kind"] == "load");
        CHECK(error["message"].get<std::string>().find("has no config.json") != std::string::npos);
        CHECK(driver.close() == 4);
    }
}

TEST_CASE("nothing a library prints reaches the protocol channel",
          "[backends][mlx][driver][stderr]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    // The stub prints through print(), straight onto descriptor 1 and onto
    // stderr; next() fails on any stdout line that is not a JSON object.
    Driver driver{dir, {{"STUB_MLX_NOISE", "NOISY-LIBRARY-LINE"}}, {"clean"}};
    REQUIRE(driver.next()["type"] == "ready");
    driver.send(generate(1, json::array({user("q")})));
    CHECK(joined(driver.until_done(1), "text") == "clean");
    CHECK(driver.close() == 0);
    const std::string errors = driver.stderr_text();
    std::size_t seen = 0;
    for (std::size_t at = errors.find("NOISY-LIBRARY-LINE"); at != std::string::npos;
         at = errors.find("NOISY-LIBRARY-LINE", at + 1)) {
        ++seen;
    }
    CHECK(seen == 3);
}

TEST_CASE(
    "the driver's guards precede its first ML import: offline, SIGINT ignored, descriptor 1 "
    "redirected",
    "[backends][mlx][driver][guards]") {
    const std::string driver = read(kDriver);
    const std::size_t first_import = driver.find("import mlx.core as mx");
    REQUIRE(first_import != std::string::npos);
    for (const std::string_view guard :
         {R"(os.environ["HF_HUB_OFFLINE"] = "1")", R"(os.environ["TRANSFORMERS_OFFLINE"] = "1")",
          "signal.signal(signal.SIGINT, signal.SIG_IGN)", "os.dup2(2, 1)"}) {
        const std::size_t at = driver.find(guard);
        INFO(guard);
        REQUIRE(at != std::string::npos);
        CHECK(at < first_import);
    }
    // No server mode, ever: the driver never names a socket.
    CHECK(driver.find("socket") == std::string::npos);
}

// ---------------------------------------------------------------------------
// Images (27c)
// ---------------------------------------------------------------------------

namespace {

/// The stub packages with mlx-vlm laid beside mlx-lm.
constexpr std::string_view kWithVlm = "stub_mlx:stub_mlx_vlm";

/// A picture as the backend sends one: base64 bytes in a data: URI.
json picture(std::string_view media, std::string_view base64) {
    return {{"type", "image"},
            {"image", "data:" + std::string{media} + ";base64," + std::string{base64}}};
}

json seeing(json parts) {
    return {{"role", "user"}, {"content", std::move(parts)}};
}

}  // namespace

TEST_CASE(
    "under --vision the driver loads through mlx-vlm, says so, and every picture reaches the "
    "model as its bytes, in place",
    "[backends][mlx][driver][vision]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    Driver driver{dir,
                  {},
                  {"I see {images}.", "Plain words.", "A long answer"},
                  kModel,
                  std::string{kWithVlm},
                  {"--vision"}};
    const json ready = driver.next();
    CHECK(ready["type"] == "ready");
    CHECK(ready["vision"] == true);
    CHECK(ready["mlx_vlm"] == "0.0-vlm-stub");
    CHECK(ready["mlx_lm"] == "0.0-stub");

    // "ABCDEFGH" and "XYZ": 8 and 3 bytes, a .png and a .jpg.
    driver.send(generate(
        1, json::array({seeing(json::array({picture("image/png", "QUJDREVGR0g="),
                                            {{"type", "text"}, {"text", "What are these?"}},
                                            picture("image/jpeg", "WFla")}))})));
    const std::vector<json> events = driver.until_done(1);
    CHECK(joined(events, "text") == "I see 2 images: 8 bytes .png, 3 bytes .jpg.");
    const json& done = events.back();
    CHECK(done["finish"] == "stop");
    CHECK(done["cached_tokens"] == 0);
    CHECK(done["completion_tokens"] == 43);
    CHECK(done["prompt_tokens"].get<std::int64_t>() > 0);

    // A text turn on the same model: through mlx-vlm too, no picture.
    driver.send(generate(2, json::array({user("And now?")})));
    CHECK(joined(driver.until_done(2), "text") == "Plain words.");
    // The cap ends a reply as the cap, not as the model's own stop.
    driver.send(generate(3, json::array({user("Go on")}), json::array(), {{"max_tokens", 4}}));
    const json capped = driver.until_done(3).back();
    CHECK(capped["finish"] == "length");
    CHECK(capped["completion_tokens"] == 4);
    CHECK(driver.close() == 0);

    const std::vector<std::string> record = driver.record();
    CHECK(count(record, "vlm load model") == 1);
    // The format's markers and parser are mlx_lm's, over the same directory.
    CHECK(count(record, "tokenizer model") == 1);
    CHECK(count(record, "load ") == 0);
    CHECK(count(record, "vlm generate images=2 ") == 1);
    CHECK(count(record, "vlm generate images=0 ") == 2);
    // The pictures were files in the driver's own temporary directory, there
    // for their turn alone: none outlives it.
    std::vector<std::filesystem::path> pictures;
    for (const std::string& line : record) {
        if (line.starts_with("vlm image ")) {
            pictures.emplace_back(line.substr(10));
        }
    }
    REQUIRE(pictures.size() == 2);
    CHECK(pictures[0].parent_path() == pictures[1].parent_path());
    CHECK(pictures[0].filename().string().ends_with(".png"));
    CHECK(pictures[1].filename().string().ends_with(".jpg"));
    CHECK_FALSE(std::filesystem::exists(pictures[0].parent_path()));
    // This mlx-vlm's step takes a sampler: mlx_lm's, with the request's values.
    CHECK(count(record, "vlm generate images=2 options=logits_processors,max_tokens,sampler") == 1);
}

TEST_CASE(
    "a vision reply is read as a text one is: calls by mlx_lm's parser, a stop string, a cancel",
    "[backends][mlx][driver][vision]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    Driver driver{
        dir,
        {{"STUB_MLX_TOOLS", "json_tools"}, {"STUB_MLX_DELAY_MS", "20"}},
        {R"(Looking.<tool_call>{"name": "read_file", "arguments": {"path": "a.txt"}}</tool_call>)",
         "abcENDxyz", std::string(400, 'x'), "after"},
        kModel,
        std::string{kWithVlm},
        {"--vision"}};
    CHECK(driver.next()["tool_parser"] == "json_tools");

    driver.send(generate(1, json::array({seeing(json::array({picture("image/png", "QUJD")}))}),
                         read_file_tool()));
    const std::vector<json> called = driver.until_done(1);
    CHECK(joined(called, "text") == "Looking.");
    const std::vector<json> calls = of_type(called, "tool_call");
    REQUIRE(calls.size() == 1);
    CHECK(calls[0]["arguments"] == json{{"path", "a.txt"}});
    CHECK(called.back()["finish"] == "tool_calls");

    driver.send(
        generate(2, json::array({user("q")}), json::array(), {{"stop", json::array({"END"})}}));
    const std::vector<json> stopped = driver.until_done(2);
    CHECK(joined(stopped, "text") == "abc");
    CHECK(stopped.back()["finish"] == "stop");

    driver.send(generate(3, json::array({user("go on")})));
    REQUIRE(driver.next()["type"] == "text");
    driver.send({{"type", "cancel"}, {"id", 3}});
    const json cancelled = driver.until_done(3).back();
    CHECK(cancelled["finish"] == "cancelled");
    CHECK(cancelled["completion_tokens"] < 400);

    driver.send(generate(4, json::array({user("again")})));
    CHECK(joined(driver.until_done(4), "text") == "after");
    CHECK(driver.close() == 0);
    CHECK(count(driver.record(), "vlm load ") == 1);
}

TEST_CASE(
    "a picture the driver cannot take is refused and the model stays loaded: a remote one is "
    "never fetched",
    "[backends][mlx][driver][vision]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    Driver driver{dir, {}, {"fine"}, kModel, std::string{kWithVlm}, {"--vision"}};
    REQUIRE(driver.next()["vision"] == true);
    driver.send(generate(
        1, json::array({seeing(
               json::array({{{"type", "image"}, {"image", "https://example.com/cat.png"}}}))})));
    json error = driver.until_done(1).back();
    CHECK(error["kind"] == "request");
    CHECK(error["message"].get<std::string>().find("never fetched") != std::string::npos);

    driver.send(generate(2, json::array({seeing(json::array({picture("image/png", "")}))})));
    error = driver.until_done(2).back();
    CHECK(error["kind"] == "request");
    CHECK(error["message"].get<std::string>().find("empty") != std::string::npos);

    driver.send(generate(3, json::array({user("q")})));
    CHECK(joined(driver.until_done(3), "text") == "fine");
    CHECK(driver.close() == 0);
    // No picture outlives its turn: the files were the driver's to remove.
    CHECK(count(driver.record(), "vlm generate") == 1);
}

TEST_CASE(
    "an older mlx-vlm takes its own knobs; without mlx-vlm, or with a load it refuses, the driver "
    "says so and exits",
    "[backends][mlx][driver][vision][errors]") {
    REQUIRE_PYTHON();
    const apogee::testing::TempDir dir{"mlx-driver"};
    {
        Driver driver{dir,    {{"STUB_MLX_VLM_LEGACY", "1"}}, {"ok"},
                      kModel, std::string{kWithVlm},          {"--vision"}};
        REQUIRE(driver.next()["vision"] == true);
        driver.send(generate(
            1, json::array({user("q")}), json::array(),
            {{"sampling", {{"temperature", 0.7}, {"top_p", 0.8}, {"repetition_penalty", 1.1}}}}));
        (void)driver.until_done(1);
        CHECK(driver.close() == 0);
        CHECK(count(driver.record(),
                    "vlm generate images=0 options=max_tokens,repetition_penalty,temperature,"
                    "top_p") == 1);
    }
    {
        // mlx-lm alone: --vision is refused, naming the set that adds it.
        Driver driver{dir, {}, {"x"}, kModel, "stub_mlx", {"--vision"}};
        const json error = driver.next();
        CHECK(error["kind"] == "missing_dependency");
        CHECK(error["message"].get<std::string>().find("apogee train setup --with mlx-vlm") !=
              std::string::npos);
        CHECK(driver.close() == 3);
    }
    {
        Driver driver{dir,
                      {{"STUB_MLX_VLM_LOAD_ERROR", "Model type qwen9_vl not supported."}},
                      {"x"},
                      kModel,
                      std::string{kWithVlm},
                      {"--vision"}};
        const json error = driver.next();
        CHECK(error["kind"] == "load");
        CHECK(error["message"].get<std::string>().find("through mlx-vlm") != std::string::npos);
        CHECK(error["message"].get<std::string>().find("qwen9_vl") != std::string::npos);
        CHECK(driver.close() == 4);
    }
    {
        // Without --vision the same runtime loads through mlx-lm, as it always did.
        Driver driver{dir, {}, {"x"}, kModel, std::string{kWithVlm}};
        const json ready = driver.next();
        CHECK(ready["vision"] == false);
        CHECK(ready["mlx_vlm"].is_null());
        CHECK(driver.close() == 0);
    }
}
