#include "operations/system_view.h"

#include <catch2/catch_test_macros.hpp>
#include <nlohmann/json.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <string>

#include "platform/platform.h"
#include "support/env_guard.h"

/// `apogee system`'s view (32a): the units every surface shares, the table
/// and the document over a view with every reading known and over one with
/// none -- every row and every field still there -- and the store's walk.
namespace {

using apogee::operations::StoreDisk;
using apogee::operations::SystemView;
namespace platform = apogee::platform;

[[nodiscard]] SystemView full_view() {
    SystemView view;
    view.machine.cpu = platform::CpuInfo{.model = "Fake 9000",
                                         .physical_cores = 8,
                                         .logical_cores = 16,
                                         .performance_cores = 6,
                                         .efficiency_cores = 2};
    view.machine.load =
        platform::LoadAverage{.one_minute = 2.314, .five_minutes = 1.98, .fifteen_minutes = 1.75};
    view.machine.utilization =
        platform::Utilization{.percent = 14.4, .window = std::chrono::milliseconds{500}};
    view.machine.memory = platform::MemoryInfo{.total = std::int64_t{64} << 30U,
                                               .available = std::int64_t{24} << 30U};
    view.machine.process_footprint = std::int64_t{42} << 20U;
    view.machine.gpu = platform::GpuInfo{.name = "Fake 9000", .unified_memory = true};
    view.model_budget = std::int64_t{48} << 30U;
    view.store = StoreDisk{.root = "/store/models",
                           .bytes = std::int64_t{300} << 30U,
                           .volume = platform::VolumeInfo{.capacity = std::int64_t{2} << 40U,
                                                          .available = std::int64_t{1} << 40U}};
    return view;
}

[[nodiscard]] SystemView unknown_view() {
    SystemView view;
    view.machine.load_unknown = "this OS keeps none";
    view.machine.gpu = platform::GpuInfo{.unknown = "not read on this platform yet"};
    view.model_budget_unknown = "this build has no llama.cpp";
    view.store = StoreDisk{.root = "/store/models"};
    return view;
}

}  // namespace

TEST_CASE("bytes read in binary units at the precision every surface shows",
          "[operations][system]") {
    using apogee::operations::format_bytes;
    CHECK(format_bytes(0) == "0 B");
    CHECK(format_bytes(1023) == "1023 B");
    CHECK(format_bytes(1024) == "1 KiB");
    CHECK(format_bytes(std::int64_t{42} << 20U) == "42 MiB");
    CHECK(format_bytes((std::int64_t{1} << 30U) - 1) == "1024 MiB");
    CHECK(format_bytes(std::int64_t{64} << 30U) == "64.0 GiB");
    CHECK(format_bytes((std::int64_t{61} << 30U) + (std::int64_t{205} << 20U)) == "61.2 GiB");
    CHECK(format_bytes(std::int64_t{2} << 40U) == "2.0 TiB");
    CHECK(apogee::operations::format_percent(14.4) == "14%");
    CHECK(apogee::operations::format_percent(14.5) == "15%");
    CHECK(apogee::operations::format_load(
              {.one_minute = 2.314, .five_minutes = 1.98, .fifteen_minutes = 1.75}) ==
          "2.31 1.98 1.75");
}

TEST_CASE("the table shows every reading, in the shared units", "[operations][system]") {
    const std::string arch{platform::to_string(platform::host_architecture())};
    CHECK(apogee::operations::system_table(full_view()) ==
          "cpu:    Fake 9000 · " + arch +
              " · 8 cores (6 performance + 2 efficiency), 16 threads\n"
              "load:   2.31 1.98 1.75 (1, 5, 15 min)\n"
              "usage:  14% over 500 ms\n"
              "memory: 64.0 GiB total · 40.0 GiB used · 24.0 GiB available\n"
              "apogee: 42 MiB (this process) · models held: none by this process -- a session "
              "holds its own; /suite in one names them\n"
              "gpu:    Fake 9000 (integrated, sharing the system's memory) · for models: 48.0 GiB "
              "of it (Metal's working set)\n"
              "disk:   models 300.0 GiB in /store/models · 1.0 TiB available of 2.0 TiB on its "
              "volume\n");
}

