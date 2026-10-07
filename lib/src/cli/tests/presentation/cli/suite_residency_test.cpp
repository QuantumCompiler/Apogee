#include "cli/suite_residency.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <random>
#include <sstream>
#include <string>
#include <vector>

#include "backends/llamacpp.h"
#include "cli/chat.h"
#include "cli/models.h"
#include "cli/registry.h"
#include "cli/root.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "modelstore/footprint.h"
#include "modelstore/sidecar.h"
#include "support/env_guard.h"
#include "support/fake_llama.h"
#include "support/gguf_builder.h"
#include "views/status_line.h"

/// A suite as a set in a session (27e), as a person reads it: the arithmetic
/// selecting a suite states, word for word; the refusal and `--force`; the
/// footprint `models status` and `/suite` show; the warmup walk on M1's busy
/// line, and its silence where that line is silent.
namespace {

using apogee::models::LocalWindow;
using apogee::models::MachineBudget;
using apogee::models::MemberFootprint;
using apogee::models::SuiteFootprint;

constexpr std::int64_t kMiB = std::int64_t{1024} * 1024;
constexpr std::int64_t kGiB = 1024 * kMiB;

/// A local member already priced: `weights` and `cache` in MiB at `window`.
[[nodiscard]] MemberFootprint local(const std::string& backend, std::vector<std::string> roles,
                                    std::int64_t weights, std::int64_t cache, std::int64_t window) {
    MemberFootprint member;
    member.backend = backend;
    member.roles = std::move(roles);
    member.local = true;
    member.weights = weights * kMiB;
    LocalWindow priced;
    priced.window = window;
    priced.cache_bytes = cache * kMiB;
    member.window = priced;
    return member;
}

/// research: root at 32K, helper at its 4K pin for two roles, a cloud vision
/// member that costs this machine nothing.
[[nodiscard]] SuiteFootprint research(std::optional<std::int64_t> budget) {
    SuiteFootprint footprint;
    footprint.suite = "research";
    footprint.members.push_back(local("root", {"chat"}, 1926, 1904, 32768));
    footprint.members.push_back(local("helper", {"embedding", "utility"}, 770, 68, 4096));
    MemberFootprint cloud;
    cloud.backend = "claude";
    cloud.roles = {"vision"};
    footprint.members.push_back(cloud);
    footprint.machine = MachineBudget{.bytes = budget};
    return footprint;
}

}  // namespace

TEST_CASE("selecting a suite states its arithmetic, word for word",
          "[commands][suites][residency]") {
    using apogee::commands::admission_line;
    using apogee::commands::admission_refusal;

    // Fits: each local member's MiB, the margin, the sum, the machine.
    const SuiteFootprint fits = research(96 * kGiB);
    CHECK(admission_line(fits) ==
          "suite research needs 5692 MiB of this machine's 98304 MiB: root 3830 + helper 838 + "
          "1024 margin -- fits");
    CHECK(admission_refusal(fits, "--force").empty());

    // Over: the same numbers, refused with the way past it -- and forced, said
    // so on the line that stays.
    const SuiteFootprint over = research(4 * kGiB);
    CHECK(admission_line(over) ==
          "suite research needs 5692 MiB of this machine's 4096 MiB: root 3830 + helper 838 + "
          "1024 margin -- it does not fit");
    CHECK(admission_refusal(over, "--force") ==
          "suite research needs 5692 MiB of this machine's 4096 MiB: root 3830 + helper 838 + "
          "1024 margin -- it does not fit; --force runs it anyway");
    CHECK(admission_refusal(over, "/suite research --force")
              .ends_with("; /suite research --force runs it anyway"));
    CHECK(admission_line(over, true) ==
          "suite research needs 5692 MiB of this machine's 4096 MiB: root 3830 + helper 838 + "
          "1024 margin -- over budget, run anyway (--force)");

    // An unknown size: named, the sum a floor, and no refusal on its account.
    SuiteFootprint unknown = research(96 * kGiB);
    MemberFootprint embedder;
    embedder.backend = "embedder";
    embedder.roles = {"extraction"};
    embedder.local = true;
    embedder.unknown = "no recorded size";
    unknown.members.push_back(embedder);
    CHECK(admission_line(unknown) ==
          "suite research needs at least 5692 MiB of this machine's 98304 MiB: root 3830 + "
          "helper 838 + embedder unknown (no recorded size) + 1024 margin -- fits as far as known");
    CHECK(admission_refusal(unknown, "--force").empty());

    // A machine whose budget nothing reports: stated, never judged.
    CHECK(admission_line(research(std::nullopt)) ==
          "suite research needs 5692 MiB: root 3830 + helper 838 + 1024 margin -- this "
          "machine's budget is not known here");
    CHECK(admission_refusal(research(std::nullopt), "--force").empty());

    // Nothing held here at all.
    SuiteFootprint cloud;
    cloud.suite = "fast";
    MemberFootprint remote;
    remote.backend = "claude";
    remote.roles = {"chat"};
    cloud.members.push_back(remote);
    CHECK(admission_line(cloud) == "suite fast: no member holds memory on this machine");
}

