#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <regex>
#include <sstream>
#include <string>
#include <vector>

#include "platform/child_process.h"
#include "support/cli_home.h"

/// Chat's session core, pinned from outside (27s).
///
/// `apogee execute` shares chat's session core -- one machinery, parameterized
/// by mode -- and the guardrail is that the generalization is invisible to
/// chat. The battery below drives `apogee chat` in process through every
/// path the core owns -- a piped REPL with every slash command, a suite named,
/// switched, turned off and resumed, machine mode, every launch refusal, the
/// flags' help and their completion -- and holds the whole of what it prints,
/// stdout and stderr and the exit code, to a golden written by chat as it
/// stood BEFORE the core was cut out of it. Byte for byte: a chat id and the
/// temporary home are the only things normalized.
namespace {

using apogee::testing::CliHome;

/// Two echoing mock members and a suite of them; `extra` is appended.
std::string config_with(const std::filesystem::path& scripts, const std::string& extra = {}) {
    std::filesystem::create_directories(scripts);
    std::ofstream{scripts / "root.json"} << R"({"turns": [{"text": "ROOT<{{last_user}}>"}]})";
    std::ofstream{scripts / "helper.json"} << R"({"turns": [{"text": "HELPER<{{last_user}}>"}]})";
    return "backends:\n"
           "  root:\n    type: mock\n    model_path: " +
           (scripts / "root.json").generic_string() +
           "\n  helper:\n    type: mock\n    model_path: " +
           (scripts / "helper.json").generic_string() +
           "\nmodels:\n  default: root\nmemory:\n  recall: false\n"
           "suites:\n  duo:\n    description: The root and its helper.\n    members:\n"
           "      chat: root\n      utility: helper\n" +
           extra;
}

/// A throwaway install of that config.
struct Home {
    CliHome home{std::string{}};

    explicit Home(const std::string& extra = {}) {
        std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc}
            << config_with(home.home() / "scripts", extra);
    }
};

/// `text` with every occurrence of `from` replaced by `to`.
std::string replaced(std::string text, const std::string& from, const std::string& to) {
    if (from.empty()) {
        return text;
    }
    for (std::size_t at = text.find(from); at != std::string::npos;
         at = text.find(from, at + to.size())) {
        text.replace(at, from.size(), to);
    }
    return text;
}

/// What a run printed, with what differs between runs -- the home's path and
/// a chat's id -- named instead.
std::string normalized(const std::string& text, const Home& home) {
    std::string out = replaced(text, home.home.home().generic_string(), "<home>");
    out = replaced(out, home.home.home().string(), "<home>");
    static const std::regex kChatId{"[0-9]{8}-[0-9]{6}-[0-9a-f]{4}"};
    return std::regex_replace(out, kChatId, "<chat-id>");
}

/// One run of `args` on `home` with `input` on stdin, as the golden records
/// it: the command line, the input, the exit code, stdout, stderr.
std::string scenario(const Home& home, const std::vector<std::string>& args,
                     const std::string& input = {}) {
    const std::istringstream fed{input};
    std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
    std::string out;
    std::string err;
    const int code = home.home.run(args, &out, &err);
    std::cin.rdbuf(old_in);
    std::cin.clear();
    std::string said = "=== apogee";
    for (const std::string& arg : args) {
        said += " " + (arg.empty() ? std::string{"\"\""} : arg);
    }
    said += "\n--- stdin\n" + input + "--- exit " + std::to_string(code) + "\n--- stdout\n" +
            normalized(out, home) + "--- stderr\n" + normalized(err, home);
    return said;
}

/// Every slash command a chat answers on a pipe, and two it does not.
constexpr const char* kSlashBattery =
    "hello\n/help\n/models\n/model\n/model helper\nagain\n/model root\n/suite\n/think\n"
    "/think off\n/think\n/recall\n/retriever\n/retriever lexical\n/rerank\n/rerank off\n"
    "/temperature warm\n/max-tokens many\n/permissions\n/allow\n/branch\n/attachments\n"
    "/detach nothing\n/attach\n/check\n/title renamed\n/system be brief\n/compact\n"
    "/play summarize-verify x\n/symphonies\n/nonsense\n/exit\n";

