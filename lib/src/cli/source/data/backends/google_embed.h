#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// Wire translation for Gemini's batch embeddings endpoint
/// (`POST /{version}/models/{model}:batchEmbedContents`).
///
/// Pure functions; the provider owns the HTTP. As with `openai_embed.h`, the
/// shapes are taken from the documented format rather than recorded from a
/// live call, because Apogee holds no key to record with.
///
/// Two things that differ from OpenAI and are easy to get wrong: the model is
/// named in the URL **and** repeated, prefixed `models/`, inside every request
/// row; and rows come back in request order with no index, so order is
/// trusted here where it is checked there.
namespace apogee::backends::google_embed {

/// The most rows one `batchEmbedContents` call may carry -- the documented
/// maximum, taken as the batch size (the recorded default for this item).
inline constexpr std::size_t kMaxInputsPerRequest = 100;

/// The model used when the entry sets none.
[[nodiscard]] std::string_view default_model() noexcept;

/// Dimensions the named model produces, or 0 when unknown here.
[[nodiscard]] std::size_t known_dimensions(std::string_view model) noexcept;

/// The request body for one batch.
[[nodiscard]] nlohmann::json build_request(std::string_view model,
                                           const std::vector<std::string>& inputs);

/// One vector per input, in order. Throws std::runtime_error naming what was
/// wrong when the body is not the documented shape.
[[nodiscard]] std::vector<std::vector<float>> parse_response(const nlohmann::json& body,
                                                             std::size_t expected);

}  // namespace apogee::backends::google_embed