TEST_CASE("the footprint block: each member, resident or not, and the set's total",
          "[commands][suites][residency]") {
    SuiteFootprint footprint = research(96 * kGiB);
    MemberFootprint embedder;
    embedder.backend = "embedder";
    embedder.roles = {"extraction"};
    embedder.local = true;
    embedder.unknown = "no recorded size";
    footprint.members.push_back(embedder);

    // A session's: what its harness holds.
    const auto resident = [](std::string_view backend) -> std::optional<bool> {
        if (backend == "root") {
            return true;
        }
        if (backend == "claude") {
            return std::nullopt;
        }
        return false;
    };
    const std::vector<std::string> lines = apogee::commands::footprint_lines(footprint, resident);
    REQUIRE(lines.size() == 6);
    CHECK(lines[0] == "footprint: suite research");
    CHECK(lines[1] ==
          "  root (chat): 3830 MiB -- 1926 MiB of weights, 1904 MiB of q8_0 cache at 32768 tokens "
          "· resident");
    CHECK(lines[2] ==
          "  helper (embedding, utility): 838 MiB -- 770 MiB of weights, 68 MiB of q8_0 cache at "
          "4096 tokens · not loaded");
    CHECK(lines[3] == "  claude (vision): nothing held here");
    CHECK(lines[4] == "  embedder (extraction): unknown -- no recorded size · not loaded");
    CHECK(lines[5] ==
          "  total: suite research needs at least 5692 MiB of this machine's 98304 MiB: root 3830 "
          "+ helper 838 + embedder unknown (no recorded size) + 1024 margin -- fits as far as "
          "known");

    // `models status` loads nothing, so it claims no residency.
    const std::vector<std::string> status = apogee::commands::footprint_lines(footprint);
    CHECK(status[1] ==
          "  root (chat): 3830 MiB -- 1926 MiB of weights, 1904 MiB of q8_0 cache at 32768 tokens");
}

TEST_CASE("/suite's argument: a suite, then --force and --warm", "[commands][suites][residency]") {
    using apogee::commands::parse_suite_argument;
    const apogee::commands::SuiteArgument plain = parse_suite_argument("research");
    CHECK(plain.suite == "research");
    CHECK_FALSE(plain.force);
    CHECK_FALSE(plain.warm);
    CHECK(plain.error.empty());

    const apogee::commands::SuiteArgument both = parse_suite_argument("--warm  big --force");
    CHECK(both.suite == "big");
    CHECK(both.force);
    CHECK(both.warm);
    CHECK(both.error.empty());

    CHECK(parse_suite_argument("big --hot").error ==
          "unknown option '--hot' -- /suite <name|off> [--force] [--warm]");
    CHECK(parse_suite_argument("big small").error ==
          "one suite at a time -- /suite <name|off> [--force] [--warm]");
    CHECK(parse_suite_argument("--force").error ==
          "name the suite -- /suite <name|off> [--force] [--warm]");
}

TEST_CASE("the banner records a forced suite", "[commands][suites][residency]") {
    CHECK(apogee::commands::banner_suite("research", false) == "  ·  suite research");
    CHECK(apogee::commands::banner_suite("research", true) ==
          "  ·  suite research (over budget, --force)");
    CHECK(apogee::commands::banner_suite("", false).empty());
}

// ---------------------------------------------------------------------------
// The warmup walk, on the busy line
// ---------------------------------------------------------------------------

namespace {

/// A harness whose suite `research` has two local members over scripted
/// runtimes, each load taking long enough for the line to paint.
struct WarmSession {
    apogee::harness::Config config = apogee::harness::parse_config(R"(models:
  default: root
  default_suite: research
backends:
  root:
    type: llamacpp
    model_path: /models/root.gguf
  helper:
    type: llamacpp
    model_path: /models/helper.gguf
  claude:
    type: anthropic
    model: claude-sonnet
suites:
  research:
    members:
      chat: root
      vision: claude
      utility: helper
)",
                                                                   "suite_residency_test");
    apogee::harness::Harness harness{config};
    std::vector<apogee::testing::FakeLlamaRuntime*> runtimes;