/// The battery: one string, every scenario in order.
std::string battery() {
    std::string out;
    {
        // The REPL on a pipe: every command, then the same chat resumed.
        const Home home;
        out += scenario(home, {"chat"}, kSlashBattery);
        out += scenario(home, {"chat", "-c"}, "/model\n/suite\nwhat now\n");
    }
    {
        // A suite named, switched, turned off, refused, and resumed.
        const Home home;
        out += scenario(home, {"chat", "--suite", "duo"},
                        "hi\n/suite\n/suite off\nstill here\n/suite duo\n/suite nope\n"
                        "/suite duo --warm\nbye\n");
        out += scenario(home, {"chat", "-c"}, "/suite\nback\n");
    }
    {
        // The config's default suite, and a chat that turns it off.
        const Home home;
        std::ofstream{home.home.config_path(), std::ios::binary | std::ios::trunc}
            << replaced(config_with(home.home.home() / "scripts"), "  default: root\n",
                        "  default: root\n  default_suite: duo\n");
        out += scenario(home, {"chat"}, "hi\n/suite\n");
        out += scenario(home, {"chat", "--suite", "off"}, "hi\n/suite\n");
    }
    {
        // Machine mode: turns, an attach, and a slash line that is a prompt.
        const Home home;
        out += scenario(home, {"chat", "--output-format", "stream-json"},
                        "{\"type\":\"user\",\"text\":\"hello\"}\n"
                        "{\"type\":\"user\",\"text\":\"/play summarize-verify x\"}\n"
                        "{\"type\":\"attach\",\"path\":\"nothing-here\"}\n"
                        "{\"type\":\"nonsense\"}\n");
        out += scenario(home, {"chat", "--suite", "duo", "--output-format", "stream-json"},
                        "{\"type\":\"user\",\"text\":\"hi\"}\n");
    }
    {
        // Every launch refusal, each before a turn.
        const Home home;
        out += scenario(home, {"chat", "--suite", "nope"}, "hi\n");
        out += scenario(home, {"chat", "--warm"}, "hi\n");
        out += scenario(home, {"chat", "--input-format", "stream-json"}, "hi\n");
        out += scenario(home, {"chat", "--graph", "code"}, "hi\n");
        out += scenario(home, {"chat", "--allow", "write_file"}, "hi\n");
        out += scenario(home, {"chat", "--tools", "--allow", "nothing"}, "hi\n");
        out += scenario(home, {"chat", "--fetch", "--no-fetch"}, "hi\n");
        out += scenario(home, {"chat", "--resume", "nope"}, "hi\n");
        out += scenario(home, {"chat", "-c"}, "hi\n");
        out += scenario(home, {"chat", "--think", "sometimes"}, "hi\n");
    }
    {
        // The flags, as help and as completion.
        const Home home;
        out += scenario(home, {"chat", "--help"});
        out += scenario(home, {"__complete", "chat", "--"});
        out += scenario(home, {"__complete", "chat", "--suite", ""});
        out += scenario(home, {"__complete", "chat", "--think", ""});
        out += scenario(home, {"__complete", "chat", "--effort", ""});
        out += scenario(home, {"__complete", "chat", "--permission-mode", ""});
        out += scenario(home, {"__complete", "chat", "--output-format", ""});
    }
    return out;
}

std::optional<std::string> slurp(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in.good()) {
        return std::nullopt;
    }
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("chat's whole surface is byte for byte the one from before the session core was shared",
          "[chat][session][golden]") {
    const std::string actual = battery();
    // Written for review when asked to; never into the tree.
    if (const char* dir = std::getenv("APOGEE_CHAT_SESSION_GOLDENS");
        dir != nullptr && *dir != '\0') {
        std::ofstream{std::filesystem::path{dir} / "chat_session.golden", std::ios::binary}
            << actual;
    }
    const std::optional<std::string> golden =
        slurp(std::filesystem::path{APOGEE_TEST_FIXTURES} / "cli" / "chat_session.golden");
    REQUIRE(golden.has_value());
    CHECK(actual == *golden);
}

// --- a backend named at launch (M29) -------------------------------------------

namespace {

/// Two mocks that start -- `helper` running a model named `helper-own` --
/// and `gone`, a vendor CLI whose binary is nowhere: configured, never
/// started (M26's fixture). `alone` configures `gone` and nothing else.
struct NamedHome {
    apogee::testing::CliHome home{std::string{}};
    std::filesystem::path binary = home.home() / "nowhere" / "claude";

    explicit NamedHome(bool alone = false) {
        const std::filesystem::path scripts = home.home() / "scripts";
        std::filesystem::create_directories(scripts);
        std::ofstream{scripts / "root.json"} << R"({"turns": [{"text": "ROOT<{{last_user}}>"}]})";
        std::ofstream{scripts / "helper.json"}
            << R"({"turns": [{"text": "HELPER<{{last_user}}>"}]})";
        std::string config = "backends:\n  gone:\n    type: claude-cli\n    binary: \"" +
                             binary.generic_string() + "\"\n";
        if (!alone) {
            config += "  root:\n    type: mock\n    model_path: " +
                      (scripts / "root.json").generic_string() +
                      "\n  helper:\n    type: mock\n    model: helper-own\n    model_path: " +
                      (scripts / "helper.json").generic_string() + "\n";
        }
        config += "models:\n  default: " + std::string{alone ? "gone" : "root"} +
                  "\nmemory:\n  recall: false\n";
        std::ofstream{home.config_path(), std::ios::binary | std::ios::trunc} << config;
    }

