#include "platform/system_info.h"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <fstream>
#include <set>
#include <sstream>
#include <string>
#include <system_error>
#include <thread>
#include <utility>

#if defined(_WIN32)
#include <windows.h>
// After windows.h: version 2 maps GetProcessMemoryInfo to kernel32's
// K32GetProcessMemoryInfo, so nothing links psapi.
#define PSAPI_VERSION 2
#include <psapi.h>

#include <vector>
#elif defined(__APPLE__)
#include <mach/mach.h>
#include <sys/sysctl.h>
#include <sys/types.h>
#else
#include <unistd.h>
#endif

// The machine's numbers, one OS read each. Like platform.cpp, this file is a
// home of platform `#if`s: everything above asks `SystemSource`.

namespace apogee::platform {

std::optional<std::int64_t> MemoryInfo::used() const noexcept {
    if (!total.has_value() || !available.has_value()) {
        return std::nullopt;
    }
    return std::max<std::int64_t>(0, *total - *available);
}

std::optional<double> utilization(const CpuTimes& before, const CpuTimes& after) {
    if (after.total <= before.total || after.busy < before.busy) {
        return std::nullopt;
    }
    const auto busy = static_cast<double>(after.busy - before.busy);
    const auto total = static_cast<double>(after.total - before.total);
    return std::clamp(100.0 * busy / total, 0.0, 100.0);
}

MachineSnapshot read_machine(const SystemSource& source, std::chrono::milliseconds window,
                             const Wait& wait) {
    MachineSnapshot snapshot;
    // The utilization first: its window is the only wait, and every other
    // read is then of the machine as it stands at the end of it.
    if (window.count() > 0) {
        if (const std::optional<CpuTimes> before = source.cpu_times(); before.has_value()) {
            if (wait) {
                wait(window);
            } else {
                std::this_thread::sleep_for(window);
            }
            if (const std::optional<CpuTimes> after = source.cpu_times(); after.has_value()) {
                if (const std::optional<double> percent = utilization(*before, *after);
                    percent.has_value()) {
                    snapshot.utilization = Utilization{.percent = *percent, .window = window};
                }
            }
        }
    }
    snapshot.cpu = source.cpu();
    snapshot.load = source.load_average();
    if (!snapshot.load.has_value()) {
        snapshot.load_unknown = source.load_unknown();
    }
    snapshot.memory = source.memory();
    snapshot.process_footprint = source.process_footprint();
    snapshot.gpu = source.gpu();
    return snapshot;
}

namespace {

/// The GPU's story where no cheap read exists: said, never guessed.
[[maybe_unused]] constexpr std::string_view kGpuNotRead = "not read on this platform yet";

/// A GPU whose name nothing read, and why.
[[maybe_unused]] [[nodiscard]] GpuInfo unknown_gpu(std::string reason,
                                                   bool unified_memory = false) {
    GpuInfo gpu;
    gpu.unified_memory = unified_memory;
    gpu.unknown = std::move(reason);
    return gpu;
}

[[nodiscard]] VolumeInfo volume_of(const std::filesystem::path& path) {
    std::error_code code;
    const std::filesystem::space_info space = std::filesystem::space(path, code);
    if (code) {
        return {};
    }
    // An unknown size reads as all bits set.
    constexpr auto kUnknown = static_cast<std::uintmax_t>(-1);
    VolumeInfo volume;
    if (space.capacity != kUnknown) {
        volume.capacity = static_cast<std::int64_t>(space.capacity);
    }
    if (space.available != kUnknown) {
        volume.available = static_cast<std::int64_t>(space.available);
    }
    return volume;
}

#if !defined(_WIN32) && !defined(__APPLE__)

/// The first value of `key:` in a `/proc` file of `key: value` lines.
[[nodiscard]] std::optional<std::string> proc_field(const char* file, std::string_view key) {
    std::ifstream in{file};
    std::string line;
    while (std::getline(in, line)) {
        if (line.size() > key.size() && line.compare(0, key.size(), key) == 0) {
            const std::size_t colon = line.find(':', key.size());
            if (colon == std::string::npos) {
                continue;
            }
            // Only the key itself before the colon, so `cpu MHz` never reads
            // as `cpu`.
            if (line.find_first_not_of(" \t", key.size()) != colon) {
                continue;
            }
            const std::size_t start = line.find_first_not_of(" \t", colon + 1);
            return start == std::string::npos ? std::string{} : line.substr(start);
        }
    }
    return std::nullopt;
}

/// A `/proc/meminfo`-style `<n> kB` value, in bytes.
[[nodiscard]] std::optional<std::int64_t> kilobytes_field(const char* file, std::string_view key) {
    const std::optional<std::string> value = proc_field(file, key);
    if (!value.has_value()) {
        return std::nullopt;
    }
    std::istringstream in{*value};
    std::int64_t kilobytes = 0;
    if (!(in >> kilobytes) || kilobytes < 0) {
        return std::nullopt;
    }
    return kilobytes * 1024;
}

#endif

#if defined(__APPLE__)

[[nodiscard]] std::optional<std::string> sysctl_string(const char* name) {
    std::size_t size = 0;
    if (sysctlbyname(name, nullptr, &size, nullptr, 0) != 0 || size == 0) {
        return std::nullopt;
    }
    std::string value(size, '\0');
    if (sysctlbyname(name, value.data(), &size, nullptr, 0) != 0) {
        return std::nullopt;
    }
    value.resize(std::min(size, value.find('\0')));
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
}

template <typename T>
[[nodiscard]] std::optional<T> sysctl_value(const char* name) {
    T value{};
    std::size_t size = sizeof(value);
    if (sysctlbyname(name, &value, &size, nullptr, 0) != 0 || size != sizeof(value)) {
        return std::nullopt;
    }
    return value;
}

[[nodiscard]] std::optional<int> sysctl_count(const char* name) {
    const std::optional<std::int32_t> value = sysctl_value<std::int32_t>(name);
    if (!value.has_value() || *value <= 0) {
        return std::nullopt;
    }
    return static_cast<int>(*value);
}

#endif

class HostSystem final : public SystemSource {
public:
    [[nodiscard]] CpuInfo cpu() const override;
    [[nodiscard]] std::optional<CpuTimes> cpu_times() const override;
    [[nodiscard]] std::optional<LoadAverage> load_average() const override;
    [[nodiscard]] std::string load_unknown() const override;
    [[nodiscard]] MemoryInfo memory() const override;
    [[nodiscard]] std::optional<std::int64_t> process_footprint() const override;
    [[nodiscard]] GpuInfo gpu() const override;

