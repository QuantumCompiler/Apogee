#include "platform/child_process.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <string>

/// The child-process seam, against real processes.
///
/// These use `/bin/sh` and `/bin/cat` rather than a fake, deliberately: the
/// whole value of this layer is that it drives a real OS mechanism correctly,
/// and a fake would only prove the interface compiles. They stay hermetic —
/// no network, no writes outside a pipe — and every one of them finishes in
/// milliseconds.
namespace {

using apogee::platform::ChildCommand;
using apogee::platform::ReadStatus;

/// Reads until EOF or the budget runs out.
[[nodiscard]] std::string drain(apogee::platform::ChildProcess& child,
                                std::chrono::milliseconds budget) {
    const auto deadline = std::chrono::steady_clock::now() + budget;
    std::string all;
    std::string chunk;
    while (std::chrono::steady_clock::now() < deadline) {
        const ReadStatus status = child.read_stdout(chunk, std::chrono::milliseconds{50});
        if (status == ReadStatus::Data) {
            all += chunk;
            continue;
        }
        if (status == ReadStatus::Eof || status == ReadStatus::Error) {
            break;
        }
    }
    return all;
}

}  // namespace

TEST_CASE("a child echoes what it is given on stdin", "[platform][child]") {
    if (!apogee::platform::supports_child_processes()) {
        SUCCEED("no child-process support on this platform");
        return;
    }

    ChildCommand command;
    command.program = "cat";

    std::string error;
    auto child = apogee::platform::start_child(command, error);
    REQUIRE(child != nullptr);
    INFO(error);

    REQUIRE(child->write_stdin("hello\nworld\n"));
    // Closing stdin is how a stream-fed child is told to finish -- the same
    // mechanism the CLI backend uses so the child can emit its terminal event.
    child->close_stdin();

    CHECK(drain(*child, std::chrono::seconds{3}) == "hello\nworld\n");
}

TEST_CASE("stdout and stderr stay separate", "[platform][child]") {
    // The rule that keeps diagnostics out of the JSONL stream. Merged, a
    // warning printed mid-turn lands inside the event stream and breaks the
    // parser at the worst possible moment.
    if (!apogee::platform::supports_child_processes()) {
        SUCCEED("no child-process support on this platform");
        return;
    }

    ChildCommand command;
    command.program = "sh";
    command.arguments = {"-c", "echo to-stdout; echo to-stderr 1>&2"};

    std::string error;
    auto child = apogee::platform::start_child(command, error);
    REQUIRE(child != nullptr);
    child->close_stdin();

    const std::string out = drain(*child, std::chrono::seconds{3});

    std::string err;
    std::string chunk;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{3};
    while (std::chrono::steady_clock::now() < deadline) {
        const ReadStatus status = child->read_stderr(chunk, std::chrono::milliseconds{50});
        if (status == ReadStatus::Data) {
            err += chunk;
            continue;
        }
        if (status == ReadStatus::Eof || status == ReadStatus::Error) {
            break;
        }
    }

    CHECK(out.find("to-stdout") != std::string::npos);
    CHECK(out.find("to-stderr") == std::string::npos);
    CHECK(err.find("to-stderr") != std::string::npos);
}

TEST_CASE("a read on a quiet child times out rather than blocking", "[platform][child]") {
    // What lets a reader loop notice a cancellation flag. Without a timeout,
    // cancelling a turn would mean killing the child -- losing the terminal
    // event where the session id and cost live.
    if (!apogee::platform::supports_child_processes()) {
        SUCCEED("no child-process support on this platform");
        return;
    }

    ChildCommand command;
    command.program = "sh";
    command.arguments = {"-c", "sleep 5"};

    std::string error;
    auto child = apogee::platform::start_child(command, error);
    REQUIRE(child != nullptr);

    std::string out;
    const auto started = std::chrono::steady_clock::now();
    const ReadStatus status = child->read_stdout(out, std::chrono::milliseconds{100});
    const auto elapsed = std::chrono::steady_clock::now() - started;

    CHECK(status == ReadStatus::Timeout);
    CHECK(elapsed < std::chrono::seconds{2});
    child->terminate();
}

TEST_CASE("exit status is reported", "[platform][child]") {
    if (!apogee::platform::supports_child_processes()) {
        SUCCEED("no child-process support on this platform");
        return;
    }

    ChildCommand command;
    command.program = "sh";
    command.arguments = {"-c", "exit 3"};

    std::string error;
    auto child = apogee::platform::start_child(command, error);
    REQUIRE(child != nullptr);
    child->close_stdin();

    const std::optional<int> status = child->wait_for_exit(std::chrono::seconds{5});
    REQUIRE(status.has_value());
    CHECK(*status == 3);
}

TEST_CASE("writing to a dead child fails rather than throwing", "[platform][child]") {
    // A child dying mid-conversation is a state this backend is REQUIRED to
    // recover from, so it has to be an ordinary false, not an exception.
    if (!apogee::platform::supports_child_processes()) {
        SUCCEED("no child-process support on this platform");
        return;
    }

    ChildCommand command;
    command.program = "sh";
    command.arguments = {"-c", "exit 0"};

    std::string error;
    auto child = apogee::platform::start_child(command, error);
    REQUIRE(child != nullptr);
    (void)child->wait_for_exit(std::chrono::seconds{5});

    // The first write may land in the pipe buffer; a subsequent one cannot.
    bool ever_failed = false;
    for (int attempt = 0; attempt < 200 && !ever_failed; ++attempt) {
        if (!child->write_stdin(std::string(4096, 'x'))) {
            ever_failed = true;
        }
    }
    CHECK(ever_failed);
}

TEST_CASE("a missing program is refused by name", "[platform][child]") {
    // Naming what was not found beats a numeric errno the user cannot act on.
    std::string error;
    ChildCommand command;
    command.program = "apogee-definitely-not-a-real-program";

    CHECK(apogee::platform::start_child(command, error) == nullptr);
    CHECK(error.find("apogee-definitely-not-a-real-program") != std::string::npos);
}

TEST_CASE("PATH resolution finds real programs and rejects invented ones", "[platform][child]") {
    using apogee::platform::find_on_path;

    CHECK_FALSE(find_on_path("sh").empty());
    CHECK(find_on_path("apogee-definitely-not-a-real-program").empty());
    CHECK(find_on_path("").empty());
    // An explicit path is honoured as given, and checked.
    CHECK(find_on_path("/bin/sh") == "/bin/sh");
    CHECK(find_on_path("/nonexistent/binary").empty());
}

TEST_CASE("a large stream survives the pipe in whatever chunks it arrives", "[platform][child]") {
    // The reason this layer exists: a pipe hands over bytes in sizes nobody
    // chose. Far more than one buffer's worth, so several reads are guaranteed.
    if (!apogee::platform::supports_child_processes()) {
        SUCCEED("no child-process support on this platform");
        return;
    }

    ChildCommand command;
    command.program = "sh";
    command.arguments = {"-c", "for i in $(seq 1 2000); do echo line-$i; done"};

    std::string error;
    auto child = apogee::platform::start_child(command, error);
    REQUIRE(child != nullptr);
    child->close_stdin();

    const std::string all = drain(*child, std::chrono::seconds{10});
    CHECK(all.find("line-1\n") != std::string::npos);
    CHECK(all.find("line-2000\n") != std::string::npos);
}
