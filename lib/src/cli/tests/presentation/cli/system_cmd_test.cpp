#include "cli/system_cmd.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <memory>
#include <sstream>
#include <string>
#include <vector>

#include "cli/registry.h"
#include "cli/root.h"
#include "operations/system_view.h"
#include "support/cli_home.h"
#include "support/env_guard.h"

/// `apogee system` (32a) on the real command tree over a fake OS: one JSON
/// document, the table plain and byte-stable, the model budget read once and
/// from the one function sizing reads -- and the command completing.
namespace {

namespace platform = apogee::platform;
using apogee::commands::SystemSeams;

class FakeSystem final : public platform::SystemSource {
public:
    [[nodiscard]] platform::CpuInfo cpu() const override {
        return {.model = "Fake 9000", .physical_cores = 8, .logical_cores = 8};
    }

    [[nodiscard]] std::optional<platform::CpuTimes> cpu_times() const override {
        ++reads_;
        return platform::CpuTimes{.busy = 100 * reads_, .total = 400 * reads_};
    }

    [[nodiscard]] std::optional<platform::LoadAverage> load_average() const override {
        return platform::LoadAverage{.one_minute = 1, .five_minutes = 2, .fifteen_minutes = 3};
    }

    [[nodiscard]] std::string load_unknown() const override {
        return {};
    }

    [[nodiscard]] platform::MemoryInfo memory() const override {
        return {.total = std::int64_t{16} << 30U, .available = std::int64_t{4} << 30U};
    }

    [[nodiscard]] std::optional<std::int64_t> process_footprint() const override {
        return std::int64_t{30} << 20U;
    }

    [[nodiscard]] platform::GpuInfo gpu() const override {
        return {.unknown = "not read on this platform yet"};
    }

    [[nodiscard]] platform::VolumeInfo volume(
        const std::filesystem::path& /*path*/) const override {
        return {.capacity = std::int64_t{1} << 40U, .available = std::int64_t{1} << 39U};
    }

private:
    mutable std::uint64_t reads_ = 0;
};

struct Run {
    int code = -1;
    std::string out;
    std::string err;
};

/// `apogee system <args>` over `seams`, its streams captured.
[[nodiscard]] Run run_system(const SystemSeams& seams, const std::vector<std::string>& args) {
    apogee::commands::CommandRegistry registry;
    registry.add(std::make_unique<apogee::commands::SystemCommand>(seams));
    apogee::commands::RootCommand root{std::move(registry)};
    std::vector<const char*> argv{"apogee", "system"};
    for (const std::string& arg : args) {
        argv.push_back(arg.c_str());
    }
    const std::ostringstream out;
    const std::ostringstream err;
    std::streambuf* old_out = std::cout.rdbuf(out.rdbuf());
    std::streambuf* old_err = std::cerr.rdbuf(err.rdbuf());
    Run run;
    run.code = root.run(static_cast<int>(argv.size()), argv.data());
    std::cout.rdbuf(old_out);
    std::cerr.rdbuf(old_err);
    run.out = out.str();
    run.err = err.str();
    return run;
}

}  // namespace

TEST_CASE("system prints one document, the model budget read once from its source",
          "[cli][system]") {
    const apogee::testing::TempDir home{"system-cmd"};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    const FakeSystem os;
    int budget_reads = 0;
    const SystemSeams seams{
        .machine =
            [&budget_reads]() {
                ++budget_reads;
                return apogee::models::MachineBudget{.bytes = std::int64_t{12} << 30U};
            },
        .source = &os,
        .wait = [](std::chrono::milliseconds) {}};

    const Run json = run_system(seams, {"--output-format", "json"});
    REQUIRE(json.code == 0);
    CHECK(json.err.empty());
    CHECK(budget_reads == 1);
    REQUIRE(std::ranges::count(json.out, '\n') == 1);
    const nlohmann::json document = nlohmann::json::parse(json.out);
    CHECK(document["gpu"]["model_budget_bytes"] == std::int64_t{12} << 30U);
    CHECK(document["cpu"]["utilization"] ==
          nlohmann::json{{"percent", 25.0}, {"window_ms", platform::kUtilizationWindow.count()}});
    CHECK(document["memory"]["used_bytes"] == std::int64_t{12} << 30U);
    CHECK(document["disk"]["store"] == (home.path() / "models").string());
    CHECK(document["disk"]["store_bytes"] == 0);

    // The table says the same budget, in the same units, and is plain.
    budget_reads = 0;
    const Run text = run_system(seams, {});
    REQUIRE(text.code == 0);
    CHECK(budget_reads == 1);
    CHECK(text.out.find("for models: 12.0 GiB") != std::string::npos);
    CHECK(text.out.find("usage:  25% over 500 ms") != std::string::npos);
    CHECK(text.out.find('\x1b') == std::string::npos);
    // Byte-stable: the same machine prints the same bytes.
    CHECK(run_system(seams, {}).out == text.out);
}

TEST_CASE("system's model budget is the very function sizing and admission read", "[cli][system]") {
    // One symbol, by construction: the default seam targets `machine_budget`,
    // the source chat, execute, task and the suite admission default to --
    // `backends::offload_memory_total`, the devices' memory 26a sizes by.
    const SystemSeams seams;
    const auto* target = seams.machine.target<apogee::models::MachineBudget (*)()>();
    REQUIRE(target != nullptr);
    CHECK(*target == &apogee::commands::machine_budget);
    CHECK(seams.source == &platform::host_system());
}

TEST_CASE("an unknown budget is said with its reason", "[cli][system]") {
    const apogee::testing::TempDir home{"system-cmd-unknown"};
    const apogee::testing::EnvGuard guard{"APOGEE_HOME", home.path().string()};
    const FakeSystem os;
    const SystemSeams seams{.machine = []() { return apogee::models::MachineBudget{}; },
                            .source = &os,
                            .wait = [](std::chrono::milliseconds) {}};
    const Run text = run_system(seams, {});
    REQUIRE(text.code == 0);
    CHECK(
        text.out.find("gpu:    unknown (not read on this platform yet) · for models: unknown (") !=
        std::string::npos);
    const nlohmann::json document =
        nlohmann::json::parse(run_system(seams, {"--output-format", "json"}).out);
    CHECK(document["gpu"]["model_budget_bytes"].is_null());
    CHECK_FALSE(document["gpu"]["model_budget_unknown"].get<std::string>().empty());
}

TEST_CASE("system refuses a turn's stream format by name", "[cli][system]") {
    const FakeSystem os;
    const SystemSeams seams{.source = &os, .wait = [](std::chrono::milliseconds) {}};
    const Run run = run_system(seams, {"--output-format", "stream-json"});
    CHECK(run.code != 0);
    CHECK(run.out.empty());
}

TEST_CASE("system and its flag complete, and it is in the help", "[cli][system][completion]") {
    const apogee::testing::CliHome home{"backends: {}\n"};
    std::string out;
    std::string err;
    REQUIRE(home.run({"__complete", "sys"}, &out, &err) == 0);
    CHECK(out.find("system") != std::string::npos);
    out.clear();
    REQUIRE(home.run({"__complete", "system", "--"}, &out, &err) == 0);
    CHECK(out.find("--output-format") != std::string::npos);
    out.clear();
    REQUIRE(home.run({"__complete", "system", "--output-format", ""}, &out, &err) == 0);
    CHECK(out.find("json") != std::string::npos);
    CHECK(out.find("text") != std::string::npos);
    out.clear();
    REQUIRE(home.run({"--help"}, &out, &err) == 0);
    CHECK(out.find("system") != std::string::npos);
}
