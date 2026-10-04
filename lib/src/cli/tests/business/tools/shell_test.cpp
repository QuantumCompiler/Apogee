#include "tools/shell.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <random>
#include <string>
#include <string_view>

#include "agent/tool.h"
#include "platform/child_process.h"
#include "support/env_guard.h"
#include "tools/process.h"

/// The shell toolset: the trailers, the timeout as a result, and the gate.
namespace {

using apogee::agent::ToolOutcome;
using apogee::agent::ToolRegistry;

}  // namespace

TEST_CASE("run_command renders stdout, stderr and the exit status, and is gated",
          "[tools][shell][permission]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const apogee::testing::TempDir temp{"tools-shell-" + std::to_string(std::random_device{}())};
    ToolRegistry registry;
    apogee::tools::register_shell_tool(registry, temp.path(), std::chrono::seconds{10});
    const apogee::agent::Tool* tool = registry.find("run_command");
    REQUIRE(tool != nullptr);
    CHECK(tool->writes);  // the divergence from Ommi: the shell goes through the gate
    CHECK(tool->describe_target(R"({"command":"echo hi"})") == "echo hi");

    const ToolOutcome ok = tool->run(R"({"command":"echo out; echo err 1>&2; exit 3"})");
    CHECK_FALSE(ok.is_error);
    CHECK(ok.content == "out\n[stderr]\nerr\n[exit 3]");

    // The working directory: the default, and an override.
    CHECK(tool->run(R"({"command":"pwd"})").content.find(temp.path().filename().string()) !=
          std::string::npos);
    std::filesystem::create_directories(temp.path() / "elsewhere");
    CHECK(tool->run(R"({"command":"pwd","cwd":")" + (temp.path() / "elsewhere").string() + R"("})")
              .content.find("elsewhere") != std::string::npos);
    CHECK(tool->run(R"({"command":"pwd","cwd":"/nonexistent/dir"})").is_error);
    CHECK(tool->run(R"({"command":""})").is_error);
    CHECK(tool->run("{}").is_error);

    // A quote in the command cannot escape its argument: it is data.
    CHECK(tool->run(R"({"command":"echo \"$1\" done"})").content.starts_with(" done"));
}

TEST_CASE("a command that outlives the timeout is a result, not a hang", "[tools][shell]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const apogee::testing::TempDir temp{"tools-shell-t-" + std::to_string(std::random_device{}())};
    const auto started = std::chrono::steady_clock::now();
    const apogee::tools::ShellResult result =
        apogee::tools::run_shell("sleep 30", temp.path(), std::chrono::milliseconds{300});
    CHECK(result.timed_out);
    CHECK(result.rendered == "[timed out after 0s]");
    CHECK(std::chrono::steady_clock::now() - started < std::chrono::seconds{10});
}

namespace {

using apogee::tools::CapturedOutput;
using apogee::tools::OutputLimit;
using apogee::tools::ProcessOutcome;

/// A stream holding `text`, captured the way the shell runs one.
CapturedOutput captured(const std::string& text, std::size_t keep, std::size_t piece = 1000) {
    CapturedOutput output{OutputLimit{keep, keep}};
    for (std::size_t at = 0; at < text.size(); at += piece) {
        output.append(std::string_view{text}.substr(at, piece));
    }
    return output;
}

/// `count` numbered lines, each `width` bytes with its newline.
std::string lines(int count, std::size_t width, std::string_view tag = "line") {
    std::string out;
    for (int i = 1; i <= count; ++i) {
        std::string row = std::string{tag} + " " + std::to_string(i) + " ";
        row.resize(width - 1, 'x');
        out += row + "\n";
    }
    return out;
}

/// The seam line a cut output carries, and the number it names.
std::uint64_t omitted_count(const std::string& rendered) {
    const std::size_t at = rendered.find("\n[... ");
    REQUIRE(at != std::string::npos);
    return std::stoull(rendered.substr(at + 6));
}

bool valid_utf8(std::string_view text) {
    for (std::size_t i = 0; i < text.size();) {
        const auto lead = static_cast<unsigned char>(text[i]);
        const std::size_t length = lead < 0x80             ? 1
                                   : (lead & 0xE0) == 0xC0 ? 2
                                   : (lead & 0xF0) == 0xE0 ? 3
                                   : (lead & 0xF8) == 0xF0 ? 4
                                                           : 0;
        if (length == 0 || i + length > text.size()) {
            return false;
        }
        for (std::size_t k = 1; k < length; ++k) {
            if ((static_cast<unsigned char>(text[i + k]) & 0xC0) != 0x80) {
                return false;
            }
        }
        i += length;
    }
    return true;
}

}  // namespace

