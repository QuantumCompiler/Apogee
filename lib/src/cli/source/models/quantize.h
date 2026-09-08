#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

/// Re-quantizing a GGUF in-process.
///
/// Shrinking a model is otherwise a second toolchain: llama.cpp's own
/// `llama-quantize` binary, which Apogee's build deliberately does not produce.
/// Linking the library it wraps costs nothing extra once llama.cpp is already
/// linked, so a user who has a model can make a smaller one without leaving the
/// harness.
///
/// ## It sits behind the build flag, and says so
///
/// `llama_model_quantize` needs llama.cpp. The GGUF *reader* could be
/// reimplemented in two hundred lines when that turned out to be a problem
/// (see `gguf_inspect.h`); quantization cannot, so this genuinely requires
/// `-DAPOGEE_ENABLE_LLAMA=ON`.
///
/// **The command still exists in every build.** A subcommand that is simply
/// absent reads as "Apogee cannot do this"; one that refuses with a reason
/// reads as "this build cannot, and here is the flag". That difference is the
/// whole point of the `#else` branch, and it is what the refusal test pins.
namespace apogee::models {

/// A quantization type, as a user names it on the command line.
struct QuantType {
    /// The spelling, e.g. "Q4_K_M".
    std::string name;
    /// A short description for the listing.
    std::string summary;
};

/// The types this build accepts, in a sensible order for a listing.
///
/// A curated subset rather than everything llama.cpp defines: the full list
/// runs to dozens of variants, most of which exist for research. Naming the
/// handful people actually use makes the error message for a typo useful.
[[nodiscard]] std::vector<QuantType> quant_types();

/// What a quantization run produced.
struct QuantizeResult {
    bool ok = false;
    /// Why not. Always set when `ok` is false — including the "this build has
    /// no llama.cpp" case, which names the flag.
    std::string error;
    /// Sizes either side, for the report. Zero when the run did not complete.
    std::int64_t input_bytes = 0;
    std::int64_t output_bytes = 0;
};

/// Quantizes `input` into `output` at `type`.
///
/// `output` must not already exist — the same rule acquisition follows, and for
/// the same reason: a quantize that silently replaced a model someone was using
/// would be the most expensive kind of convenience.
[[nodiscard]] QuantizeResult quantize(const std::filesystem::path& input,
                                      const std::filesystem::path& output, std::string_view type);

}  // namespace apogee::models