    explicit WarmSession(std::chrono::milliseconds load) {
        for (const std::string& name : std::vector<std::string>{"root", "helper"}) {
            auto runtime = std::make_unique<apogee::testing::FakeLlamaRuntime>();
            runtime->load_delay = load;
            runtimes.push_back(runtime.get());
            apogee::backends::LlamaCppProvider::Options options;
            options.backend_name = name;
            options.model = name;
            options.model_path = "/models/" + name + ".gguf";
            harness.register_provider(name, std::make_shared<apogee::backends::LlamaCppProvider>(
                                                std::move(options), std::move(runtime)));
        }
        harness.use_default_router();
    }
};

}  // namespace

TEST_CASE("warming a suite rides the busy line, member by member",
          "[commands][suites][residency]") {
    // The frame each member gets, as M1's general frame paints it.
    CHECK(apogee::commands::spinner_frame(apogee::commands::warm_label("research", "helper"), 0, 0,
                                          apogee::commands::BusyCount{.done = 2, .total = 2},
                                          80) == "✻ warming suite research: loading helper (2/2)");

    WarmSession session{std::chrono::milliseconds{150}};
    std::ostringstream terminal;
    std::vector<std::string> failed;
    {
        apogee::commands::BusyLine line{
            terminal, "warming suite research",
            apogee::commands::BusyLine::Options{.active = true,
                                                .delay = std::chrono::milliseconds{0},
                                                .interval = std::chrono::milliseconds{10},
                                                .width = [] { return std::size_t{80}; }}};
        failed = apogee::commands::warm_suite(session.harness, session.config, line);
    }
    CHECK(failed.empty());
    CHECK(session.runtimes[0]->loads == 1);
    CHECK(session.runtimes[1]->loads == 1);
    const std::string painted = terminal.str();
    // In role order, numbered against the members that had something to
    // load -- the cloud member is never asked.
    const std::size_t first = painted.find("warming suite research: loading root (1/2)");
    const std::size_t second = painted.find("warming suite research: loading helper (2/2)");
    CHECK(first != std::string::npos);
    CHECK(second != std::string::npos);
    CHECK(first < second);
    CHECK(painted.find("claude") == std::string::npos);
    // Cleared when done: the last thing written erases the line.
    CHECK(painted.ends_with("\r\x1b[2K"));
}

TEST_CASE("warming a suite writes nothing where the line is silent",
          "[commands][suites][residency]") {
    // A pipe, --quiet, JSON output: busy_options(quiet) makes the line
    // inactive, and an inactive line writes no byte -- the members still load.
    WarmSession session{std::chrono::milliseconds{50}};
    apogee::commands::BusyLine::Options inactive;
    inactive.active = false;
    std::ostringstream silent;
    {
        apogee::commands::BusyLine line{silent, "warming suite research", inactive};
        CHECK(apogee::commands::warm_suite(session.harness, session.config, line).empty());
    }
    CHECK(silent.str().empty());
    CHECK(session.runtimes[0]->loads == 1);
    CHECK(session.runtimes[1]->loads == 1);
    // A suite already resident has nothing to warm, and nothing is loaded
    // twice.
    apogee::commands::BusyLine::Options active;
    active.active = true;
    active.delay = std::chrono::milliseconds{0};
    std::ostringstream again;
    {
        apogee::commands::BusyLine line{again, "warming suite research", active};
        CHECK(apogee::commands::warm_suite(session.harness, session.config, line).empty());
    }
    CHECK(session.runtimes[0]->loads == 1);
    CHECK(session.runtimes[1]->loads == 1);
}

TEST_CASE("a member that cannot be warmed is said, and the walk goes on",
          "[commands][suites][residency]") {
    WarmSession session{std::chrono::milliseconds{0}};
    session.runtimes[0]->load_error = "could not load the model at '/models/root.gguf'";
    std::ostringstream silent;
    apogee::commands::BusyLine::Options inactive;
    inactive.active = false;
    apogee::commands::BusyLine line{silent, "warming", inactive};
    const std::vector<std::string> failed =
        apogee::commands::warm_suite(session.harness, session.config, line);
    REQUIRE(failed.size() == 1);
    CHECK(failed.front().starts_with("could not warm root -- its first use will try again: "));
    CHECK(failed.front().find("/models/root.gguf") != std::string::npos);
    CHECK(session.runtimes[1]->loads == 1);
}

