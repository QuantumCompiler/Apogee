#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "backends/llama_runtime.h"
#include "backends/model_profile.h"
#include "contracts/config.h"
#include "modelstore/gguf_inspect.h"

/// How a local model is sampled (26h). Every knob takes its value from the
/// first rung of one ladder that sets it:
///
///   1. the request's own -- `-t`, `/temperature`;
///   2. the backend's config;
///   3. the model file's `general.sampling.*`, its authors' recommendation;
///   4. the family's published default, with thinking on or off;
///   5. llama.cpp's neutral value -- greedy, for a model nothing above names.
///
/// Per knob rather than per rung: a file that names a temperature and no
/// top-k leaves the top-k to its family. Before this, the local backend was
/// greedy whatever was asked, and `-t` was accepted and ignored.
namespace apogee::backends {

/// The rung a value came from.
enum class SamplingSource : std::uint8_t { Request, Config, ModelFile, Family, Default };

/// "request", "config", "model file", "family", "default".
[[nodiscard]] std::string_view to_string(SamplingSource source) noexcept;

/// One value in force, and where it came from.
template <typename Value>
struct Sourced {
    Value value{};
    SamplingSource source = SamplingSource::Default;
};

/// What a generation samples with, each value with its source.
struct ResolvedSampling {
    Sourced<double> temperature;
    Sourced<double> top_p{.value = 1.0};
    Sourced<std::int64_t> top_k;
    Sourced<double> min_p;
    Sourced<double> repeat_penalty{.value = 1.0};
    Sourced<double> presence_penalty;
    /// The config's; unset draws one per generation.
    std::optional<std::uint32_t> seed;
    /// The family's source line, when any value came from the family rung.
    std::string family_source;

    /// The settings handed to the runtime.
    [[nodiscard]] SamplingSettings settings() const;
};

/// The ladder's rungs, top to bottom.
struct SamplingLadder {
    SamplingRung request;
    SamplingRung config;
    SamplingRung model_file;
    SamplingRung family;
    std::string family_source;
    std::optional<std::uint32_t> seed;
};

/// Resolves every knob down the ladder.
[[nodiscard]] ResolvedSampling resolve_sampling(const SamplingLadder& ladder);

/// The rung a backend's config offers, and its seed.
[[nodiscard]] SamplingRung config_rung(const harness::BackendConfig& backend);
[[nodiscard]] std::optional<std::uint32_t> config_seed(const harness::BackendConfig& backend);

/// The rung a model file's header offers.
[[nodiscard]] SamplingRung model_file_rung(const models::GgufSampling& sampling);

/// The rung a family offers, with thinking on or off; empty for an
/// unprofiled model or a family whose card names none.
[[nodiscard]] SamplingRung family_rung(const ModelProfile* profile, bool thinking);

/// What is in force, on one line: each value and its source, the family's
/// card named when it supplied any. `models info` and `--verbose` print it.
[[nodiscard]] std::string describe_sampling(const ResolvedSampling& resolved);

}  // namespace apogee::backends