    [[nodiscard]] VolumeInfo volume(const std::filesystem::path& path) const override {
        return volume_of(path);
    }
};

#if defined(_WIN32)

[[nodiscard]] std::uint64_t ticks(const FILETIME& time) {
    return (static_cast<std::uint64_t>(time.dwHighDateTime) << 32U) | time.dwLowDateTime;
}

CpuInfo HostSystem::cpu() const {
    CpuInfo info;
    std::string name(256, '\0');
    auto size = static_cast<DWORD>(name.size());
    if (RegGetValueA(HKEY_LOCAL_MACHINE, R"(HARDWARE\DESCRIPTION\System\CentralProcessor\0)",
                     "ProcessorNameString", RRF_RT_REG_SZ, nullptr, name.data(),
                     &size) == ERROR_SUCCESS) {
        name.resize(std::min<std::size_t>(size, name.find('\0')));
        // The registry pads some names with trailing spaces.
        name.erase(name.find_last_not_of(' ') + 1);
        if (!name.empty()) {
            info.model = std::move(name);
        }
    }
    SYSTEM_INFO system{};
    GetSystemInfo(&system);
    if (system.dwNumberOfProcessors > 0) {
        info.logical_cores = static_cast<int>(system.dwNumberOfProcessors);
    }
    DWORD length = 0;
    GetLogicalProcessorInformation(nullptr, &length);
    if (length > 0) {
        std::vector<SYSTEM_LOGICAL_PROCESSOR_INFORMATION> entries(
            length / sizeof(SYSTEM_LOGICAL_PROCESSOR_INFORMATION));
        if (GetLogicalProcessorInformation(entries.data(), &length) != 0) {
            const auto cores = std::ranges::count_if(entries, [](const auto& entry) {
                return entry.Relationship == RelationProcessorCore;
            });
            if (cores > 0) {
                info.physical_cores = static_cast<int>(cores);
            }
        }
    }
    return info;
}

std::optional<CpuTimes> HostSystem::cpu_times() const {
    FILETIME idle{};
    FILETIME kernel{};
    FILETIME user{};
    if (GetSystemTimes(&idle, &kernel, &user) == 0) {
        return std::nullopt;
    }
    // Kernel time includes the idle time.
    const std::uint64_t total = ticks(kernel) + ticks(user);
    const std::uint64_t idle_ticks = ticks(idle);
    return CpuTimes{.busy = total > idle_ticks ? total - idle_ticks : 0, .total = total};
}

std::optional<LoadAverage> HostSystem::load_average() const {
    return std::nullopt;
}

std::string HostSystem::load_unknown() const {
    return "Windows keeps no load average";
}

MemoryInfo HostSystem::memory() const {
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof(status);
    if (GlobalMemoryStatusEx(&status) == 0) {
        return {};
    }
    return MemoryInfo{.total = static_cast<std::int64_t>(status.ullTotalPhys),
                      .available = static_cast<std::int64_t>(status.ullAvailPhys)};
}

std::optional<std::int64_t> HostSystem::process_footprint() const {
    PROCESS_MEMORY_COUNTERS counters{};
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)) == 0) {
        return std::nullopt;
    }
    return static_cast<std::int64_t>(counters.WorkingSetSize);
}