// ---------------------------------------------------------------------------
// Admission through `apogee chat`
// ---------------------------------------------------------------------------

namespace {

/// `apogee chat` in process over a sandbox: a mock chat model and three local
/// extraction members -- one recorded far beyond any machine, one small, one
/// with no record -- priced against a fixed 64 GiB machine. Extraction is
/// never asked in a chat, so no local model is ever loaded.
struct AdmissionChat {
    apogee::testing::TempDir home{"suite-admission-" + std::to_string(std::random_device{}())};
    apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    std::filesystem::path config_path = home.path() / "config" / "config.yaml";
    std::string out;
    std::string err;

    AdmissionChat() {
        std::filesystem::create_directories(config_path.parent_path());
        const std::filesystem::path script = home.path() / "root.json";
        std::ofstream{script, std::ios::binary}
            << nlohmann::json{{"turns", {{{"text", "ROOT-SAYS"}}}}}.dump();
        std::ofstream{config_path, std::ios::binary}
            << "models:\n  default: root\nbackends:\n  root:\n    type: mock\n    model_path: "
            << script.string() << "\n"
            << "  big:\n    type: llamacpp\n    model_path: " << model("big", 200 * kGiB).string()
            << "\n"
            << "  small:\n    type: llamacpp\n    model_path: "
            << model("small", 770 * kMiB).string() << "\n"
            << "  loose:\n    type: llamacpp\n    model_path: "
            << model("loose", std::nullopt).string() << "\n"
            << "suites:\n"
            << "  roomy:\n    members:\n      chat: root\n      extraction: small\n"
            << "  huge:\n    members:\n      chat: root\n      extraction: big\n"
            << "  partial:\n    members:\n      chat: root\n      extraction: loose\n";
    }

    /// A Llama 3.2 1B header -- 544 MiB of q8_0 cache at 32K -- and, unless
    /// `recorded` is nullopt, the record the store keeps beside it.
    [[nodiscard]] std::filesystem::path model(const std::string& name,
                                              std::optional<std::int64_t> recorded) const {
        apogee::testing::GgufBuilder builder;
        builder.magic().u32(3).u64(1).u64(6);
        builder.string_kv("general.architecture", "llama");
        builder.u32_kv("llama.block_count", 16);
        builder.u32_kv("llama.context_length", 131072);
        builder.u32_kv("llama.embedding_length", 2048);
        builder.u32_kv("llama.attention.head_count", 32);
        builder.u32_kv("llama.attention.head_count_kv", 8);
        builder.tensor("token_embd.weight");
        const std::filesystem::path file = home.path() / (name + ".gguf");
        std::ofstream{file, std::ios::binary} << builder.bytes();
        if (recorded.has_value()) {
            apogee::models::Sidecar record;
            record.file = file.filename().string();
            record.file_size = *recorded;
            REQUIRE(apogee::models::write_sidecar(file, record));
        }
        return file;
    }

    /// `apogee <args>` in process with `input` on stdin, the machine 64 GiB.
    int run(const std::vector<std::string>& args, const std::string& input = {}) {
        std::ostringstream captured_out;
        std::ostringstream captured_err;
        std::istringstream fed{input};
        std::streambuf* old_out = std::cout.rdbuf(captured_out.rdbuf());
        std::streambuf* old_err = std::cerr.rdbuf(captured_err.rdbuf());
        std::streambuf* old_in = std::cin.rdbuf(fed.rdbuf());
        int code = -1;
        {
            apogee::commands::CommandRegistry registry;
            registry.add(std::make_unique<apogee::commands::ChatCommand>(
                [] { return MachineBudget{.bytes = 64 * kGiB}; }));
            apogee::commands::RootCommand root{std::move(registry)};
            const std::string path = config_path.string();
            std::vector<const char*> argv{"apogee", "--config", path.c_str()};
            for (const std::string& arg : args) {
                argv.push_back(arg.c_str());
            }
            code = root.run(static_cast<int>(argv.size()), argv.data());
        }
        std::cout.rdbuf(old_out);
        std::cerr.rdbuf(old_err);
        std::cin.rdbuf(old_in);
        std::cin.clear();
        out = captured_out.str();
        err = captured_err.str();
        return code;
    }
};

}  // namespace

TEST_CASE("a chat under a suite that fits states it and proceeds",
          "[commands][suites][residency]") {
    AdmissionChat chat;
    // small: 770 MiB recorded + 544 MiB of cache at 32K.
    REQUIRE(chat.run({"chat", "--suite", "roomy"}, "hello\n") == 0);
    CHECK(chat.err.find("suite roomy needs 2338 MiB of this machine's 65536 MiB: small 1314 + "
                        "1024 margin -- fits") != std::string::npos);
    CHECK(chat.out.find("ROOT-SAYS") != std::string::npos);
}

