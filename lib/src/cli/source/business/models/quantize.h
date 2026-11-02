#pragma once

#include <cstddef>
#include <filesystem>
#include <functional>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
#include "platform/child_process.h"

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
///
/// ## A stop stops it at once: it runs as a child (57i)
///
/// `llama_model_quantize` takes no abort hook, and a stop acted on only when
/// it returns comes after the GUI's ten-second grace has turned into SIGTERM
/// and SIGKILL. So `models quantize` and the register chain run it in a
/// child of Apogee's own -- `apogee __quantize <input> <output> <type>`,
/// hidden plumbing -- whose per-tensor progress and result come back over
/// its stdout, one JSON object a line, and whose stderr is a bounded tail:
/// a stop terminates the child, and the caller removes what it staged.
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

/// Their spellings alone, for completion.
[[nodiscard]] std::vector<std::string> quant_type_names();

/// `name` as the table spells it -- `q4_k_m` is `Q4_K_M` -- or nothing when
/// it names no accepted type. What a backend made from a quantization is
/// named after, so one level is never registered twice under two spellings.
[[nodiscard]] std::optional<std::string> canonical_quant_type(std::string_view name);

/// How far a quantization is: the tensor it is on, of how many. Read from
/// llama.cpp's own per-tensor log lines, so it is said only when they are.
using QuantizeProgress = std::function<void(std::size_t done, std::size_t total)>;

/// What a quantization run produced.
struct QuantizeResult {
    bool ok = false;
    /// Why not. Always set when `ok` is false — including the "this build has
    /// no llama.cpp" case, which names the flag.
    std::string error;
    /// Sizes either side, for the report. Zero when the run did not complete.
    std::int64_t input_bytes = 0;
    std::int64_t output_bytes = 0;
    /// Stopped by its cancellation token before it finished (57i): `ok` is
    /// false, and what it wrote is the caller's to remove.
    bool stopped = false;
};

/// Whether this build links llama.cpp and so can quantize at all. A caller
/// with a long pipeline ahead of the quantize step (`train promote
/// --quantize`) asks this first, so it refuses before fusing and converting
/// rather than after.
[[nodiscard]] bool quantize_supported() noexcept;

/// Quantizes `input` into `output` at `type`.
///
/// `output` must not already exist — the same rule acquisition follows, and for
/// the same reason: a quantize that silently replaced a model someone was using
/// would be the most expensive kind of convenience.
///
/// **llama.cpp's log never reaches the terminal** (M3). For the run its log
/// callback is Apogee's: the metadata dump and the per-tensor lines are
/// dropped -- the latter told to `progress` -- and its warnings and errors
/// are kept for a failure's message. The callback in place before is put
/// back after.
[[nodiscard]] QuantizeResult quantize(const std::filesystem::path& input,
                                      const std::filesystem::path& output, std::string_view type,
                                      const QuantizeProgress& progress = {});

/// The hidden verb a quantization runs in as a child of Apogee's own (57i).
inline constexpr std::string_view kQuantizeVerb = "__quantize";

/// `<executable> __quantize <input> <output> <type>`: one quantization in a
/// child of `executable` -- the running binary, so `apogee-dev` runs its own.
[[nodiscard]] platform::ChildCommand quantize_child_command(const std::filesystem::path& executable,
                                                            const std::filesystem::path& input,
                                                            const std::filesystem::path& output,
                                                            std::string_view type);

/// The child's side (`apogee __quantize`): `quantize` run, and said on `out`
/// as the parent reads it -- `{"tensor":12,"of":291}` a line as each tensor
/// is done, then once, last, `{"ok":true,"input_bytes":…,"output_bytes":…}`
/// or `{"ok":false,"error":"…"}` -- every line flushed as it is written.
/// Returns the child's exit code: 0 when it quantized, 1 when it did not.
[[nodiscard]] int quantize_for_parent(const std::filesystem::path& input,
                                      const std::filesystem::path& output, std::string_view type,
                                      std::ostream& out);

/// The parent's side: `command` (`quantize_child_command`'s, or a test's
/// stand-in) run as a captured child, its stdin closed, each progress line
/// told to `progress`, its stderr kept as an 8 KiB tail and never written
/// anywhere. `cancellation` is polled between reads: a stop terminates the
/// child at once and answers `stopped`. A child that ends without its result
/// line fails with the last line its stderr said. No deadline: a large model
/// quantizes for as long as it takes.
[[nodiscard]] QuantizeResult quantize_in_child(const platform::ChildCommand& command,
                                               const QuantizeProgress& progress,
                                               const harness::CancellationToken& cancellation);

}  // namespace apogee::models
