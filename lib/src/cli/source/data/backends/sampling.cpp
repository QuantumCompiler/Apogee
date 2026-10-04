#include "backends/sampling.h"

#include <initializer_list>
#include <iomanip>
#include <sstream>

namespace apogee::backends {
namespace {

/// The first rung that sets the knob `field` reaches, or the default.
template <typename Value, typename Field>
Sourced<Value> first_set(const SamplingLadder& ladder, Field field, Value fallback) {
    const std::initializer_list<std::pair<const SamplingRung*, SamplingSource>> rungs = {
        {&ladder.request, SamplingSource::Request},
        {&ladder.config, SamplingSource::Config},
        {&ladder.model_file, SamplingSource::ModelFile},
        {&ladder.family, SamplingSource::Family},
    };
    for (const auto& [rung, source] : rungs) {
        if (const auto& value = rung->*field; value.has_value()) {
            return {.value = static_cast<Value>(*value), .source = source};
        }
    }
    return {.value = fallback, .source = SamplingSource::Default};
}

/// A number as a person writes it: 0.8, not the 0.800000011920929 a file's
/// 32-bit float reads back as.
[[nodiscard]] std::string number(double value) {
    std::ostringstream out;
    out << std::setprecision(6) << value;
    return out.str();
}

template <typename Value>
void describe(std::ostringstream& out, std::string_view name, const Sourced<Value>& sourced) {
    out << (out.tellp() > 0 ? " · " : "") << name << " "
        << number(static_cast<double>(sourced.value)) << " (" << to_string(sourced.source) << ")";
}

}  // namespace

std::string_view to_string(SamplingSource source) noexcept {
    switch (source) {
        case SamplingSource::Request:
            return "request";
        case SamplingSource::Config:
            return "config";
        case SamplingSource::ModelFile:
            return "model file";
        case SamplingSource::Family:
            return "family";
        case SamplingSource::Default:
            return "default";
    }
    return "default";
}

SamplingSettings ResolvedSampling::settings() const {
    return SamplingSettings{.temperature = temperature.value,
                            .top_p = top_p.value,
                            .top_k = top_k.value,
                            .min_p = min_p.value,
                            .repeat_penalty = repeat_penalty.value,
                            .presence_penalty = presence_penalty.value,
                            .seed = seed};
}

ResolvedSampling resolve_sampling(const SamplingLadder& ladder) {
    ResolvedSampling out;
    out.temperature = first_set(ladder, &SamplingRung::temperature, 0.0);
    out.top_p = first_set(ladder, &SamplingRung::top_p, 1.0);
    out.top_k = first_set(ladder, &SamplingRung::top_k, std::int64_t{0});
    out.min_p = first_set(ladder, &SamplingRung::min_p, 0.0);
    out.repeat_penalty = first_set(ladder, &SamplingRung::repeat_penalty, 1.0);
    out.presence_penalty = first_set(ladder, &SamplingRung::presence_penalty, 0.0);
    out.seed = ladder.seed;
    for (const SamplingSource source :
         {out.temperature.source, out.top_p.source, out.top_k.source, out.min_p.source,
          out.repeat_penalty.source, out.presence_penalty.source}) {
        if (source == SamplingSource::Family) {
            out.family_source = ladder.family_source;
            break;
        }
    }
    return out;
}

SamplingRung config_rung(const harness::BackendConfig& backend) {
    return SamplingRung{.temperature = backend.temperature,
                        .top_p = backend.top_p,
                        .top_k = backend.top_k,
                        .min_p = backend.min_p,
                        .repeat_penalty = backend.repeat_penalty,
                        .presence_penalty = backend.presence_penalty};
}

std::optional<std::uint32_t> config_seed(const harness::BackendConfig& backend) {
    if (!backend.seed.has_value()) {
        return std::nullopt;
    }
    return static_cast<std::uint32_t>(*backend.seed);
}

SamplingRung model_file_rung(const models::GgufSampling& sampling) {
    return SamplingRung{.temperature = sampling.temperature,
                        .top_p = sampling.top_p,
                        .top_k = sampling.top_k,
                        .min_p = sampling.min_p,
                        .repeat_penalty = sampling.repeat_penalty,
                        .presence_penalty = std::nullopt};
}

SamplingRung family_rung(const ModelProfile* profile, bool thinking) {
    if (profile == nullptr) {
        return {};
    }
    return thinking ? profile->sampling_thinking : profile->sampling_answering;
}

std::string describe_sampling(const ResolvedSampling& resolved) {
    std::ostringstream out;
    if (resolved.temperature.value <= 0.0) {
        out << "greedy, temperature 0 (" << to_string(resolved.temperature.source) << ")";
    } else {
        describe(out, "temperature", resolved.temperature);
        describe(out, "top-p", resolved.top_p);
        describe(out, "top-k", resolved.top_k);
        describe(out, "min-p", resolved.min_p);
    }
    describe(out, "repeat penalty", resolved.repeat_penalty);
    describe(out, "presence penalty", resolved.presence_penalty);
    if (resolved.temperature.value > 0.0) {
        out << " · seed "
            << (resolved.seed.has_value() ? std::to_string(*resolved.seed) + " (config)"
                                          : std::string{"drawn per answer"});
    }
    if (!resolved.family_source.empty()) {
        out << " -- family values from the " << resolved.family_source;
    }
    return out.str();
}

}  // namespace apogee::backends