TEST_CASE("a chat under a suite that cannot fit is refused with the numbers; --force runs it",
          "[commands][suites][residency]") {
    AdmissionChat chat;
    CHECK(chat.run({"chat", "--suite", "huge"}, "hello\n") == 1);
    CHECK(chat.err.find("apogee chat: suite huge needs 206368 MiB of this machine's 65536 MiB: big "
                        "205344 + 1024 margin -- it does not fit; --force runs it anyway") !=
          std::string::npos);
    CHECK(chat.out.find("ROOT-SAYS") == std::string::npos);

    REQUIRE(chat.run({"chat", "--suite", "huge", "--force"}, "hello\n") == 0);
    CHECK(chat.err.find("suite huge needs 206368 MiB of this machine's 65536 MiB: big 205344 + "
                        "1024 margin -- over budget, run anyway (--force)") != std::string::npos);
    CHECK(chat.out.find("ROOT-SAYS") != std::string::npos);
}

TEST_CASE("a member with no recorded size is said to be unknown, and the chat still runs",
          "[commands][suites][residency]") {
    AdmissionChat chat;
    REQUIRE(chat.run({"chat", "--suite", "partial"}, "hello\n") == 0);
    CHECK(chat.err.find("suite partial needs at least 1024 MiB of this machine's 65536 MiB: loose "
                        "unknown (no recorded size) + 1024 margin -- fits as far as known") !=
          std::string::npos);
    CHECK(chat.out.find("ROOT-SAYS") != std::string::npos);
}

TEST_CASE("/suite refuses a suite that cannot fit, and --force switches to it",
          "[commands][suites][residency]") {
    AdmissionChat chat;
    REQUIRE(chat.run({"chat", "--suite", "roomy"},
                     "/suite huge\n/suite\n/suite huge --force\n/suite\n/suite roomy --hot\n") ==
            0);
    CHECK(chat.err.find("it does not fit; /suite huge --force runs it anyway") !=
          std::string::npos);
    // Refused: still roomy, its footprint shown by /suite with each member's
    // residency -- small never asked for, so not loaded.
    CHECK(chat.err.find("suite: roomy -- chat root · extraction small") != std::string::npos);
    CHECK(chat.err.find("footprint: suite roomy") != std::string::npos);
    CHECK(chat.err.find("  root (chat): nothing held here") != std::string::npos);
    // Forced: switched, and said so with the numbers.
    CHECK(chat.err.find("suite huge -- chat root · extraction big") != std::string::npos);
    CHECK(chat.err.find("over budget, run anyway (--force)") != std::string::npos);
    CHECK(chat.err.find("footprint: suite huge") != std::string::npos);
    CHECK(chat.err.find("unknown option '--hot'") != std::string::npos);
}

TEST_CASE("warming needs a suite, and paints nothing on a pipe", "[commands][suites][residency]") {
    AdmissionChat chat;
    CHECK(chat.run({"chat", "--warm"}, "hello\n") == 1);
    CHECK(chat.err.find("--warm loads a suite's members, and this chat runs under none") !=
          std::string::npos);
    // With one, on a pipe: nothing painted -- no escape byte anywhere.
    REQUIRE(chat.run({"chat", "--suite", "roomy", "--warm"}, "hello\n") == 0);
    CHECK(chat.err.find('\x1b') == std::string::npos);
    REQUIRE(chat.run({"chat", "--suite", "roomy", "--warm", "--quiet"}, "hello\n") == 0);
    CHECK(chat.err.find('\x1b') == std::string::npos);
}

TEST_CASE("models status prices the active suite against the machine",
          "[commands][suites][residency]") {
    const AdmissionChat chat;
    apogee::harness::Config config = apogee::harness::load_config(chat.config_path);
    config.models.default_suite = "roomy";
    const std::string status = apogee::commands::render_role_status(
        config, {}, [] { return MachineBudget{.bytes = 64 * kGiB}; });
    CHECK(status.find("footprint: suite roomy\n"
                      "  root (chat): nothing held here\n"
                      "  small (extraction): 1314 MiB -- 770 MiB of weights, 544 MiB of q8_0 "
                      "cache at 32768 tokens\n"
                      "  total: suite roomy needs 2338 MiB of this machine's 65536 MiB: small "
                      "1314 + 1024 margin -- fits\n") != std::string::npos);
}
