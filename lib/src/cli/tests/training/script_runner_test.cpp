#include "training/script_runner.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <string>
#include <vector>

#include "harness/cancellation.h"
#include "support/fake_child.h"

/// The C++ side of the Python boundary: every line shape classified, the
/// stream framed across chunk boundaries, a non-JSON line kept as a message,
/// `{"error"}` its own event, the exit code and the stderr tail carried, a
/// spawn failure named, cancellation terminating the child.
namespace {

using apogee::testing::FakeChild;
using apogee::training::classify_script_line;
using apogee::training::run_command;
using apogee::training::run_script;
using apogee::training::ScriptEvent;
using apogee::training::ScriptOutcome;
using apogee::training::ScriptRequest;
using apogee::training::Spawner;
using apogee::training::tail_of;

/// A child whose termination the test can see after the runner has
/// destroyed it.
class ObservedChild final : public apogee::platform::ChildProcess {
public:
    ObservedChild(std::unique_ptr<FakeChild> inner, bool& terminated)
        : inner_{std::move(inner)}, terminated_{terminated} {}

    [[nodiscard]] bool write_stdin(std::string_view bytes) override {
        return inner_->write_stdin(bytes);
    }

    void close_stdin() override {
        inner_->close_stdin();
    }

    [[nodiscard]] apogee::platform::ReadStatus read_stdout(std::string& out,
                                                           std::chrono::milliseconds t) override {
        return inner_->read_stdout(out, t);
    }

    [[nodiscard]] apogee::platform::ReadStatus read_stderr(std::string& out,
                                                           std::chrono::milliseconds t) override {
        return inner_->read_stderr(out, t);
    }

    [[nodiscard]] bool exited() override {
        return inner_->exited();
    }

    [[nodiscard]] std::optional<int> wait_for_exit(std::chrono::milliseconds t) override {
        return inner_->wait_for_exit(t);
    }

    void terminate() override {
        terminated_ = true;
        inner_->terminate();
    }

private:
    std::unique_ptr<FakeChild> inner_;
    bool& terminated_;
};

Spawner spawning(std::string stdout_script, int exit_status = 0, std::string stderr_script = {},
                 std::size_t chunk_size = 0, bool* terminated = nullptr) {
    return [=](const apogee::platform::ChildCommand&,
               std::string&) -> std::unique_ptr<apogee::platform::ChildProcess> {
        auto child = std::make_unique<FakeChild>();
        child->stdout_script = stdout_script;
        child->stderr_script = stderr_script;
        child->exit_status = exit_status;
        child->chunk_size = chunk_size;
        if (terminated != nullptr) {
            return std::make_unique<ObservedChild>(std::move(child), *terminated);
        }
        return child;
    };
}

ScriptRequest request() {
    ScriptRequest out;
    out.interpreter = "/venv/bin/python";
    out.script = "/scripts/prepare_dataset.py";
    out.arguments = {"--source", "x"};
    return out;
}

}  // namespace

TEST_CASE("every line shape is classified", "[training][scripts][protocol]") {
    ScriptEvent message = classify_script_line(R"({"message": "Loading..."})");
    CHECK(message.kind == ScriptEvent::Kind::Message);
    CHECK(message.text == "Loading...");

    ScriptEvent error = classify_script_line(R"({"error": "boom"})");
    CHECK(error.kind == ScriptEvent::Kind::Error);
    CHECK(error.text == "boom");

    ScriptEvent record =
        classify_script_line(R"({"rows_written": 3, "rows_skipped": 1, "out": "p"})");
    CHECK(record.kind == ScriptEvent::Kind::Record);
    CHECK(record.record["rows_written"] == 3);

    // A non-JSON line is a message, never dropped: a stack trace reaches
    // the caller as text.
    ScriptEvent prose = classify_script_line("Traceback (most recent call last):");
    CHECK(prose.kind == ScriptEvent::Kind::Message);
    CHECK(prose.text == "Traceback (most recent call last):");

    // Opened like an object and did not parse: still a message.
    ScriptEvent broken = classify_script_line("{not json");
    CHECK(broken.kind == ScriptEvent::Kind::Message);
    CHECK(broken.text == "{not json");
}

TEST_CASE("a script's events arrive in order and the run is ok on a clean exit",
          "[training][scripts]") {
    std::vector<ScriptEvent> events;
    const ScriptOutcome outcome = run_script(
        request(), [&events](const ScriptEvent& e) { events.push_back(e); }, {},
        spawning("{\"message\": \"one\"}\nplain text\n{\"rows_written\": 2, \"rows_skipped\": 0, "
                 "\"out\": \"x\"}\n"));
    CHECK(outcome.ok);
    REQUIRE(outcome.exit_code.has_value());
    CHECK(*outcome.exit_code == 0);
    REQUIRE(events.size() == 3);
    CHECK(events[0].text == "one");
    CHECK(events[1].kind == ScriptEvent::Kind::Message);
    CHECK(events[1].text == "plain text");
    CHECK(events[2].kind == ScriptEvent::Kind::Record);
}

