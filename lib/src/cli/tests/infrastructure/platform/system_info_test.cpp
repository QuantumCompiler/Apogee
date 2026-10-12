#include "platform/system_info.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <chrono>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "platform/platform.h"

/// The machine's read (32a): the utilization arithmetic, the snapshot's
/// composition over a fake OS -- every reading known and every one unknown --
/// and a smoke row against this host's real one.
namespace {

using apogee::platform::CpuInfo;
using apogee::platform::CpuTimes;
using apogee::platform::GpuInfo;
using apogee::platform::LoadAverage;
using apogee::platform::MachineSnapshot;
using apogee::platform::MemoryInfo;
using apogee::platform::SystemSource;
using apogee::platform::VolumeInfo;
using Catch::Matchers::WithinAbs;

/// An OS that answers what it is told, counting the CPU-time reads.
class FakeSystem final : public SystemSource {
public:
    CpuInfo cpu_info;
    std::vector<std::optional<CpuTimes>> times;
    std::optional<LoadAverage> load;
    std::string load_reason;
    MemoryInfo memory_info;
    std::optional<std::int64_t> footprint;
    GpuInfo gpu_info;
    mutable int time_reads = 0;

    [[nodiscard]] CpuInfo cpu() const override {
        return cpu_info;
    }

    [[nodiscard]] std::optional<CpuTimes> cpu_times() const override {
        const auto index = static_cast<std::size_t>(time_reads++);
        return index < times.size() ? times[index] : std::nullopt;
    }

    [[nodiscard]] std::optional<LoadAverage> load_average() const override {
        return load;
    }

    [[nodiscard]] std::string load_unknown() const override {
        return load_reason;
    }

    [[nodiscard]] MemoryInfo memory() const override {
        return memory_info;
    }

    [[nodiscard]] std::optional<std::int64_t> process_footprint() const override {
        return footprint;
    }

    [[nodiscard]] GpuInfo gpu() const override {
        return gpu_info;
    }

    [[nodiscard]] VolumeInfo volume(const std::filesystem::path& /*path*/) const override {
        return {};
    }
};

}  // namespace

TEST_CASE("utilization is the busy share of the time between two readings", "[platform][system]") {
    using apogee::platform::utilization;
    REQUIRE(utilization({.busy = 100, .total = 400}, {.busy = 150, .total = 600}).has_value());
    CHECK_THAT(*utilization({.busy = 100, .total = 400}, {.busy = 150, .total = 600}),
               WithinAbs(25.0, 1e-9));
    CHECK_THAT(*utilization({.busy = 0, .total = 0}, {.busy = 10, .total = 10}),
               WithinAbs(100.0, 1e-9));
    // No time between them, or counters that went backwards: nothing to say.
    CHECK_FALSE(utilization({.busy = 5, .total = 10}, {.busy = 5, .total = 10}).has_value());
    CHECK_FALSE(utilization({.busy = 5, .total = 10}, {.busy = 4, .total = 20}).has_value());
    CHECK_FALSE(utilization({.busy = 5, .total = 10}, {.busy = 6, .total = 9}).has_value());
}

TEST_CASE("used memory is what is not available, and unknown when either is",
          "[platform][system]") {
    CHECK(MemoryInfo{.total = 100, .available = 30}.used() == 70);
    CHECK(MemoryInfo{.total = 100, .available = 130}.used() == 0);
    CHECK_FALSE(MemoryInfo{.total = 100}.used().has_value());
    CHECK_FALSE(MemoryInfo{.available = 100}.used().has_value());
}

