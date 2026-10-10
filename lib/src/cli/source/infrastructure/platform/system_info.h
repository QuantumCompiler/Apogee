#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>

/// The machine, read (32a): CPU, memory, the GPU's story, a volume's space,
/// and this process's own footprint -- one function per OS read, behind the
/// one seam a test fakes.
///
/// **Unknown is said, never guessed.** Every reading is optional: a platform
/// that cannot answer leaves it empty, and whatever renders it says
/// `unknown`. Nothing here spawns a child (`system_profiler`, `nvidia-smi`),
/// loads a model or touches the network -- an OS call or a `/proc` file each.
///
/// The memory a model offloads to is NOT read here: that is the devices'
/// memory llama.cpp reports, which 26a's window sizing and 27e's suite
/// admission read through `backends::offload_memory_total`, and which a
/// surface showing this snapshot takes from that same function beside it.
/// What lives here is what nothing else reads: the system's own memory, the
/// CPU, the volume, the process.
namespace apogee::platform {

/// What the CPU has done since boot, in the OS's own ticks: two readings a
/// moment apart say how busy it was in between (`utilization`).
struct CpuTimes {
    std::uint64_t busy = 0;
    std::uint64_t total = 0;
};

struct CpuInfo {
    /// As the OS names it: `Apple M3 Max`, `AMD Ryzen 9 7950X 16-Core
    /// Processor`.
    std::optional<std::string> model;
    std::optional<int> physical_cores;
    std::optional<int> logical_cores;
    /// The two kinds of core a hybrid CPU has, where the OS says cheaply
    /// (Apple silicon's performance levels); empty on a CPU with one kind,
    /// or where nothing says.
    std::optional<int> performance_cores;
    std::optional<int> efficiency_cores;
};

/// The run queue averaged over one, five and fifteen minutes (POSIX's
/// `getloadavg`).
struct LoadAverage {
    double one_minute = 0;
    double five_minutes = 0;
    double fifteen_minutes = 0;
};

/// The system's memory, as the OS counts it. `available` is what can be had
/// without swapping -- Linux's `MemAvailable`, Windows's available physical
/// memory, macOS's free and inactive pages -- not the narrower "free", which
/// on every one of them leaves out the cache the OS gives back on demand.
struct MemoryInfo {
    std::optional<std::int64_t> total;
    std::optional<std::int64_t> available;

    /// `total - available`, when both are known.
    [[nodiscard]] std::optional<std::int64_t> used() const noexcept;
};

/// What the platform can say about a GPU without asking a vendor's tool.
struct GpuInfo {
    /// The device, where the OS names it: Apple silicon's GPU is the chip's
    /// own, named by the chip.
    std::optional<std::string> name;
    /// It shares the machine's memory with the CPU (Apple silicon), so its
    /// memory is the system's -- the model budget beside it says how much.
    bool unified_memory = false;
    /// Why `name` is unknown, for a person; empty when it is not.
    std::string unknown;
};

/// A volume's space, as `std::filesystem::space` reads it: its size, and
/// what an ordinary process may still write.
struct VolumeInfo {
    std::optional<std::int64_t> capacity;
    std::optional<std::int64_t> available;
};

/// The OS reads, one each -- what `host_system` answers and a test fakes.
class SystemSource {
public:
    SystemSource() = default;
    SystemSource(const SystemSource&) = delete;
    SystemSource& operator=(const SystemSource&) = delete;
    SystemSource(SystemSource&&) = delete;
    SystemSource& operator=(SystemSource&&) = delete;
    virtual ~SystemSource() = default;

    [[nodiscard]] virtual CpuInfo cpu() const = 0;
    [[nodiscard]] virtual std::optional<CpuTimes> cpu_times() const = 0;
    [[nodiscard]] virtual std::optional<LoadAverage> load_average() const = 0;
    /// Why `load_average` is empty on this platform, for a person: Windows
    /// keeps no load average. Empty where one is kept.
    [[nodiscard]] virtual std::string load_unknown() const = 0;
    [[nodiscard]] virtual MemoryInfo memory() const = 0;
    /// This process's own memory: what the OS charges it -- its physical
    /// footprint on macOS, resident set on Linux, working set on Windows.
    [[nodiscard]] virtual std::optional<std::int64_t> process_footprint() const = 0;
    [[nodiscard]] virtual GpuInfo gpu() const = 0;
    [[nodiscard]] virtual VolumeInfo volume(const std::filesystem::path& path) const = 0;
};

/// This machine's OS, read. One for the process; it holds no state.
[[nodiscard]] const SystemSource& host_system();

/// How busy the CPU was between two readings, 0 to 100 percent; empty when
/// no time passed between them or the counters went backwards.
[[nodiscard]] std::optional<double> utilization(const CpuTimes& before, const CpuTimes& after);

/// The CPU's utilization and the window it was measured over.
struct Utilization {
    double percent = 0;
    std::chrono::milliseconds window{0};
};

/// One reading of the machine.
struct MachineSnapshot {
    CpuInfo cpu;
    std::optional<LoadAverage> load;
    std::string load_unknown;
    std::optional<Utilization> utilization;
    MemoryInfo memory;
    std::optional<std::int64_t> process_footprint;
    GpuInfo gpu;
};

/// The window `apogee system` measures utilization over: long enough to
/// mean something, short enough that a one-shot command needs no busy line.
inline constexpr std::chrono::milliseconds kUtilizationWindow{500};

/// Waits out a sampling window; the real one sleeps, a test's returns.
using Wait = std::function<void(std::chrono::milliseconds)>;

/// Everything `source` can say, once: the CPU's utilization read twice
/// `window` apart through `wait` (the thread sleeping when it is null), the
/// rest read once. A zero window measures no utilization.
[[nodiscard]] MachineSnapshot read_machine(const SystemSource& source,
                                           std::chrono::milliseconds window, const Wait& wait = {});

}  // namespace apogee::platform