TEST_CASE("the stream is framed across chunk boundaries", "[training][scripts][framing]") {
    const std::string script =
        "{\"message\": \"alpha\"}\n{\"message\": \"beta\"}\n{\"rows_written\": 1, "
        "\"rows_skipped\": 0, \"out\": \"x\"}";
    std::vector<std::string> whole;
    std::vector<std::string> chunked;
    (void)run_script(
        request(), [&whole](const ScriptEvent& e) { whole.push_back(e.text); }, {},
        spawning(script));
    (void)run_script(
        request(), [&chunked](const ScriptEvent& e) { chunked.push_back(e.text); }, {},
        spawning(script, 0, {}, 3));
    REQUIRE(whole.size() == 3);
    CHECK(whole == chunked);
}

TEST_CASE("an error line is the outcome's error and the run is not ok", "[training][scripts]") {
    const ScriptOutcome outcome = run_script(
        request(), {}, {},
        spawning(
            "{\"message\": \"starting\"}\n{\"error\": \"Missing Python packages: datasets\"}\n",
            1));
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.error == "Missing Python packages: datasets");
    REQUIRE(outcome.exit_code.has_value());
    CHECK(*outcome.exit_code == 1);
}

TEST_CASE("a bad exit with no error line is named with its code and the stderr tail",
          "[training][scripts]") {
    // Ommi discarded the exit status: a crashed trainer read as success. Here
    // the crash is the outcome, with what the script said on stderr.
    const ScriptOutcome outcome =
        run_script(request(), {}, {}, spawning("", 2, "Traceback\nValueError: bad\n"));
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.error == "prepare_dataset.py exited with code 2");
    CHECK(outcome.stderr_tail == "Traceback\nValueError: bad");
    CHECK(outcome.describe().find("stderr: Traceback") != std::string::npos);
}

TEST_CASE("a spawn failure names the script", "[training][scripts]") {
    const ScriptOutcome outcome =
        run_script(request(), {}, {},
                   [](const apogee::platform::ChildCommand&,
                      std::string& error) -> std::unique_ptr<apogee::platform::ChildProcess> {
                       error = "no such interpreter";
                       return nullptr;
                   });
    CHECK_FALSE(outcome.ok);
    CHECK(outcome.error == "could not start prepare_dataset.py: no such interpreter");
    CHECK_FALSE(outcome.exit_code.has_value());
}

TEST_CASE("cancellation terminates the child and reports cancelled", "[training][scripts]") {
    apogee::harness::CancellationToken token = apogee::harness::CancellationToken::create();
    token.cancel();
    bool terminated = false;
    const ScriptOutcome outcome = run_script(
        request(), {}, token, spawning("{\"message\": \"never read\"}\n", 0, {}, 0, &terminated));
    CHECK(outcome.cancelled);
    CHECK_FALSE(outcome.ok);
    CHECK(terminated);
}

TEST_CASE("the interpreter, the script and the arguments form the command", "[training][scripts]") {
    apogee::platform::ChildCommand seen;
    (void)run_script(request(), {}, {},
                     [&seen](const apogee::platform::ChildCommand& command,
                             std::string&) -> std::unique_ptr<apogee::platform::ChildProcess> {
                         seen = command;
                         return std::make_unique<FakeChild>();
                     });
    CHECK(seen.program == "/venv/bin/python");
    REQUIRE(seen.arguments.size() == 3);
    CHECK(seen.arguments[0] == "/scripts/prepare_dataset.py");
    CHECK(seen.arguments[1] == "--source");
}

TEST_CASE("run_command captures both streams and the exit code", "[training][scripts]") {
    apogee::platform::ChildCommand command;
    command.program = "pip";
    const apogee::training::CommandResult result =
        run_command(command, {}, spawning("Successfully installed\n", 0, "WARNING: x\n"));
    CHECK(result.ok());
    CHECK(result.out == "Successfully installed\n");
    CHECK(result.err == "WARNING: x\n");
    const apogee::training::CommandResult failed =
        run_command(command, {}, spawning("", 1, "ERROR: no matching distribution\n"));
    CHECK_FALSE(failed.ok());
    REQUIRE(failed.exit_code.has_value());
    CHECK(*failed.exit_code == 1);
}

TEST_CASE("tail_of keeps the last lines", "[training][scripts]") {
    CHECK(tail_of("a\nb\nc\nd\n", 2) == "c\nd");
    CHECK(tail_of("only", 8) == "only");
    CHECK(tail_of("", 8).empty());
    CHECK(tail_of("a\nb\n", 0).empty());
}