TEST_CASE("a platform that answers nothing still shows every row, each said unknown",
          "[operations][system]") {
    const std::string arch{platform::to_string(platform::host_architecture())};
    CHECK(apogee::operations::system_table(unknown_view()) ==
          "cpu:    unknown model · " + arch +
              " · cores unknown\n"
              "load:   unknown (this OS keeps none)\n"
              "usage:  unknown\n"
              "memory: unknown total · unknown used · unknown available\n"
              "apogee: unknown (this process) · models held: none by this process -- a session "
              "holds its own; /suite in one names them\n"
              "gpu:    unknown (not read on this platform yet) · for models: unknown (this build "
              "has no llama.cpp)\n"
              "disk:   models unknown in /store/models · unknown available of unknown on its "
              "volume\n");
}

TEST_CASE("the models a process holds are named", "[operations][system]") {
    SystemView view = full_view();
    view.models_held = {"root", "helper"};
    const std::string table = apogee::operations::system_table(view);
    CHECK(table.find("models held: root, helper\n") != std::string::npos);
    CHECK(apogee::operations::system_document(view)["apogee"]["models_held"] ==
          nlohmann::json::array({"root", "helper"}));
}

TEST_CASE("the document carries every field, an unknown one null beside its reason",
          "[operations][system]") {
    const nlohmann::json known = apogee::operations::system_document(full_view());
    const nlohmann::json unknown = apogee::operations::system_document(unknown_view());

    // The same keys either way: unknown-ness is a value, never an absence.
    const auto keys = [](const nlohmann::json& document) {
        std::string all;
        for (const auto& [section, fields] : document.items()) {
            all += section + "{";
            for (const auto& [field, value] : fields.items()) {
                all += field + ",";
            }
            all += "}";
        }
        return all;
    };
    CHECK(keys(known) == keys(unknown));

    CHECK(known["cpu"]["model"] == "Fake 9000");
    CHECK(known["cpu"]["load"]["one_minute"] == 2.31);
    CHECK(known["cpu"]["utilization"] == nlohmann::json{{"percent", 14.4}, {"window_ms", 500}});
    CHECK(known["memory"]["used_bytes"] == std::int64_t{40} << 30U);
    CHECK(known["gpu"]["model_budget_bytes"] == std::int64_t{48} << 30U);
    CHECK(known["gpu"]["model_budget_unknown"].is_null());
    CHECK(known["disk"]["store_bytes"] == std::int64_t{300} << 30U);

    CHECK(unknown["cpu"]["model"].is_null());
    CHECK(unknown["cpu"]["load"].is_null());
    CHECK(unknown["cpu"]["load_unknown"] == "this OS keeps none");
    CHECK(unknown["cpu"]["utilization"].is_null());
    CHECK(unknown["memory"]["total_bytes"].is_null());
    CHECK(unknown["memory"]["used_bytes"].is_null());
    CHECK(unknown["apogee"]["process_bytes"].is_null());
    CHECK(unknown["apogee"]["models_held"] == nlohmann::json::array());
    CHECK(unknown["gpu"]["name"].is_null());
    CHECK(unknown["gpu"]["unknown"] == "not read on this platform yet");
    CHECK(unknown["gpu"]["model_budget_bytes"].is_null());
    CHECK(unknown["gpu"]["model_budget_unknown"] == "this build has no llama.cpp");
    CHECK(unknown["disk"]["store_bytes"].is_null());
    CHECK(unknown["disk"]["volume_available_bytes"].is_null());
}

TEST_CASE("the store's footprint is its files, a link not counted, and no store is empty",
          "[operations][system]") {
    const apogee::testing::TempDir root{"system-store"};
    const std::filesystem::path store = root.path() / "models";
    const platform::SystemSource& host = platform::host_system();

    // No store yet: nothing in it, and the volume it would be on still read.
    const StoreDisk missing = apogee::operations::read_store_disk(host, store);
    CHECK(missing.bytes == 0);
    CHECK(missing.volume.capacity.has_value());

    std::filesystem::create_directories(store / "llama" / "gguf" / "abc");
    std::ofstream{store / "llama" / "gguf" / "abc" / "model.gguf", std::ios::binary}
        << std::string(3000, 'x');
    std::ofstream{store / "llama" / "record.json", std::ios::binary} << std::string(24, 'y');
    const std::filesystem::path outside = root.path() / "outside.gguf";
    std::ofstream{outside, std::ios::binary} << std::string(5000, 'z');
    std::error_code code;
    std::filesystem::create_symlink(outside, store / "linked.gguf", code);

    const StoreDisk disk = apogee::operations::read_store_disk(host, store);
    CHECK(disk.root == store);
    CHECK(disk.bytes == 3024);
    CHECK(disk.volume.capacity.has_value());
    CHECK(disk.volume.available.has_value());
}