GpuInfo HostSystem::gpu() const {
    return unknown_gpu(std::string{kGpuNotRead});
}

#elif defined(__APPLE__)

CpuInfo HostSystem::cpu() const {
    CpuInfo info;
    info.model = sysctl_string("machdep.cpu.brand_string");
    info.physical_cores = sysctl_count("hw.physicalcpu");
    info.logical_cores = sysctl_count("hw.logicalcpu");
    // Apple silicon's two kinds of core: level 0 is performance, level 1
    // efficiency. One level means one kind, and nothing is split.
    if (sysctl_count("hw.nperflevels").value_or(0) == 2) {
        info.performance_cores = sysctl_count("hw.perflevel0.physicalcpu");
        info.efficiency_cores = sysctl_count("hw.perflevel1.physicalcpu");
    }
    return info;
}

std::optional<CpuTimes> HostSystem::cpu_times() const {
    host_cpu_load_info_data_t load{};
    mach_msg_type_number_t count = HOST_CPU_LOAD_INFO_COUNT;
    // The host_info_t is an int array the call fills; the struct is that array.
    if (host_statistics(mach_host_self(), HOST_CPU_LOAD_INFO,
                        reinterpret_cast<host_info_t>(&load),  // NOLINT
                        &count) != KERN_SUCCESS) {
        return std::nullopt;
    }
    const std::uint64_t busy = std::uint64_t{load.cpu_ticks[CPU_STATE_USER]} +
                               load.cpu_ticks[CPU_STATE_SYSTEM] + load.cpu_ticks[CPU_STATE_NICE];
    return CpuTimes{.busy = busy, .total = busy + load.cpu_ticks[CPU_STATE_IDLE]};
}

std::optional<LoadAverage> HostSystem::load_average() const {
    std::array<double, 3> averages{};
    if (getloadavg(averages.data(), 3) != 3) {
        return std::nullopt;
    }
    return LoadAverage{
        .one_minute = averages[0], .five_minutes = averages[1], .fifteen_minutes = averages[2]};
}

std::string HostSystem::load_unknown() const {
    return {};
}

MemoryInfo HostSystem::memory() const {
    MemoryInfo info;
    if (const std::optional<std::uint64_t> total = sysctl_value<std::uint64_t>("hw.memsize");
        total.has_value() && *total > 0) {
        info.total = static_cast<std::int64_t>(*total);
    }
    vm_statistics64_data_t vm{};
    mach_msg_type_number_t count = HOST_VM_INFO64_COUNT;
    vm_size_t page = 0;
    if (host_page_size(mach_host_self(), &page) == KERN_SUCCESS &&
        host_statistics64(mach_host_self(), HOST_VM_INFO64,
                          reinterpret_cast<host_info64_t>(&vm),  // NOLINT
                          &count) == KERN_SUCCESS) {
        // Free pages (the speculative ones among them) and inactive ones:
        // what the kernel hands out before it has to compress or swap.
        const auto available =
            static_cast<std::int64_t>((std::uint64_t{vm.free_count} + vm.inactive_count) * page);
        info.available = info.total.has_value() ? std::min(available, *info.total) : available;
    }
    return info;
}

