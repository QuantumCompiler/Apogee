#include "backends/sampling.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <string>

#include "backends/model_profile.h"
#include "contracts/config.h"
#include "modelstore/gguf_inspect.h"

/// The sampling ladder (26h), rung by rung: the request, the config, the
/// model file, the family, greedy. Per knob, so a rung that names one value
/// leaves the others to the rungs below.
using apogee::backends::config_rung;
using apogee::backends::config_seed;
using apogee::backends::describe_sampling;
using apogee::backends::family_rung;
using apogee::backends::model_file_rung;
using apogee::backends::ModelProfile;
using apogee::backends::resolve_profile;
using apogee::backends::resolve_sampling;
using apogee::backends::ResolvedSampling;
using apogee::backends::SamplingLadder;
using apogee::backends::SamplingRung;
using apogee::backends::SamplingSource;
using Catch::Matchers::ContainsSubstring;

TEST_CASE("with nothing on any rung, sampling is greedy and says it is the default",
          "[backends][sampling]") {
    const ResolvedSampling resolved = resolve_sampling(SamplingLadder{});
    CHECK(resolved.temperature.value == 0.0);
    CHECK(resolved.temperature.source == SamplingSource::Default);
    CHECK(resolved.top_p.value == 1.0);
    CHECK(resolved.top_k.value == 0);
    CHECK(resolved.min_p.value == 0.0);
    CHECK(resolved.repeat_penalty.value == 1.0);
    CHECK(resolved.presence_penalty.value == 0.0);
    CHECK_FALSE(resolved.seed.has_value());
    CHECK(resolved.settings().greedy());
    CHECK(describe_sampling(resolved).starts_with("greedy, temperature 0 (default)"));
}

TEST_CASE("each rung outranks every rung below it, one rung at a time", "[backends][sampling]") {
    // The temperature down the whole ladder: remove the top rung each time and
    // the next one answers.
    SamplingLadder ladder{.request = {.temperature = 0.1},
                          .config = {.temperature = 0.2},
                          .model_file = {.temperature = 0.3},
                          .family = {.temperature = 0.4}};
    CHECK(resolve_sampling(ladder).temperature.value == 0.1);
    CHECK(resolve_sampling(ladder).temperature.source == SamplingSource::Request);
    ladder.request = {};
    CHECK(resolve_sampling(ladder).temperature.value == 0.2);
    CHECK(resolve_sampling(ladder).temperature.source == SamplingSource::Config);
    ladder.config = {};
    CHECK(resolve_sampling(ladder).temperature.value == 0.3);
    CHECK(resolve_sampling(ladder).temperature.source == SamplingSource::ModelFile);
    ladder.model_file = {};
    CHECK(resolve_sampling(ladder).temperature.value == 0.4);
    CHECK(resolve_sampling(ladder).temperature.source == SamplingSource::Family);
    ladder.family = {};
    CHECK(resolve_sampling(ladder).temperature.source == SamplingSource::Default);
}

TEST_CASE("every knob walks the ladder on its own", "[backends][sampling]") {
    // A file that names a temperature and nothing else leaves the rest to
    // the family, and a config that names a top-k alone takes only that.
    const ResolvedSampling resolved = resolve_sampling(
        SamplingLadder{.config = {.top_k = 40},
                       .model_file = {.temperature = 0.7, .repeat_penalty = 1.05},
                       .family = {.temperature = 0.6, .top_p = 0.95, .top_k = 20, .min_p = 0.05},
                       .family_source = "the card"});
    CHECK(resolved.temperature.value == 0.7);
    CHECK(resolved.temperature.source == SamplingSource::ModelFile);
    CHECK(resolved.top_p.value == 0.95);
    CHECK(resolved.top_p.source == SamplingSource::Family);
    CHECK(resolved.top_k.value == 40);
    CHECK(resolved.top_k.source == SamplingSource::Config);
    CHECK(resolved.min_p.value == 0.05);
    CHECK(resolved.min_p.source == SamplingSource::Family);
    CHECK(resolved.repeat_penalty.value == 1.05);
    CHECK(resolved.repeat_penalty.source == SamplingSource::ModelFile);
    CHECK(resolved.presence_penalty.source == SamplingSource::Default);
    CHECK(resolved.family_source == "the card");
}

TEST_CASE("the family's source is named only when the family supplied a value",
          "[backends][sampling]") {
    // A family whose every value a higher rung covered contributed nothing,
    // and a line crediting its card would say otherwise.
    const ResolvedSampling covered =
        resolve_sampling(SamplingLadder{.model_file = {.temperature = 0.7},
                                        .family = {.temperature = 0.6},
                                        .family_source = "the card"});
    CHECK(covered.family_source.empty());
    CHECK_THAT(describe_sampling(covered), !ContainsSubstring("the card"));
}

TEST_CASE("the config's seed passes through, and fixes the draw", "[backends][sampling]") {
    apogee::harness::BackendConfig backend;
    backend.seed = 42;
    const ResolvedSampling resolved = resolve_sampling(
        SamplingLadder{.request = {.temperature = 0.8}, .seed = config_seed(backend)});
    REQUIRE(resolved.seed.has_value());
    CHECK(*resolved.seed == 42U);
    CHECK(resolved.settings().seed == resolved.seed);
    CHECK_THAT(describe_sampling(resolved), ContainsSubstring("seed 42 (config)"));

    CHECK_THAT(describe_sampling(resolve_sampling(SamplingLadder{.request = {.temperature = 0.8}})),
               ContainsSubstring("seed drawn per answer"));
}

