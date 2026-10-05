#include "modelstore/footprint.h"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace apogee::models {
namespace {

/// The record's size of the file on disk, or nullopt when there is no record
/// or it states none.
[[nodiscard]] std::optional<std::int64_t> recorded_size(const std::optional<Sidecar>& record) {
    if (!record.has_value() || record->file_size <= 0) {
        return std::nullopt;
    }
    return record->file_size;
}

/// A member whose memory is not in this process and is not on the machine
/// in a form Apogee can price.
[[nodiscard]] MemberFootprint unpriced(const harness::SuiteBackend& member, bool local,
                                       std::string unknown) {
    MemberFootprint out;
    out.backend = member.backend;
    out.roles = member.roles;
    out.local = local;
    out.unknown = std::move(unknown);
    return out;
}

}  // namespace

std::optional<std::int64_t> MemberFootprint::bytes() const {
    if (!local) {
        return 0;
    }
    if (!weights.has_value() || !window.has_value() || !window->cache_bytes.has_value()) {
        return std::nullopt;
    }
    return *weights + *window->cache_bytes;
}

std::int64_t SuiteFootprint::known_bytes() const {
    std::int64_t total = 0;
    for (const MemberFootprint& member : members) {
        total += member.bytes().value_or(0);
    }
    return total;
}

bool SuiteFootprint::holds_local() const {
    return std::ranges::any_of(members, [](const MemberFootprint& member) { return member.local; });
}

bool SuiteFootprint::has_unknown() const {
    return std::ranges::any_of(
        members, [](const MemberFootprint& member) { return !member.bytes().has_value(); });
}

std::int64_t SuiteFootprint::needed() const {
    return holds_local() ? known_bytes() + kFitMargin : 0;
}

Admission SuiteFootprint::admission() const {
    if (!holds_local()) {
        return Admission::NothingHeld;
    }
    if (!machine.bytes.has_value()) {
        return Admission::BudgetUnknown;
    }
    // What is known alone is enough to refuse; an unknown never is.
    if (needed() > *machine.bytes) {
        return Admission::OverBudget;
    }
    return has_unknown() ? Admission::FitsAsFarAsKnown : Admission::Fits;
}

MemberFootprint gguf_member_footprint(const harness::SuiteBackend& member,
                                      const harness::BackendConfig& as_run,
                                      const std::optional<Sidecar>& record,
                                      const std::optional<Sidecar>& projector_record,
                                      const GgufInfo& header) {
    MemberFootprint out;
    out.backend = member.backend;
    out.roles = member.roles;
    out.local = true;

    // The weights the store recorded: the model file, and its projector when
    // the entry loads one -- 26a's check counts both.
    const std::optional<std::int64_t> model = recorded_size(record);
    const bool projected = !as_run.mmproj_path.empty();
    const std::optional<std::int64_t> projector =
        projected ? recorded_size(projector_record) : std::optional<std::int64_t>{0};
    if (model.has_value() && projector.has_value()) {
        out.weights = *model + *projector;
    }

    // The cache, as 26a states it, at the window the suite pins.
    if (header.parsed) {
        out.window = local_window(header, as_run);
    }

    if (!model.has_value()) {
        out.unknown = "no recorded size";
    } else if (!projector.has_value()) {
        out.unknown = "its projector has no recorded size";
    } else if (!header.parsed) {
        out.unknown = "its header could not be read";
    } else if (!out.window->cache_bytes.has_value()) {
        out.unknown = "its cache size is not known for this architecture";
    }
    return out;
}

SuiteFootprint suite_footprint(const harness::Config& config, std::string_view suite,
                               const MachineBudget& machine) {
    SuiteFootprint out;
    out.machine = machine;
    const auto entry = config.suites.find(suite);
    if (entry == config.suites.end()) {
        out.suite = std::string{suite};
        return out;
    }
    out.suite = entry->first;

    // Priced at the windows THIS suite pins, active or not: the entry as it
    // would run were the suite active, read through the one reader of a
    // member's pins.
    harness::Config under = config;
    under.models.default_suite = entry->first;

    for (harness::SuiteBackend member : harness::suite_backends(entry->second)) {
        const auto backend = config.backends.find(member.backend);
        if (backend == config.backends.end()) {
            out.members.push_back(
                unpriced(member, false, "no backend named '" + member.backend + "'"));
            continue;
        }
        // Named as the config spells it: the key its provider is registered
        // under, which a session asks about its residency by.
        member.backend = backend->first;
        switch (backend->second.type) {
            case harness::BackendType::LlamaCpp: {
                const harness::BackendConfig as_run =
                    harness::backend_as_run(under, member.backend);
                const std::string path = harness::expand_env(as_run.model_path);
                if (path.empty()) {
                    out.members.push_back(unpriced(member, true, "no model_path"));
                    break;
                }
                const std::filesystem::path file{path};
                const std::string projector = harness::expand_env(as_run.mmproj_path);
                out.members.push_back(gguf_member_footprint(
                    member, as_run, load_sidecar(file),
                    projector.empty() ? std::nullopt
                                      : load_sidecar(std::filesystem::path{projector}),
                    inspect_gguf(file)));
                break;
            }
            case harness::BackendType::Mlx:
                // 27b's windows slot into the same arithmetic when that track
                // prices them; until then, unknown -- and it still runs.
                out.members.push_back(
                    unpriced(member, true, "an MLX model's footprint is not stated yet"));
                break;
            case harness::BackendType::OllamaCli:
                out.members.push_back(
                    unpriced(member, true, "held by the Ollama server, which states no size here"));
                break;
            case harness::BackendType::Anthropic:
            case harness::BackendType::OpenAI:
            case harness::BackendType::Google:
            case harness::BackendType::ClaudeCli:
            case harness::BackendType::CodexCli:
            case harness::BackendType::GeminiCli:
            case harness::BackendType::Mock:
                // Nothing of its own in this machine's memory.
                out.members.push_back(unpriced(member, false, {}));
                break;
        }
    }
    return out;
}

}  // namespace apogee::models
