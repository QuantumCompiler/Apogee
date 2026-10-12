#include "tui/monitor_bar.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "operations/system_view.h"
#include "tui/pump.h"
#include "tui/shell.h"
#include "tui/view.h"

/// The system monitor (32e) on a manual pump and a fake machine: the strip's
/// words in `apogee system`'s units, every reading known and none, the tick's
/// cadence held while the shell is busy, a stalled probe never blocking a
/// frame -- the bar going stale with its age -- and no burst once it recovers.
namespace {

namespace platform = apogee::platform;
using std::chrono::milliseconds;
using std::chrono::seconds;

/// A machine that answers as told, counting its reads -- and, gated, holding
/// a read until it is let go.
class FakeMachine final : public platform::SystemSource {
public:
    bool answers = true;
    bool gated = false;
    mutable std::atomic<int> time_reads{0};
    mutable std::atomic<int> waiting{0};

    void release() {
        {
            const std::lock_guard lock{mutex_};
            gated = false;
        }
        open_.notify_all();
    }

    [[nodiscard]] platform::CpuInfo cpu() const override {
        return {};
    }

    [[nodiscard]] std::optional<platform::CpuTimes> cpu_times() const override {
        {
            std::unique_lock lock{mutex_};
            ++waiting;
            open_.wait(lock, [this]() { return !gated; });
            --waiting;
        }
        const int read = ++time_reads;
        if (!answers) {
            return std::nullopt;
        }
        return platform::CpuTimes{.busy = static_cast<std::uint64_t>(100 * read),
                                  .total = static_cast<std::uint64_t>(400 * read)};
    }

    [[nodiscard]] std::optional<platform::LoadAverage> load_average() const override {
        if (!answers) {
            return std::nullopt;
        }
        return platform::LoadAverage{.one_minute = 1.5, .five_minutes = 1.25, .fifteen_minutes = 1};
    }

    [[nodiscard]] std::string load_unknown() const override {
        return {};
    }

    [[nodiscard]] platform::MemoryInfo memory() const override {
        if (!answers) {
            return {};
        }
        return {.total = std::int64_t{64} << 30U, .available = std::int64_t{24} << 30U};
    }

    [[nodiscard]] std::optional<std::int64_t> process_footprint() const override {
        if (!answers) {
            return std::nullopt;
        }
        return std::int64_t{42} << 20U;
    }

    [[nodiscard]] platform::GpuInfo gpu() const override {
        return platform::GpuInfo{.name = "Fake 9000", .unified_memory = true};
    }

    [[nodiscard]] platform::VolumeInfo volume(
        const std::filesystem::path& /*path*/) const override {
        return {};
    }

private:
    mutable std::mutex mutex_;
    mutable std::condition_variable open_;
};

struct Stage {
    explicit Stage(FakeMachine& machine, std::vector<std::string> held = {})
        : bar{pump, {.color = false}, machine, apogee::tui::MonitorOptions{.held = [held]() {
                  return held;
              }}} {
        shell.add(apogee::tui::text_view("Home", {"a view"}));
        shell.set_bottom_bar(bar.view());
    }

    [[nodiscard]] std::string strip() {
        (void)pump.drain();
        const std::string frame = shell.render_text(200, 6);
        const std::size_t last = frame.rfind('\n', frame.size() - 2);
        return frame.substr(last + 1);
    }

    /// The clock on, a tick's sample taken and arrived.
    void tick(milliseconds by = milliseconds{2000}) {
        pump.advance(by);
        (void)pump.drain();
        bar.settle();
        (void)pump.drain();
    }

    apogee::tui::ManualPump pump;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    apogee::tui::MonitorBar bar;
};

}  // namespace

TEST_CASE("the strip says each reading in apogee system's units, and leaves out what is unknown",
          "[tui][monitor]") {
    const platform::MachineSample full{
        .times = platform::CpuTimes{.busy = 1, .total = 2},
        .utilization = 14.4,
        .load =
            platform::LoadAverage{
                .one_minute = 2.314, .five_minutes = 1.98, .fifteen_minutes = 1.75},
        .memory = {.total = std::int64_t{64} << 30U, .available = std::int64_t{24} << 30U},
        .process_footprint = std::int64_t{42} << 20U};
    const platform::GpuInfo apple{.name = "Fake 9000", .unified_memory = true};
    CHECK(apogee::tui::monitor_parts(full, apple, {"root", "helper"}) ==
          std::vector<std::string>{"cpu 14%", "load 2.31 1.98 1.75", "mem 40.0 GiB / 64.0 GiB",
                                   "apogee 42 MiB · 2 held", "gpu Fake 9000, unified"});
    // The same numbers in `apogee system`'s table: one set of units.
    apogee::operations::SystemView view;
    view.machine.memory = full.memory;
    view.machine.load = full.load;
    view.machine.utilization = platform::Utilization{.percent = 14.4};
    view.machine.process_footprint = full.process_footprint;
    const std::string table = apogee::operations::system_table(view);
    CHECK(table.find("14% over") != std::string::npos);
    CHECK(table.find("2.31 1.98 1.75") != std::string::npos);
    CHECK(table.find("64.0 GiB total · 40.0 GiB used") != std::string::npos);
    CHECK(table.find("42 MiB (this process)") != std::string::npos);

    // Elsewhere: no GPU story told, none invented; a first reading has no
    // utilization yet; nothing read is nothing said.
    platform::MachineSample partial = full;
    partial.utilization.reset();
    partial.load.reset();
    CHECK(apogee::tui::monitor_parts(partial, platform::GpuInfo{.unknown = "not read"}, {}) ==
          std::vector<std::string>{"mem 40.0 GiB / 64.0 GiB", "apogee 42 MiB"});
    CHECK(apogee::tui::monitor_parts({}, {}, {}).empty());
}

