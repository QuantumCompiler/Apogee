#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "platform/system_info.h"

/// The machine as Apogee shows it (32a): `platform`'s snapshot, the memory
/// a model offloads to as 26a's sizing reads it, what this process holds,
/// and the model store on its volume -- composed once, so `apogee system`'s
/// table, its JSON document and the TUI's monitor bar (32e) read one view in
/// one set of units and never disagree about the same machine.
///
/// Nothing here reads a config or a credential: hardware and process numbers
/// only.
namespace apogee::operations {

/// The model store on disk.
struct StoreDisk {
    std::filesystem::path root;
    /// Every file under `root` in all; 0 when there is no store yet, empty
    /// when it could not be walked.
    std::optional<std::int64_t> bytes;
    /// The volume `root` is on -- or, with no store yet, the one it would be.
    platform::VolumeInfo volume;
};

/// The store at `root`, its files summed (symlinks not followed, so a
/// linked model outside the store is not counted twice) and its volume read
/// through `source`.
[[nodiscard]] StoreDisk read_store_disk(const platform::SystemSource& source,
                                        const std::filesystem::path& root);

struct SystemView {
    platform::MachineSnapshot machine;
    /// The memory of the devices a model offloads to, in all -- the number
    /// 26a's window sizing and 27e's suite admission read
    /// (`backends::offload_memory_total`), handed in by the caller from that
    /// one function, never estimated here.
    std::optional<std::int64_t> model_budget;
    /// Why `model_budget` is unknown, for a person; empty when it is not.
    std::string model_budget_unknown;
    /// The backends whose models this process holds in memory (27e's
    /// `Harness::resident`). A process that loads nothing -- `apogee
    /// system`, one-shot -- holds none, and says where to look instead.
    std::vector<std::string> models_held;
    StoreDisk store;
};

/// `bytes` in binary units, the precision every surface shows: `512 B`,
/// `84 KiB`, `42 MiB`, `61.2 GiB`, `1.8 TiB`.
[[nodiscard]] std::string format_bytes(std::int64_t bytes);

/// A percentage as shown: whole numbers, `14%`.
[[nodiscard]] std::string format_percent(double percent);

/// The three load averages as shown: `2.31 1.98 1.75`.
[[nodiscard]] std::string format_load(const platform::LoadAverage& load);

/// The CPU as a person reads it: `Apple M3 Max · arm64 · 16 cores (12
/// performance + 4 efficiency)`, each part `unknown` where the OS said
/// nothing.
[[nodiscard]] std::string cpu_words(const platform::CpuInfo& cpu);

/// The human table, a line per reading and `unknown` wherever the platform
/// could not answer -- every row present on every platform.
[[nodiscard]] std::string system_table(const SystemView& view);

/// The same facts as one JSON document: every field present, an unknown one
/// `null` -- with the reason beside it where there is one.
[[nodiscard]] nlohmann::json system_document(const SystemView& view);

}  // namespace apogee::operations