TEST_CASE("a captured stream keeps its head and tail and counts what fell between",
          "[tools][shell][cap]") {
    // Fits: the whole stream, whatever the pieces.
    CHECK(captured("hello world", 8, 3).text() == "hello world");
    CHECK(captured("hello world", 8, 3).omitted() == 0);

    // Past both ends: the first and last bytes exactly, the middle counted.
    const std::string digits = "0123456789abcdefghijklmnopqrstuvwxyz";
    const CapturedOutput cut = captured(digits, 5, 7);
    CHECK(cut.head() == "01234");
    CHECK(cut.tail() == "vwxyz");
    CHECK(cut.omitted() == digits.size() - 10);

    // The default keeps only the tail -- git's and every other caller's.
    CapturedOutput tail_only{OutputLimit{0, 4}};
    tail_only.append("abcdef");
    tail_only.append("gh");
    CHECK(tail_only.head().empty());
    CHECK(tail_only.text() == "efgh");
    CHECK(tail_only.omitted() == 4);
}

TEST_CASE("command output past the cap keeps its first and last lines and names the rest",
          "[tools][shell][cap]") {
    constexpr std::size_t keep = 1024;

    SECTION("short output is rendered whole, as it always was") {
        ProcessOutcome outcome;
        outcome.out = captured("out\n", keep);
        outcome.err = captured("err\n", keep);
        outcome.exit_code = 3;
        CHECK(apogee::tools::render_command_output(outcome, keep) ==
              "out\n[stderr]\nerr\n[exit 3]");
    }

    SECTION("a long stdout: whole lines from each end, and the exact count between") {
        const std::string text = lines(2000, 50);
        ProcessOutcome outcome;
        outcome.out = captured(text, keep);
        outcome.exit_code = 0;
        const std::string rendered = apogee::tools::render_command_output(outcome, keep);
        const std::string whole = text.substr(0, text.size() - 1) + "\n[exit 0]";

        CHECK(rendered.size() < 2 * keep + 400);
        CHECK(rendered.starts_with("line 1 xxx"));
        CHECK(rendered.ends_with("line 2000 " + std::string(39, 'x') + "\n[exit 0]"));
        const std::size_t seam = rendered.find("\n[... ");
        const std::size_t after = rendered.find("]\n", seam) + 2;
        const std::string head = rendered.substr(0, seam);
        const std::string tail = rendered.substr(after);
        // Each end is the real output's, cut at a line boundary...
        CHECK(whole.starts_with(head + "\n"));
        CHECK(whole.ends_with("\n" + tail));
        CHECK(head.size() <= keep);
        CHECK(tail.size() <= keep);
        // ...and the seam says exactly how much it stands for.
        CHECK(omitted_count(rendered) == whole.size() - head.size() - tail.size());
        CHECK(rendered.find("read_file's offset and limit") != std::string::npos);
    }

    SECTION("both streams long: the head is stdout's, the tail stderr's and the exit") {
        ProcessOutcome outcome;
        outcome.out = captured(lines(500, 40, "out"), keep);
        outcome.err = captured(lines(500, 40, "err"), keep);
        outcome.exit_code = 1;
        const std::string rendered = apogee::tools::render_command_output(outcome, keep);
        CHECK(rendered.starts_with("out 1 "));
        CHECK(rendered.ends_with("err 500 " + std::string(31, 'x') + "\n[exit 1]"));
        const std::uint64_t whole = 2 * (lines(500, 40).size() - 1) +
                                    std::string{"\n[stderr]\n"}.size() +
                                    std::string{"\n[exit 1]"}.size();
        const std::size_t seam = rendered.find("\n[... ");
        const std::size_t after = rendered.find("]\n", seam) + 2;
        CHECK(omitted_count(rendered) == whole - seam - (rendered.size() - after));
    }

    SECTION("two streams each under the cap, together over it, are cut too") {
        ProcessOutcome outcome;
        outcome.out = captured(lines(30, 40, "out"), keep);
        outcome.err = captured(lines(30, 40, "err"), keep);
        outcome.exit_code = 0;
        const std::string rendered = apogee::tools::render_command_output(outcome, keep);
        CHECK(rendered.find("bytes omitted") != std::string::npos);
        CHECK(rendered.starts_with("out 1 "));
        CHECK(rendered.ends_with("[exit 0]"));
    }

    SECTION("one enormous line is cut between characters, never through one") {
        // Two- and three-byte characters, moved by up to four ASCII bytes at
        // the front and the rest at the back, so the head's cut and the
        // tail's both meet every alignment: with a gap (the stream kept to
        // its ends) and without one (two streams, each whole, too long
        // together).
        std::string characters;
        while (characters.size() < 10 * keep) {
            characters += "é€";
        }
        for (std::size_t shift = 0; shift < 5; ++shift) {
            const std::string text =
                std::string(shift, 'a') + characters + std::string(4 - shift, 'b');
            ProcessOutcome gapped;
            gapped.out = captured(text, keep);
            gapped.exit_code = 0;
            // Each stream whole characters, each under the cap, together over it.
            std::string out_text(shift, 'a');
            std::string err_text;
            for (int i = 0; i < 300; ++i) {
                out_text += "é€";
                err_text += "€é";
            }
            err_text += std::string(4 - shift, 'b');
            ProcessOutcome whole;
            whole.out = captured(out_text, keep);
            whole.err = captured(err_text, keep);
            whole.exit_code = 0;
            for (const ProcessOutcome* outcome : {&gapped, &whole}) {
                const std::string rendered = apogee::tools::render_command_output(*outcome, keep);
                INFO("shift " << shift << (outcome == &gapped ? ", gapped" : ", whole"));
                CHECK(valid_utf8(rendered));
                CHECK(rendered.find("bytes omitted") != std::string::npos);
            }
        }
    }
}

TEST_CASE("a command printing megabytes returns its ends in under 17 KiB", "[tools][shell][cap]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const apogee::testing::TempDir temp{"tools-shell-c-" + std::to_string(std::random_device{}())};
    ToolRegistry registry;
    apogee::tools::register_shell_tool(registry, temp.path(), std::chrono::seconds{60});
    const apogee::agent::Tool* tool = registry.find("run_command");
    REQUIRE(tool != nullptr);
    CHECK(tool->description.find("first and last 8 KB") != std::string::npos);

    // About 5.7 MB: 100,000 lines of 57 bytes.
    const ToolOutcome outcome = tool->run(
        R"({"command":"awk 'BEGIN { for (i = 1; i <= 100000; i++) printf \"line %06d xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx\\n\", i }'"})");
    CHECK_FALSE(outcome.is_error);
    CHECK(outcome.content.size() < 17 * 1024);
    CHECK(outcome.content.starts_with("line 000001 "));
    CHECK(outcome.content.ends_with("line 100000 " + std::string(44, 'x') + "\n[exit 0]"));
    CHECK(omitted_count(outcome.content) > 5'000'000);
}
