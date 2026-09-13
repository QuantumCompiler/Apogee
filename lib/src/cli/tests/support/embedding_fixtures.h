#pragma once

#include <cstddef>
#include <string_view>

/// Embedding fixtures shared between the embedding-client tests here and the
/// per-store binding tests in vector-hybrid-rerank, which must refuse to mix
/// vectors of different widths or from different models in one collection.
///
/// **Shaped from the documented response format, not recorded from a live
/// call.** Apogee holds no API key of its own and never reads a user's
/// (`cli.no_vendor_credentials`), so there is nothing to record with; the
/// bodies below are the documented shape with short vectors. The two widths
/// are deliberately different and deliberately small.
namespace apogee::testing::embeddings {

/// What the two vendors' default embedding models really produce -- the
/// numbers a store should record beside its model name, and the mismatch a
/// binding test should stage.
inline constexpr std::size_t kOpenAiDefaultWidth = 1536;  // text-embedding-3-small
inline constexpr std::size_t kGoogleDefaultWidth = 3072;  // gemini-embedding-001

/// An OpenAI `/v1/embeddings` body for two inputs, with the rows **out of
/// order** -- the API carries an `index` per row and does not promise request
/// order, so a parser that trusts position is wrong in a way this fixture
/// catches.
inline constexpr std::string_view kOpenAiTwoRowsOutOfOrder = R"({
  "object": "list",
  "data": [
    {"object": "embedding", "index": 1, "embedding": [0.5, 0.5, 0.5, 0.5]},
    {"object": "embedding", "index": 0, "embedding": [1.0, 0.0, 0.0, 0.0]}
  ],
  "model": "text-embedding-3-small",
  "usage": {"prompt_tokens": 8, "total_tokens": 8}
})";

/// A Gemini `batchEmbedContents` body for two inputs. No index: rows come back
/// in request order, and that is the only order there is.
inline constexpr std::string_view kGoogleTwoRows = R"({
  "embeddings": [
    {"values": [1.0, 0.0, 0.0]},
    {"values": [0.0, 1.0, 0.0]}
  ]
})";

/// A body whose vectors are a different width from the OpenAI fixture's --
/// the same model name, a different width is exactly the inconsistency a
/// store must refuse.
inline constexpr std::string_view kOpenAiOneRowWidthEight = R"({
  "object": "list",
  "data": [
    {"object": "embedding", "index": 0, "embedding": [1,0,0,0,0,0,0,0]}
  ],
  "model": "text-embedding-3-small",
  "usage": {"prompt_tokens": 3, "total_tokens": 3}
})";

}  // namespace apogee::testing::embeddings