TEST_CASE("a snapshot reads every answer once and measures over the window it names",
          "[platform][system]") {
    FakeSystem os;
    os.cpu_info = CpuInfo{.model = "Fake 9000",
                          .physical_cores = 8,
                          .logical_cores = 16,
                          .performance_cores = 6,
                          .efficiency_cores = 2};
    os.times = {CpuTimes{.busy = 1000, .total = 4000}, CpuTimes{.busy = 1300, .total = 5000}};
    os.load = LoadAverage{.one_minute = 1.5, .five_minutes = 1.25, .fifteen_minutes = 1.0};
    os.memory_info = MemoryInfo{.total = 64, .available = 16};
    os.footprint = 42;
    os.gpu_info = GpuInfo{.name = "Fake GPU", .unified_memory = true};

    std::vector<std::chrono::milliseconds> waited;
    const MachineSnapshot snapshot = apogee::platform::read_machine(
        os, std::chrono::milliseconds{500},
        [&waited](std::chrono::milliseconds window) { waited.push_back(window); });

    REQUIRE(waited == std::vector<std::chrono::milliseconds>{std::chrono::milliseconds{500}});
    CHECK(os.time_reads == 2);
    REQUIRE(snapshot.utilization.has_value());
    CHECK_THAT(snapshot.utilization->percent, WithinAbs(30.0, 1e-9));
    CHECK(snapshot.utilization->window == std::chrono::milliseconds{500});
    CHECK(snapshot.cpu.model == "Fake 9000");
    CHECK(snapshot.cpu.performance_cores == 6);
    REQUIRE(snapshot.load.has_value());
    CHECK(snapshot.load->five_minutes == 1.25);
    CHECK(snapshot.load_unknown.empty());
    CHECK(snapshot.memory.used() == 48);
    CHECK(snapshot.process_footprint == 42);
    CHECK(snapshot.gpu.name == "Fake GPU");
}

TEST_CASE("an OS that answers nothing yields a snapshot of unknowns, with its reasons",
          "[platform][system]") {
    FakeSystem os;
    os.load_reason = "this OS keeps none";
    os.gpu_info = GpuInfo{.unknown = "not read on this platform yet"};
    bool waited = false;
    const MachineSnapshot snapshot = apogee::platform::read_machine(
        os, std::chrono::milliseconds{500},
        [&waited](std::chrono::milliseconds /*window*/) { waited = true; });

    // No first reading: nothing to wait for.
    CHECK_FALSE(waited);
    CHECK_FALSE(snapshot.utilization.has_value());
    CHECK_FALSE(snapshot.cpu.model.has_value());
    CHECK_FALSE(snapshot.cpu.logical_cores.has_value());
    CHECK_FALSE(snapshot.load.has_value());
    CHECK(snapshot.load_unknown == "this OS keeps none");
    CHECK_FALSE(snapshot.memory.total.has_value());
    CHECK_FALSE(snapshot.process_footprint.has_value());
    CHECK_FALSE(snapshot.gpu.name.has_value());
    CHECK(snapshot.gpu.unknown == "not read on this platform yet");
}

TEST_CASE("a zero window measures no utilization and waits for nothing", "[platform][system]") {
    FakeSystem os;
    os.times = {CpuTimes{.busy = 1, .total = 2}, CpuTimes{.busy = 2, .total = 4}};
    bool waited = false;
    const MachineSnapshot snapshot = apogee::platform::read_machine(
        os, std::chrono::milliseconds{0}, [&waited](std::chrono::milliseconds) { waited = true; });
    CHECK_FALSE(waited);
    CHECK(os.time_reads == 0);
    CHECK_FALSE(snapshot.utilization.has_value());
}

TEST_CASE("this host's machine reads as a machine", "[platform][system][smoke]") {
    const SystemSource& host = apogee::platform::host_system();

    const MemoryInfo memory = host.memory();
    REQUIRE(memory.total.has_value());
    CHECK(*memory.total > 0);
    if (memory.available.has_value()) {
        CHECK(*memory.available >= 0);
        CHECK(*memory.available <= *memory.total);
        REQUIRE(memory.used().has_value());
        CHECK(*memory.used() <= *memory.total);
    }

    const CpuInfo cpu = host.cpu();
    REQUIRE(cpu.logical_cores.has_value());
    CHECK(*cpu.logical_cores > 0);

    const std::optional<CpuTimes> times = host.cpu_times();
    REQUIRE(times.has_value());
    CHECK(times->total >= times->busy);

    const std::optional<std::int64_t> footprint = host.process_footprint();
    REQUIRE(footprint.has_value());
    CHECK(*footprint > 0);

    const VolumeInfo volume = host.volume(std::filesystem::temp_directory_path());
    REQUIRE(volume.capacity.has_value());
    CHECK(*volume.capacity > 0);

    // A load average where the OS keeps one, and a reason where it does not.
    if (apogee::platform::host_os() == apogee::platform::OperatingSystem::Windows) {
        CHECK_FALSE(host.load_average().has_value());
        CHECK_FALSE(host.load_unknown().empty());
    } else {
        CHECK(host.load_average().has_value());
        CHECK(host.load_unknown().empty());
    }
}
