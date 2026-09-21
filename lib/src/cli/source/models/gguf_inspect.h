#pragma once

#include <cstdint>
#include <filesystem>
#include <string>

/// Reading a GGUF file's header — architecture, tensor counts, and whether it
/// parses at all.
///
/// **Header only.** Nothing here touches tensor data, so the whole inspection
/// costs a few kilobytes of reads however large the file is. That is what lets
/// `apogee check` and `apogee models info` run it on every invocation instead
/// of hiding it behind a flag nobody passes.
///
/// ## Why this is not llama.cpp's `gguf.h`
///
/// The item this was built from recorded the opposite decision: use llama.cpp's
/// reader, so that "the header parses" and "llama.cpp can read it" are one
/// claim rather than two. That rationale is sound and it does not survive
/// contact with the build.
///
/// **llama.cpp is off by default** (`-DAPOGEE_ENABLE_LLAMA=ON` opts in), and
/// `macos-arm64` — the only merge-blocking CI target — builds without it. A
/// reader behind that flag would mean `check` and `models info` report
/// *nothing* in the default build, and, decisively, that this item's own
/// guardrail ("GGUF header inspection run in CI against a fixture") would not
/// run in the job that gates merges. A check that cannot run where it matters
/// is not a check.
///
/// So the header read is ours and always compiles, and it is the only claim
/// this file makes: **the header is well formed**. It deliberately does not
/// claim the model loads — that is a stronger statement, it needs llama.cpp,
/// and a build without llama.cpp must say "not checked" rather than imply it.
/// Two claims honestly distinguished beats one claim that is usually
/// unavailable, and the stronger one belongs with whatever links the runtime.
///
/// ## Why a header read and not a full load
///
/// Ommi's recorded lesson, and the reason this reads structure rather than
/// calling `exists()`: **a model can be present, the right size, and match a
/// recorded digest while still being unloadable** — a truncated download, or a
/// Git LFS pointer file committed instead of the model. Checking presence
/// alone reports healthy, and the failure surfaces much later inside
/// llama.cpp, where it looks like a different bug.
///
/// Checking the four magic bytes is barely better, and was what `apogee check`
/// did until this landed: a half-finished download has perfectly valid magic,
/// so the row said "model loads" for precisely the file that cannot. Parsing
/// the whole header — every key, every tensor descriptor — is what actually
/// distinguishes the two, and it still costs kilobytes.
///
/// A full load would be stronger still and is deliberately not done: it costs
/// gigabytes of I/O per model, and `check` is something a user runs when
/// something is already wrong.
///
/// ## Robustness is the point
///
/// Every length in a GGUF comes from the file itself, so a corrupt or hostile
/// file can claim a 2^64-byte string. Every read here is bounds-checked against
/// the real file size and every count is capped, because the first thing this
/// code will meet in the wild is a half-downloaded model — which is exactly
/// the case `models repair` exists for and must be able to *diagnose*.
namespace apogee::models {

/// `general.file_type` was absent from the header.
inline constexpr std::uint32_t kUnknownFileType = 0xFFFFFFFFU;

/// What a header read found. `parsed == false` always carries a `parse_error`.
struct GgufInfo {
    /// The header was understood end to end.
    bool parsed = false;

    /// Why not, when `parsed` is false. Never empty in that case — an empty
    /// field where a reason belongs is the reporting failure this struct is
    /// shaped to prevent.
    std::string parse_error;

    /// GGUF container version (3 at time of writing).
    std::uint32_t version = 0;

    /// `general.architecture` — "llama", "qwen35", "gemma3", …. Empty when the
    /// key is absent, which is itself worth reporting.
    std::string architecture;

    /// `general.name`, when present. Often a content hash rather than a
    /// human-readable name, so it is reported and never relied on.
    std::string name;

    /// Total tensors declared in the header.
    std::int64_t tensors = 0;

    /// Tensors belonging to the text model — total minus the vision and
    /// projector tensors a combined multimodal blob carries.
    ///
    /// The difference is the signal: when it is non-zero the file is a combined
    /// text+vision blob, which is the shape Ollama distributes and which older
    /// llama.cpp could not load. `tensors - text_tensors` is what
    /// `models repair` would strip.
    std::int64_t text_tensors = 0;

    /// The file's size on disk, in bytes.
    std::int64_t file_size = 0;

    /// `general.file_type` — llama.cpp's `llama_ftype`, recording how the
    /// tensors are stored. 0 is all-F32, 1 is mostly-F16, 32 is BF16;
    /// everything else is a quantized model. `kUnknownFileType` when absent.
    std::uint32_t file_type = kUnknownFileType;

    /// Whether the weights are already quantized.
    ///
    /// Load-bearing for `models quantize`: llama.cpp **refuses to requantize**,
    /// and its own message ("requantizing from type q8_0 is disabled") arrives
    /// buried in a couple of hundred per-tensor log lines. Knowing up front
    /// turns that into one sentence before anything starts.
    [[nodiscard]] bool is_quantized() const noexcept {
        return parsed && file_type != kUnknownFileType && file_type != 0 && file_type != 1 &&
               file_type != 32;
    }

    /// Whether this file is a standalone multimodal projector (an "mmproj"):
    /// every tensor is a vision tensor and there is no text model at all.
    ///
    /// Distinguished from a combined blob because the two need opposite
    /// messages. A projector is a normal, expected file — it is exactly what
    /// `mmproj_path` wants — and reporting it as a "combined text+vision blob"
    /// (which the first version did) tells a user something is wrong with a
    /// file that is perfectly correct.
    [[nodiscard]] bool is_projector() const noexcept {
        return parsed && tensors > 0 && text_tensors == 0;
    }

    /// Whether this file carries vision tensors **alongside** a text model —
    /// the shape Ollama distributes some models in.
    [[nodiscard]] bool has_vision_tensors() const noexcept {
        return parsed && tensors > text_tensors && text_tensors > 0;
    }
};

/// Reads `path`'s header.
///
/// Never throws and never reports a partial success: any problem — missing
/// file, wrong magic, a length that runs past the end — comes back as
/// `parsed == false` with a reason a user can act on.
[[nodiscard]] GgufInfo inspect_gguf(const std::filesystem::path& path);

}  // namespace apogee::models
