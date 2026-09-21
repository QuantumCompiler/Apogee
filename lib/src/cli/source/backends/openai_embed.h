#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <string>
#include <string_view>
#include <vector>

/// Wire translation for OpenAI's embeddings endpoint (`POST /v1/embeddings`).
///
/// Pure functions, like the chat translators: JSON in, JSON or vectors out, no
/// HTTP. The provider owns the request, the key, and the retry policy; this
/// file owns only the shape of the body and the shape of the answer, which is
/// what lets every case here be tested with a string.
///
/// **Not recorded from a live call.** The chat translators' fixtures were
/// shaped from the documented format, and so are these: Apogee holds no API
/// key of its own and never reads a user's (`cli.no_vendor_credentials`), so
/// there is nothing to record with. The bodies in the tests are the documented
/// response with short vectors. If that documentation is ever wrong, this is
/// where the fix lands, and the test fixture with it.
namespace apogee::backends::openai_embed {

/// The most inputs one request may carry -- the documented maximum, taken as
/// the batch size (the recorded default for this item). A larger list is split
/// here rather than refused.
inline constexpr std::size_t kMaxInputsPerRequest = 2048;

/// The model used when the entry sets none.
[[nodiscard]] std::string_view default_model() noexcept;

/// Dimensions the named model produces, or 0 when this build does not know
/// the model -- the interface's "known after the first call" answer.
[[nodiscard]] std::size_t known_dimensions(std::string_view model) noexcept;

/// The request body for one batch.
[[nodiscard]] nlohmann::json build_request(std::string_view model,
                                           const std::vector<std::string>& inputs);

/// One vector per input, **in input order**.
///
/// The response carries an `index` per row and does not promise to return
/// rows in request order, so rows are placed by index rather than trusted.
/// Throws std::runtime_error naming what was wrong when the body is not the
/// documented shape or is missing a row.
[[nodiscard]] std::vector<std::vector<float>> parse_response(const nlohmann::json& body,
                                                             std::size_t expected);

}  // namespace apogee::backends::openai_embed
