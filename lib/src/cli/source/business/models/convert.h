#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

#include "contracts/cancellation.h"
#include "modelstore/gguf_inspect.h"
#include "modelstore/mlx_info.h"

/// A SafeTensors snapshot turned into a GGUF -- `apogee models convert`.
///
/// The same ladder a download climbs (`acquire.h`), for the same reason: a
/// conversion runs for minutes and writes tens of gigabytes, and a half-written
/// or unreadable GGUF must never sit under the name a user will point a backend
/// at. The converter itself arrives as a closure, so this file never knows it
/// is a Python script (`training/convert.h` does) and the ladder is tested with
/// no Python at all.
namespace apogee::models {

/// Writes a GGUF at `gguf` from the snapshot directory. Returns the error, or
/// empty on success; "cancelled" when the token fired.
using ConvertFn = std::function<std::string(const std::filesystem::path& snapshot,
                                            const std::filesystem::path& gguf,
                                            const harness::CancellationToken& cancellation)>;

struct ConvertResult {
    bool ok = false;
    bool cancelled = false;
    /// Why not. Always set when `ok` is false.
    std::string error;
    /// The GGUF, once committed.
    std::filesystem::path path;
    /// What its header says -- architecture, tensor count, size.
    GgufInfo info;
};

/// Where a conversion writes while it runs, so a surface can watch it grow.
[[nodiscard]] std::filesystem::path conversion_partial_path(const std::filesystem::path& output);

/// Roughly what a GGUF of `elements` tensor elements weighs at `out_type`, in
/// bytes: two per element at 16 bits, four at f32, 34 per 32 at q8_0. An
/// estimate for a progress line -- the converter keeps a few small tensors at
/// f32 and adds a header -- never a check.
[[nodiscard]] std::int64_t estimated_gguf_bytes(std::int64_t elements, std::string_view out_type);

/// What a snapshot's `config.json` says the model perceives beyond text:
/// a vision encoder (`vision_config`), an audio one (`audio_config`,
/// `whisper_config`) -- the keys llama.cpp's converter reads to build a
/// projector. A config that declares itself `language_model_only` has none.
struct Encoders {
    bool vision = false;
    bool audio = false;

    [[nodiscard]] bool any() const noexcept {
        return vision || audio;
    }

    /// "images", "audio", or "images and audio" -- what a projector lets the
    /// model read, for a sentence.
    [[nodiscard]] std::string reads() const;
};

/// Nothing when `config.json` cannot be read: an encoder is claimed only
/// when the model's own configuration says so.
[[nodiscard]] Encoders snapshot_encoders(const std::filesystem::path& snapshot);

/// Whether the snapshot carries a chat template -- `chat_template.jinja`,
/// `chat_template.json`, or a `chat_template` in `tokenizer_config.json`.
/// One without is a base model: it continues text rather than answering, so
/// chatting with its GGUF produces a ramble, not a reply (Gemma 4 12B,
/// 2026-09-23). Worth a note when it is converted; never a refusal -- a base
/// model is what fine-tuning starts from.
[[nodiscard]] bool snapshot_has_chat_template(const std::filesystem::path& snapshot);

/// Whether a SafeTensors tensor belongs to a vision or audio encoder, by the
/// names Hugging Face checkpoints use (`model.visual.`, `vision_tower.`,
/// `multi_modal_projector.`, `audio_tower.` ...). For splitting a size
/// estimate between the model and its projector -- never a check.
[[nodiscard]] bool is_encoder_tensor(std::string_view name) noexcept;

/// Why rungs 1 and 2 below refuse, or empty. Separate so a surface can refuse
/// BEFORE announcing a conversion or checking for Python: a refusal that
/// arrives after "converting ..." reads as a crash.
[[nodiscard]] std::string conversion_refusal(const std::filesystem::path& snapshot,
                                             const std::filesystem::path& output);

/// The ladder, in order:
///   1. `snapshot` is one (`config.json` and a `*.safetensors` shard), and
///      its `config.json` is the model's rather than a download record
///   2. nothing exists at `output` -- a conversion never replaces a file
///   3. convert into `<output>.partial`
///   4. the result parses as a GGUF
///   5. rename it into place
/// A failure at any rung, cancellation included, removes the partial.
[[nodiscard]] ConvertResult convert_snapshot(const std::filesystem::path& snapshot,
                                             const std::filesystem::path& output,
                                             const ConvertFn& convert,
                                             const harness::CancellationToken& cancellation);

// --- into MLX (27b) --------------------------------------------------------------------

/// Writes an MLX model directory at `out` from the snapshot directory --
/// `mlx_lm.convert` in the owned environment, or a test's stand-in. The
/// error, or empty; "cancelled" when the token fired.
using MlxConvertFn = std::function<std::string(const std::filesystem::path& snapshot,
                                               const std::filesystem::path& out,
                                               const harness::CancellationToken& cancellation)>;

struct MlxConvertResult {
    bool ok = false;
    bool cancelled = false;
    /// Why not. Always set when `ok` is false.
    std::string error;
    /// What the finished directory says: its window, its quantization, its
    /// shards and size.
    MlxInfo info;
};

/// Roughly what an MLX model of `elements` weighs at `bits` a weight -- a
/// 16-bit scale and bias for every group of 64 on top -- or at two bytes an
/// element unquantized. An estimate for a progress line, never a check.
[[nodiscard]] std::int64_t estimated_mlx_bytes(std::int64_t elements, int bits);

/// Why the MLX ladder's first rungs refuse, or empty -- before anything is
/// announced: not a snapshot, a damaged one, one already quantized by
/// `mlx-lm` (an MLX model runs as it is), or something already at `out`.
[[nodiscard]] std::string mlx_conversion_refusal(const std::filesystem::path& snapshot,
                                                 const std::filesystem::path& out);

/// The ladder, in order:
///   1. `snapshot` is one, intact, and not already quantized by `mlx-lm`
///   2. nothing exists at `out` -- the converter writes the directory itself
///      and a conversion never replaces anything
///   3. convert into `out`
///   4. what landed reads as a whole MLX model (`read_mlx_info`), quantized
///      to `bits` when `bits` is set -- a directory that cannot load, or one
///      the converter did not quantize as asked, is never committed
/// A failure at any rung, cancellation included, removes `out`. Committing
/// it under its id is the store's (`commit_mlx`).
[[nodiscard]] MlxConvertResult convert_snapshot_to_mlx(
    const std::filesystem::path& snapshot, const std::filesystem::path& out, int bits,
    const MlxConvertFn& convert, const harness::CancellationToken& cancellation);

}  // namespace apogee::models
