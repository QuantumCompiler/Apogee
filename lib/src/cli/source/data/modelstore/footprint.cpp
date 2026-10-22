#include "modelstore/footprint.h"

#include <algorithm>
#include <filesystem>
#include <map>
#include <system_error>
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

/// `path` absolute and normalized, as the prompt cache's fingerprint names a
/// model file (26j); as given when it cannot be made absolute.
[[nodiscard]] std::string normalized(const std::string& path) {
    if (path.empty()) {
        return {};
    }
    std::error_code code;
    const std::filesystem::path absolute = std::filesystem::absolute(path, code);
    return code ? path : absolute.lexically_normal().string();
}

/// Each adapter's file's size, in order: what llama.cpp reads it into memory
/// as. Nullopt for a file that cannot be read.
[[nodiscard]] std::vector<std::optional<std::int64_t>> adapter_sizes(
    const std::vector<harness::AdapterConfig>& adapters) {
    std::vector<std::optional<std::int64_t>> sizes;
    sizes.reserve(adapters.size());
    for (const harness::AdapterConfig& adapter : adapters) {
        std::error_code code;
        const std::uintmax_t size = std::filesystem::file_size(adapter.path, code);
        sizes.push_back(code ? std::nullopt
                             : std::optional<std::int64_t>{static_cast<std::int64_t>(size)});
    }
    return sizes;
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

BaseKey base_key(const std::string& model_path, const std::string& projector_path,
                 harness::KvCacheType cache_type, bool cache_type_named) {
    return BaseKey{.model = normalized(model_path),
                   .projector = normalized(projector_path),
                   .cache_type = cache_type,
                   .cache_type_named = cache_type_named};
}

BaseKey base_key(const harness::BackendConfig& as_run) {
    // As `LlamaCppProvider::options_from` reads the entry: a cache type the
    // config names, else the default.
    return base_key(harness::expand_env(as_run.model_path), harness::expand_env(as_run.mmproj_path),
                    as_run.cache_type.value_or(kDefaultCacheType), as_run.cache_type.has_value());
}

std::optional<std::int64_t> MemberFootprint::bytes() const {
    if (!local) {
        return 0;
    }
    // A member on another's base adds no weights of its own (53d).
    const bool own = shares.empty();
    if ((own && !weights.has_value()) || !window.has_value() || !window->cache_bytes.has_value() ||
        !adapter_bytes.has_value()) {
        return std::nullopt;
    }
    return (own ? *weights : 0) + *window->cache_bytes + *adapter_bytes;
}

std::int64_t SuiteFootprint::known_bytes() const {
    std::int64_t total = 0;
    for (const MemberFootprint& member : members) {
        total += member.bytes().value_or(0);
    }
    return total;
}

std::int64_t SuiteFootprint::saved_bytes() const {
    std::int64_t saved = 0;
    for (const MemberFootprint& member : members) {
        if (member.local && !member.shares.empty()) {
            saved += member.weights.value_or(0);
        }
    }
    return saved;
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
                                      const GgufInfo& header, const MemberExtras& extras) {
    MemberFootprint out;
    out.backend = member.backend;
    out.roles = member.roles;
    out.local = true;
    out.shares = extras.shares;

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

    // Its adapters, each read into memory over the base as its file is
    // (53a): unknown when one cannot be read, never left out.
    out.adapters = extras.adapters.size();
    std::int64_t adapters = 0;
    for (const std::optional<std::int64_t>& size : extras.adapters) {
        if (!size.has_value()) {
            out.adapter_bytes = std::nullopt;
            break;
        }
        adapters += *size;
    }
    if (out.adapter_bytes.has_value()) {
        out.adapter_bytes = adapters;
    }

    // The weights are the sharing member's own to be unknown about only when
    // it loads them: on another's base they are counted, or said, there.
    const bool own = extras.shares.empty();
    if (own && !model.has_value()) {
        out.unknown = "no recorded size";
    } else if (own && !projector.has_value()) {
        out.unknown = "its projector has no recorded size";
    } else if (!header.parsed) {
        out.unknown = "its header could not be read";
    } else if (!out.window->cache_bytes.has_value()) {
        out.unknown = "its cache size is not known for this architecture";
    } else if (!out.adapter_bytes.has_value()) {
        out.unknown = "an adapter's file could not be read";
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

    // The first member on each base, which counts its weights (53d).
    std::map<BaseKey, std::string> owners;

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
                // One loaded model serves every member on its base, as the
                // factory shares it, whatever adapters each applies (53d).
                MemberExtras extras{.adapters = adapter_sizes(as_run.adapters)};
                if (const auto [owner, first] =
                        owners.try_emplace(base_key(as_run), member.backend);
                    !first) {
                    extras.shares = owner->second;
                }
                out.members.push_back(gguf_member_footprint(
                    member, as_run, load_sidecar(file),
                    projector.empty() ? std::nullopt
                                      : load_sidecar(std::filesystem::path{projector}),
                    inspect_gguf(file), extras));
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
