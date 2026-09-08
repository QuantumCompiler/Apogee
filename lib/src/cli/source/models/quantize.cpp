#include "models/quantize.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <system_error>

#include "models/gguf_inspect.h"

#if defined(APOGEE_ENABLE_LLAMA)
#include <llama.h>
#endif

namespace apogee::models {
namespace {

/// The curated set, with llama.cpp's enum value alongside each name.
///
/// A subset on purpose: llama.cpp defines dozens, most for research. These are
/// the ones people actually ship, and a short list is what lets a typo produce
/// a useful "did you mean" rather than a wall.
struct Entry {
    std::string_view name;
    std::string_view summary;
    int type;  // llama_ftype, kept as int so this table needs no llama.h
};

// The values are llama.cpp's LLAMA_FTYPE_MOSTLY_* constants. They are stable
// wire-visible numbers -- a GGUF records them -- so pinning them here rather
// than including llama.h keeps the table readable in a build without it. The
// static_asserts below fail the build if any of them ever moves.
constexpr std::array<Entry, 7> kTypes{{
    {.name = "Q2_K", .summary = "smallest, noticeable quality loss", .type = 10},
    {.name = "Q3_K_M", .summary = "very small, moderate quality loss", .type = 12},
    {.name = "Q4_K_M", .summary = "small, balanced -- the usual choice", .type = 15},
    {.name = "Q5_K_M", .summary = "medium, low quality loss", .type = 17},
    {.name = "Q6_K", .summary = "large, very low quality loss", .type = 18},
    {.name = "Q8_0", .summary = "largest quantized, minimal loss", .type = 7},
    {.name = "F16", .summary = "unquantized half precision", .type = 1},
}};

#if defined(APOGEE_ENABLE_LLAMA)
static_assert(static_cast<int>(LLAMA_FTYPE_MOSTLY_Q4_K_M) == 15,
              "llama.cpp's ftype numbering moved; the quantize table needs updating");
static_assert(static_cast<int>(LLAMA_FTYPE_MOSTLY_Q8_0) == 7,
              "llama.cpp's ftype numbering moved; the quantize table needs updating");
static_assert(static_cast<int>(LLAMA_FTYPE_MOSTLY_F16) == 1,
              "llama.cpp's ftype numbering moved; the quantize table needs updating");
#endif

[[nodiscard]] const Entry* find_type(std::string_view name) {
    const auto* const match = std::ranges::find_if(kTypes, [name](const Entry& entry) {
        return std::ranges::equal(entry.name, name, [](char a, char b) {
            return std::toupper(static_cast<unsigned char>(a)) ==
                   std::toupper(static_cast<unsigned char>(b));
        });
    });
    return match == kTypes.end() ? nullptr : &*match;
}

[[nodiscard]] std::string accepted_names() {
    std::string out;
    for (const Entry& entry : kTypes) {
        if (!out.empty()) {
            out += ", ";
        }
        out += entry.name;
    }
    return out;
}

}  // namespace

std::vector<QuantType> quant_types() {
    std::vector<QuantType> types;
    types.reserve(kTypes.size());
    for (const Entry& entry : kTypes) {
        types.push_back({.name = std::string{entry.name}, .summary = std::string{entry.summary}});
    }
    return types;
}

QuantizeResult quantize(const std::filesystem::path& input, const std::filesystem::path& output,
                        std::string_view type) {
    QuantizeResult result;

    const Entry* entry = find_type(type);
    if (entry == nullptr) {
        result.error =
            "unknown quantization type '" + std::string{type} + "'. Accepted: " + accepted_names();
        return result;
    }

    std::error_code code;
    if (!std::filesystem::exists(input, code)) {
        result.error = "no such file: " + input.string();
        return result;
    }
    if (std::filesystem::exists(output, code)) {
        // The same rule acquisition follows: a quantize that silently replaced
        // a model someone was using would be the most expensive convenience.
        result.error = "a file already exists at " + output.string() + " -- delete it first";
        return result;
    }

    // The input has to actually be a GGUF, checked before llama.cpp is asked.
    // Its own failure for a non-GGUF is a log line and a non-zero return with
    // no explanation attached; ours names the file and the reason.
    const GgufInfo info = inspect_gguf(input);
    if (!info.parsed) {
        result.error = input.string() + " is not a readable GGUF -- " + info.parse_error;
        return result;
    }
    result.input_bytes = info.file_size;

    if (info.is_quantized()) {
        // llama.cpp refuses to requantize, and says so only after a couple of
        // hundred per-tensor log lines -- by which point the real reason is
        // long off the top of the screen. Saying it first, in one sentence,
        // costs a header read that already happened.
        result.error = input.filename().string() +
                       " is already quantized, and llama.cpp cannot requantize. Start from the "
                       "model's F16 or F32 release instead";
        return result;
    }

#if defined(APOGEE_ENABLE_LLAMA)
    llama_model_quantize_params params = llama_model_quantize_default_params();
    params.ftype = static_cast<llama_ftype>(entry->type);

    if (llama_model_quantize(input.string().c_str(), output.string().c_str(), &params) != 0) {
        // llama.cpp has already logged the specifics; this says which file and
        // what was being attempted, which its own message does not.
        result.error =
            "llama.cpp could not quantize " + input.string() + " to " + std::string{entry->name};
        std::filesystem::remove(output, code);
        return result;
    }

    result.output_bytes = static_cast<std::int64_t>(std::filesystem::file_size(output, code));
    result.ok = true;
    return result;
#else
    // Present and refusing, rather than absent. A missing subcommand reads as
    // "Apogee cannot do this"; this reads as "this build cannot, and here is
    // the flag" -- which is the difference the refusal test pins.
    (void)entry;
    result.error =
        "this build cannot quantize: llama.cpp was not compiled in. Rebuild with "
        "-DAPOGEE_ENABLE_LLAMA=ON";
    return result;
#endif
}

}  // namespace apogee::models