TEST_CASE("the bar samples on the shell's tick, one sample a tick, while the shell is busy",
          "[tui][monitor]") {
    FakeMachine machine;
    Stage stage{machine, {"root"}};
    CHECK(stage.strip().starts_with(" reading the machine…"));
    CHECK(stage.strip().ends_with(" q quit\n"));
    stage.bar.start();
    stage.bar.settle();
    // The first reading has no utilization to measure; the next does.
    CHECK(stage.strip().starts_with(
        " load 1.50 1.25 1.00  ·  mem 40.0 GiB / 64.0 GiB  ·  apogee 42 MiB · 1 held  ·  "
        "gpu Fake 9000, unified "));
    stage.tick();
    CHECK(stage.strip().starts_with(" cpu 25%  ·  load 1.50"));
    for (int i = 0; i < 5; ++i) {
        // A turn streaming meanwhile: the shell busy with its own work.
        for (int j = 0; j < 50; ++j) {
            stage.pump.post([]() {});
        }
        stage.tick();
    }
    CHECK(machine.time_reads == 7);  // the first, and one a tick
}

TEST_CASE(
    "a stalled probe never holds a frame: the bar goes stale with its age, and recovers "
    "without a burst",
    "[tui][monitor]") {
    FakeMachine machine;
    Stage stage{machine};
    stage.bar.start();
    stage.bar.settle();
    stage.tick();
    REQUIRE(machine.time_reads == 2);

    machine.gated = true;
    stage.pump.advance(milliseconds{2000});
    (void)stage.pump.drain();  // the tick: a sample out, now stalled
    const auto deadline = std::chrono::steady_clock::now() + seconds{10};
    while (machine.waiting != 1) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        std::this_thread::sleep_for(milliseconds{1});
    }
    for (int i = 0; i < 4; ++i) {
        stage.pump.advance(milliseconds{2000});
        (void)stage.pump.drain();  // the shell draws on, and no sample piles up
        CHECK(machine.waiting == 1);
    }
    const std::string stalled = stage.strip();
    CHECK(stalled.find("cpu 25%") != std::string::npos);  // the last values stand
    CHECK(stalled.find("stale 10s") != std::string::npos);

    machine.release();
    stage.bar.settle();
    (void)stage.pump.drain();
    CHECK(machine.time_reads == 3);  // the one held back, and no catching up
    CHECK(stage.strip().find("stale") == std::string::npos);
    stage.tick();
    CHECK(machine.time_reads == 4);  // then one a tick again
}

TEST_CASE("a failed sample keeps the last values and says how old they are", "[tui][monitor]") {
    FakeMachine machine;
    Stage stage{machine};
    stage.bar.start();
    stage.bar.settle();
    stage.tick();
    machine.answers = false;
    stage.tick();
    const std::string failed = stage.strip();
    CHECK(failed.find("mem 40.0 GiB / 64.0 GiB") != std::string::npos);
    CHECK(failed.find("stale 2s") != std::string::npos);
    machine.answers = true;
    stage.tick();
    CHECK(stage.strip().find("stale") == std::string::npos);
}

TEST_CASE("a machine that cannot be read at all is said so, never guessed", "[tui][monitor]") {
    FakeMachine machine;
    machine.answers = false;
    Stage stage{machine};
    stage.bar.start();
    stage.bar.settle();
    CHECK(stage.strip().starts_with(" the machine could not be read "));
}

TEST_CASE("a manual pump's tick fires once however long the clock was away", "[tui][pump]") {
    apogee::tui::ManualPump pump;
    int ticks = 0;
    pump.every(milliseconds{2000}, [&ticks]() { ++ticks; });
    pump.advance(milliseconds{1999});
    CHECK(pump.drain() == 0);
    pump.advance(milliseconds{1});
    CHECK(pump.drain() == 1);
    pump.advance(seconds{30});
    CHECK(pump.drain() == 1);  // fifteen periods away: one tick, not fifteen
    pump.advance(milliseconds{2000});
    CHECK(pump.drain() == 1);
    CHECK(ticks == 3);
}