    /// `apogee --config <config> <args>` with `input` on stdin.
    int run(const std::vector<std::string>& args, std::string& out, std::string& err,
            const std::string& input = "hi\n") const {
        const std::istringstream fed{input};
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        const int code = home.run(args, &out, &err);
        std::cin.rdbuf(old_in);
        std::cin.clear();
        return code;
    }

    /// The chats this home saved.
    [[nodiscard]] std::size_t saved() const {
        const std::filesystem::path sessions = home.home() / "sessions";
        std::error_code code;
        if (!std::filesystem::is_directory(sessions, code)) {
            return 0;
        }
        return static_cast<std::size_t>(std::distance(std::filesystem::directory_iterator{sessions},
                                                      std::filesystem::directory_iterator{}));
    }
};

constexpr const char* kDrivenHi = "{\"type\":\"user\",\"text\":\"hi\"}\n";

}  // namespace

TEST_CASE("a backend named at launch that cannot start is refused with its own reason",
          "[chat][session][named-backend]") {
    // The macOS app's probe: `chat -m <a backend whose runtime is missing>`
    // exited 0, the default's answer under the named backend's label.
    for (const std::string mode : {"chat", "execute"}) {
        INFO(mode);
        const NamedHome home;
        std::string out;
        std::string err;
        const std::string refusal =
            "apogee " + mode + ": backend 'gone' is configured but could not be started: ";
        CHECK(home.run({mode, "-m", "gone"}, out, err) == 1);
        CHECK(err.starts_with(refusal));
        // Its own reason, once named -- the factory's `gone: ` dropped: the
        // missing binary, or a platform that starts no vendor CLI at all.
        CHECK(err.find(apogee::platform::supports_child_processes()
                           ? home.binary.filename().string()
                           : "cannot spawn child processes") != std::string::npos);
        CHECK(err.find("gone: ") == std::string::npos);
        CHECK(err.ends_with(" (started backends: helper, root)\n"));
        CHECK(out.empty());

        // Driven: refused at launch, before the session event -- stdout
        // carries nothing, and nothing answers under its name.
        CHECK(home.run({mode, "-m", "gone", "--output-format", "stream-json"}, out, err,
                       kDrivenHi) == 1);
        CHECK(out.empty());
        CHECK(err.starts_with(refusal));
        CHECK(home.saved() == 0);

        // A name configured nowhere is refused too, never answered by the
        // default.
        CHECK(home.run({mode, "-m", "nope", "--output-format", "stream-json"}, out, err,
                       kDrivenHi) == 1);
        CHECK(out.empty());
        CHECK(err.starts_with("apogee " + mode + ": no backend named 'nope' (configured: "));
        CHECK(home.saved() == 0);
    }
}

TEST_CASE("a backend named with its own model answers for itself, never the default",
          "[chat][session][named-backend]") {
    // `<backend>:<model>` naming the entry's own model pins nothing (34) --
    // and was handed to the router as typed, which knew no such key and
    // fell to the default.
    const NamedHome home;
    std::string out;
    std::string err;
    REQUIRE(home.run({"chat", "-m", "helper:helper-own"}, out, err) == 0);
    CHECK(out == "HELPER<hi>\n");
    REQUIRE(home.run({"chat", "-m", "helper:helper-own", "--output-format", "stream-json"}, out,
                     err, kDrivenHi) == 0);
    std::istringstream lines{out};
    std::vector<nlohmann::json> events;
    for (std::string line; std::getline(lines, line);) {
        events.push_back(nlohmann::json::parse(line));
    }
    REQUIRE(events.size() >= 2);
    CHECK(events.front().at("model") == "helper");
    CHECK(events.back().at("type") == "result");
    CHECK(events.back().at("model") == "helper");
    CHECK(events.back().at("text") == "HELPER<hi>");
}

TEST_CASE("with nothing started, a named backend is refused for itself before the whole list",
          "[chat][session][named-backend]") {
    // The refusal names the backend asked for, not every backend's problem.
    for (const std::string mode : {"chat", "execute"}) {
        INFO(mode);
        const NamedHome home{true};
        std::string out;
        std::string err;
        CHECK(home.run({mode, "-m", "gone"}, out, err) == 1);
        CHECK(err.starts_with("apogee " + mode +
                              ": backend 'gone' is configured but could not be started: "));
        CHECK(err.ends_with(" (no backend started; 'apogee check' says why)\n"));
        CHECK(home.run({mode, "-m", "nope"}, out, err) == 1);
        CHECK(err == "apogee " + mode + ": no backend named 'nope' (configured: gone)\n");
        // Asked for nothing, every backend's problem is still the answer.
        CHECK(home.run({mode}, out, err) == 1);
        CHECK(err.starts_with("apogee " + mode + ": no usable backend is configured -- gone ("));
        CHECK(out.empty());
    }
}