TEST_CASE("the settings handed to the runtime are the resolved values", "[backends][sampling]") {
    const ResolvedSampling resolved =
        resolve_sampling(SamplingLadder{.request = {.temperature = 0.9},
                                        .config = {.top_p = 0.8,
                                                   .top_k = 20,
                                                   .min_p = 0.05,
                                                   .repeat_penalty = 1.1,
                                                   .presence_penalty = 0.5}});
    const apogee::backends::SamplingSettings settings = resolved.settings();
    CHECK(settings.temperature == 0.9);
    CHECK(settings.top_p == 0.8);
    CHECK(settings.top_k == 20);
    CHECK(settings.min_p == 0.05);
    CHECK(settings.repeat_penalty == 1.1);
    CHECK(settings.presence_penalty == 0.5);
    CHECK_FALSE(settings.greedy());
}

TEST_CASE("a backend's config and a file's header each make a rung", "[backends][sampling]") {
    apogee::harness::BackendConfig backend;
    backend.temperature = 0.5;
    backend.top_p = 0.9;
    backend.top_k = 30;
    backend.min_p = 0.02;
    backend.repeat_penalty = 1.2;
    backend.presence_penalty = 0.3;
    const SamplingRung from_config = config_rung(backend);
    CHECK(from_config.temperature == 0.5);
    CHECK(from_config.top_p == 0.9);
    CHECK(from_config.top_k == 30);
    CHECK(from_config.min_p == 0.02);
    CHECK(from_config.repeat_penalty == 1.2);
    CHECK(from_config.presence_penalty == 0.3);

    apogee::models::GgufSampling header;
    header.temperature = 0.7;
    header.top_p = 0.8;
    header.top_k = 20;
    header.min_p = 0.0;
    header.repeat_penalty = 1.0;
    const SamplingRung from_file = model_file_rung(header);
    CHECK(from_file.temperature == 0.7);
    CHECK(from_file.top_p == 0.8);
    CHECK(from_file.top_k == 20);
    CHECK(from_file.min_p == 0.0);
    CHECK(from_file.repeat_penalty == 1.0);
    // No conversion writes a presence penalty: the rung never claims one.
    CHECK_FALSE(from_file.presence_penalty.has_value());
}

TEST_CASE("Qwen samples one way thinking and another answering, as its card says",
          "[backends][sampling][profile]") {
    const ModelProfile* qwen = resolve_profile({}, "qwen35", {});
    REQUIRE(qwen != nullptr);
    const SamplingRung thinking = family_rung(qwen, true);
    const SamplingRung answering = family_rung(qwen, false);
    CHECK(thinking.temperature == 0.6);
    CHECK(thinking.top_p == 0.95);
    CHECK(thinking.top_k == 20);
    CHECK(answering.temperature == 0.7);
    CHECK(answering.top_p == 0.8);
    CHECK(answering.top_k == 20);
    CHECK_THAT(qwen->sampling_source, ContainsSubstring("Qwen3 model card"));
}

TEST_CASE("every family whose card names its sampling carries the card",
          "[backends][sampling][profile]") {
    // Profiles are evidence: a default with no source is a guess presented
    // as a recommendation.
    for (const ModelProfile& profile : apogee::backends::model_profiles()) {
        const bool names_sampling = profile.sampling_thinking.temperature.has_value() ||
                                    profile.sampling_answering.temperature.has_value();
        INFO(profile.name);
        CHECK(names_sampling == !profile.sampling_source.empty());
    }
    CHECK(family_rung(nullptr, true).temperature == std::nullopt);
    // Gemma, gpt-oss and Llama, each from its own card.
    CHECK(family_rung(resolve_profile({}, "gemma3", {}), true).top_k == 64);
    CHECK(family_rung(resolve_profile({}, "gpt-oss", {}), false).temperature == 1.0);
    CHECK(family_rung(resolve_profile({}, "llama", {}), true).top_p == 0.9);
}

TEST_CASE("the description names each value, its source, and the family's card",
          "[backends][sampling]") {
    const ResolvedSampling resolved = resolve_sampling(
        SamplingLadder{.model_file = {.temperature = 0.699999988079071, .top_p = 0.800000011920929},
                       .family = {.top_k = 20},
                       .family_source = "Qwen3 model card"});
    const std::string line = describe_sampling(resolved);
    // A file's 32-bit float reads back as a person would write it.
    CHECK_THAT(line, ContainsSubstring("temperature 0.7 (model file)"));
    CHECK_THAT(line, ContainsSubstring("top-p 0.8 (model file)"));
    CHECK_THAT(line, ContainsSubstring("top-k 20 (family)"));
    CHECK_THAT(line, ContainsSubstring("min-p 0 (default)"));
    CHECK_THAT(line, ContainsSubstring("family values from the Qwen3 model card"));
}
