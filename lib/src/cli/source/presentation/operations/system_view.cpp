#include "operations/system_view.h"

#include <array>
#include <cmath>
#include <cstdio>
#include <string>
#include <system_error>
#include <utility>

#include "platform/platform.h"

namespace apogee::operations {

namespace {

constexpr std::int64_t kKiB = 1024;
constexpr std::int64_t kMiB = kKiB * 1024;
constexpr std::int64_t kGiB = kMiB * 1024;
constexpr std::int64_t kTiB = kGiB * 1024;

/// The table's label column.
[[nodiscard]] std::string row(std::string_view label, const std::string& value) {
    std::string line{label};
    line += ':';
    line.append(label.size() < 7 ? 7 - label.size() : 1, ' ');
    return line + value + "\n";
}

[[nodiscard]] std::string one_decimal(double value) {
    std::array<char, 32> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.1f", value);
    return buffer.data();
}

[[nodiscard]] std::string cores_words(const platform::CpuInfo& cpu) {
    std::string words;
    if (cpu.physical_cores.has_value()) {
        words = std::to_string(*cpu.physical_cores) + " cores";
        if (cpu.performance_cores.has_value() && cpu.efficiency_cores.has_value()) {
            words += " (" + std::to_string(*cpu.performance_cores) + " performance + " +
                     std::to_string(*cpu.efficiency_cores) + " efficiency)";
        }
        if (cpu.logical_cores.has_value() && *cpu.logical_cores != *cpu.physical_cores) {
            words += ", " + std::to_string(*cpu.logical_cores) + " threads";
        }
        return words;
    }
    if (cpu.logical_cores.has_value()) {
        return std::to_string(*cpu.logical_cores) + " logical cores";
    }
    return "cores unknown";
}

[[nodiscard]] nlohmann::json optional_number(const std::optional<std::int64_t>& value) {
    return value.has_value() ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

[[nodiscard]] nlohmann::json optional_count(const std::optional<int>& value) {
    return value.has_value() ? nlohmann::json(*value) : nlohmann::json(nullptr);
}

[[nodiscard]] nlohmann::json optional_text(const std::string& value) {
    return value.empty() ? nlohmann::json(nullptr) : nlohmann::json(value);
}

/// A rounded value for the document: the precision shown, not the noise.
[[nodiscard]] double hundredths(double value) {
    return std::round(value * 100.0) / 100.0;
}

/// The nearest existing directory at or above `path`: the volume a store not
/// made yet would be on.
[[nodiscard]] std::filesystem::path existing_ancestor(std::filesystem::path path) {
    std::error_code code;
    while (!path.empty() && !std::filesystem::exists(path, code)) {
        const std::filesystem::path parent = path.parent_path();
        if (parent == path) {
            break;
        }
        path = parent;
    }
    return path;
}

}  // namespace

StoreDisk read_store_disk(const platform::SystemSource& source, const std::filesystem::path& root) {
    StoreDisk disk{.root = root};
    std::error_code code;
    if (!std::filesystem::exists(root, code)) {
        disk.bytes = 0;  // no store yet: nothing in it
        disk.volume = source.volume(existing_ancestor(root));
        return disk;
    }
    std::int64_t bytes = 0;
    std::filesystem::recursive_directory_iterator walk{
        root, std::filesystem::directory_options::skip_permission_denied, code};
    if (code) {
        disk.volume = source.volume(root);
        return disk;
    }
    for (const std::filesystem::recursive_directory_iterator end; walk != end;
         walk.increment(code)) {
        if (code) {
            break;
        }
        std::error_code entry_code;
        if (walk->is_regular_file(entry_code) && !walk->is_symlink(entry_code)) {
            const std::uintmax_t size = walk->file_size(entry_code);
            if (!entry_code) {
                bytes += static_cast<std::int64_t>(size);
            }
        }
    }
    if (!code) {
        disk.bytes = bytes;
    }
    disk.volume = source.volume(root);
    return disk;
}

std::string format_bytes(std::int64_t bytes) {
    if (bytes < kKiB) {
        return std::to_string(bytes) + " B";
    }
    if (bytes < kMiB) {
        return std::to_string((bytes + (kKiB / 2)) / kKiB) + " KiB";
    }
    if (bytes < kGiB) {
        return std::to_string((bytes + (kMiB / 2)) / kMiB) + " MiB";
    }
    if (bytes < kTiB) {
        return one_decimal(static_cast<double>(bytes) / static_cast<double>(kGiB)) + " GiB";
    }
    return one_decimal(static_cast<double>(bytes) / static_cast<double>(kTiB)) + " TiB";
}

std::string format_percent(double percent) {
    return std::to_string(static_cast<int>(std::lround(percent))) + "%";
}

std::string format_load(const platform::LoadAverage& load) {
    std::array<char, 64> buffer{};
    std::snprintf(buffer.data(), buffer.size(), "%.2f %.2f %.2f", load.one_minute,
                  load.five_minutes, load.fifteen_minutes);
    return buffer.data();
}

std::string cpu_words(const platform::CpuInfo& cpu) {
    return cpu.model.value_or("unknown model") + " · " +
           std::string{platform::to_string(platform::host_architecture())} + " · " +
           cores_words(cpu);
}

std::string system_table(const SystemView& view) {
    const platform::MachineSnapshot& machine = view.machine;
    std::string out;
    out += row("cpu", cpu_words(machine.cpu));
    out += row("load", machine.load.has_value()
                           ? format_load(*machine.load) + " (1, 5, 15 min)"
                           : "unknown" + (machine.load_unknown.empty()
                                              ? std::string{}
                                              : " (" + machine.load_unknown + ")"));
    out += row("usage", machine.utilization.has_value()
                            ? format_percent(machine.utilization->percent) + " over " +
                                  std::to_string(machine.utilization->window.count()) + " ms"
                            : "unknown");

    const auto known = [](const std::optional<std::int64_t>& bytes) {
        return bytes.has_value() ? format_bytes(*bytes) : std::string{"unknown"};
    };
    out += row("memory", known(machine.memory.total) + " total · " + known(machine.memory.used()) +
                             " used · " + known(machine.memory.available) + " available");

    std::string apogee = known(machine.process_footprint) + " (this process) · models held: ";
    if (view.models_held.empty()) {
        apogee += "none by this process -- a session holds its own; /suite in one names them";
    } else {
        for (std::size_t i = 0; i < view.models_held.size(); ++i) {
            apogee += (i == 0 ? "" : ", ") + view.models_held[i];
        }
    }
    out += row("apogee", apogee);

    const platform::GpuInfo& gpu = machine.gpu;
    std::string gpu_line;
    if (gpu.name.has_value()) {
        gpu_line =
            *gpu.name + (gpu.unified_memory ? " (integrated, sharing the system's memory)" : "");
    } else {
        gpu_line = "unknown" + (gpu.unknown.empty() ? std::string{} : " (" + gpu.unknown + ")");
    }
    gpu_line += " · for models: ";
    if (view.model_budget.has_value()) {
        gpu_line += format_bytes(*view.model_budget) +
                    (gpu.unified_memory ? " of it (Metal's working set)" : "");
    } else {
        gpu_line += "unknown" + (view.model_budget_unknown.empty()
                                     ? std::string{}
                                     : " (" + view.model_budget_unknown + ")");
    }
    out += row("gpu", gpu_line);

    const StoreDisk& store = view.store;
    out += row("disk", "models " + known(store.bytes) + " in " + store.root.string() + " · " +
                           known(store.volume.available) + " available of " +
                           known(store.volume.capacity) + " on its volume");
    return out;
}

nlohmann::json system_document(const SystemView& view) {
    const platform::MachineSnapshot& machine = view.machine;
    nlohmann::json cpu = {
        {"model", machine.cpu.model.has_value() ? nlohmann::json(*machine.cpu.model) : nullptr},
        {"architecture", std::string{platform::to_string(platform::host_architecture())}},
        {"physical_cores", optional_count(machine.cpu.physical_cores)},
        {"logical_cores", optional_count(machine.cpu.logical_cores)},
        {"performance_cores", optional_count(machine.cpu.performance_cores)},
        {"efficiency_cores", optional_count(machine.cpu.efficiency_cores)},
        {"load", nullptr},
        {"load_unknown", optional_text(machine.load_unknown)},
        {"utilization", nullptr},
    };
    if (machine.load.has_value()) {
        cpu["load"] = {{"one_minute", hundredths(machine.load->one_minute)},
                       {"five_minutes", hundredths(machine.load->five_minutes)},
                       {"fifteen_minutes", hundredths(machine.load->fifteen_minutes)}};
    }
    if (machine.utilization.has_value()) {
        cpu["utilization"] = {{"percent", hundredths(machine.utilization->percent)},
                              {"window_ms", machine.utilization->window.count()}};
    }
    return {
        {"cpu", std::move(cpu)},
        {"memory",
         {{"total_bytes", optional_number(machine.memory.total)},
          {"used_bytes", optional_number(machine.memory.used())},
          {"available_bytes", optional_number(machine.memory.available)}}},
        {"apogee",
         {{"process_bytes", optional_number(machine.process_footprint)},
          {"models_held", view.models_held}}},
        {"gpu",
         {{"name", machine.gpu.name.has_value() ? nlohmann::json(*machine.gpu.name) : nullptr},
          {"unified_memory", machine.gpu.unified_memory},
          {"unknown", optional_text(machine.gpu.unknown)},
          {"model_budget_bytes", optional_number(view.model_budget)},
          {"model_budget_unknown", optional_text(view.model_budget_unknown)}}},
        {"disk",
         {{"store", view.store.root.string()},
          {"store_bytes", optional_number(view.store.bytes)},
          {"volume_capacity_bytes", optional_number(view.store.volume.capacity)},
          {"volume_available_bytes", optional_number(view.store.volume.available)}}},
    };
}

}  // namespace apogee::operations