std::optional<std::int64_t> HostSystem::process_footprint() const {
    task_vm_info_data_t vm{};
    mach_msg_type_number_t count = TASK_VM_INFO_COUNT;
    if (task_info(mach_task_self(), TASK_VM_INFO,
                  reinterpret_cast<task_info_t>(&vm),  // NOLINT
                  &count) != KERN_SUCCESS) {
        return std::nullopt;
    }
    // What Activity Monitor calls the process's memory.
    return static_cast<std::int64_t>(vm.phys_footprint);
}

GpuInfo HostSystem::gpu() const {
#if defined(__aarch64__)
    // Apple silicon: the GPU is the chip's own and shares its memory.
    if (std::optional<std::string> chip = sysctl_string("machdep.cpu.brand_string");
        chip.has_value()) {
        return GpuInfo{.name = std::move(chip), .unified_memory = true};
    }
    return unknown_gpu("the chip did not name itself", true);
#else
    return unknown_gpu(std::string{kGpuNotRead});
#endif
}

#else  // Linux

CpuInfo HostSystem::cpu() const {
    CpuInfo info;
    // x86 names its model; most ARM kernels name none, and none is said.
    if (std::optional<std::string> model = proc_field("/proc/cpuinfo", "model name");
        model.has_value() && !model->empty()) {
        info.model = std::move(*model);
    }
    if (const long online = sysconf(_SC_NPROCESSORS_ONLN); online > 0) {
        info.logical_cores = static_cast<int>(online);
    }
    // Physical cores: the distinct (package, core) pairs, where the kernel
    // lists them -- x86 does; most ARM kernels do not, and that is unknown.
    std::ifstream in{"/proc/cpuinfo"};
    std::set<std::pair<std::string, std::string> > cores;
    std::string package;
    std::string line;
    while (std::getline(in, line)) {
        const auto value = [&line]() {
            const std::size_t colon = line.find(':');
            const std::size_t start = colon == std::string::npos
                                          ? std::string::npos
                                          : line.find_first_not_of(' ', colon + 1);
            return start == std::string::npos ? std::string{} : line.substr(start);
        };
        if (line.rfind("physical id", 0) == 0) {
            package = value();
        } else if (line.rfind("core id", 0) == 0) {
            cores.emplace(package, value());
        }
    }
    if (!cores.empty()) {
        info.physical_cores = static_cast<int>(cores.size());
    }
    return info;
}

std::optional<CpuTimes> HostSystem::cpu_times() const {
    std::ifstream in{"/proc/stat"};
    std::string label;
    if (!(in >> label) || label != "cpu") {
        return std::nullopt;
    }
    // user nice system idle iowait irq softirq steal: the rest (guest time)
    // is already counted in user and nice.
    std::array<std::uint64_t, 8> fields{};
    for (std::uint64_t& field : fields) {
        if (!(in >> field)) {
            return std::nullopt;
        }
    }
    const std::uint64_t idle = fields[3] + fields[4];
    const std::uint64_t busy =
        fields[0] + fields[1] + fields[2] + fields[5] + fields[6] + fields[7];
    return CpuTimes{.busy = busy, .total = busy + idle};
}

std::optional<LoadAverage> HostSystem::load_average() const {
    std::array<double, 3> averages{};
    if (getloadavg(averages.data(), 3) != 3) {
        return std::nullopt;
    }
    return LoadAverage{
        .one_minute = averages[0], .five_minutes = averages[1], .fifteen_minutes = averages[2]};
}

std::string HostSystem::load_unknown() const {
    return {};
}

MemoryInfo HostSystem::memory() const {
    return MemoryInfo{.total = kilobytes_field("/proc/meminfo", "MemTotal"),
                      .available = kilobytes_field("/proc/meminfo", "MemAvailable")};
}

std::optional<std::int64_t> HostSystem::process_footprint() const {
    return kilobytes_field("/proc/self/status", "VmRSS");
}

GpuInfo HostSystem::gpu() const {
    return unknown_gpu(std::string{kGpuNotRead});
}

#endif

}  // namespace

const SystemSource& host_system() {
    static const HostSystem kHost;
    return kHost;
}

}  // namespace apogee::platform
