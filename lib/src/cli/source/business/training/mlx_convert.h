#pragma once

#include <cstdint>
#include <filesystem>
#include <functional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
#include "training/python_env.h"
#include "training/trainer.h"

/// `mlx_convert.py`, run under Apogee's own Python environment: a
/// full-weight SafeTensors snapshot in, an MLX model directory out, through
/// `mlx_lm`'s own `convert` (27b) -- or, for a set that sees or hears,
/// `mlx_vlm`'s, its towers kept (53f), for an embedding model,
/// `mlx_embeddings`' (53h), for Wan's original layout, mlx-video's Wan
/// converter (53k), and for a stored voice model, `mlx_audio`'s (53m).
///
/// **A child, never a link** -- the GGUF converter's discipline, a different
/// engine: the driver is seeded beside the trainers, run by the environment's
/// interpreter through the one script runner (its JSONL framed by the one
/// framer, its stderr captured as a tail and folded into a failure, never
/// inherited), and ended when the caller's token fires. What it writes is the
/// caller's to verify and commit: this file never knows the store.
namespace apogee::training {

/// Which converter an MLX conversion runs (53f): mlx-lm's, which builds a
/// language model, mlx-vlm's, which builds a model that sees or hears with
/// its towers kept, or -- since 53h -- mlx-embeddings', which builds an
/// embedding model whole, and -- since 53k -- mlx-video's, which builds Wan's
/// original layout into an MLX video model, and -- since 53m -- mlx-audio's,
/// which builds a voice model as Kokoro publishes it into an MLX voice model.
/// One driver runs each (`--with mlx-vlm`, `--with mlx-embeddings`, `--with
/// mlx-video`, `--with mlx-audio`).
enum class MlxConverterKind : std::uint8_t { MlxLm, MlxVlm, MlxEmbeddings, MlxVideo, MlxAudio };

/// The converter a set's MLX conversion runs: mlx-audio for a voice model
/// (`voice`, the store's `voice` format, 53m); mlx-video for Wan's original
/// layout (`video`, by its own `config.json`'s class, 53k); mlx-vlm for a
/// draft head (`drafter`, by its own `config.json`, 53i), built as the
/// drafter its speculative path runs beside a base; mlx-embeddings for an
/// embedding model (`embedding`, sentence-transformers' by its own
/// `modules.json`, 53h); mlx-vlm for a set whose own files declare towers --
/// kept by default (2026-10-21) -- unless the person asked for mlx-lm's build
/// of its language model alone (`--text-only`); mlx-lm for every other. Never
/// chosen by a name.
[[nodiscard]] MlxConverterKind choose_mlx_converter(bool towers, bool text_only,
                                                    bool embedding = false, bool drafter = false,
                                                    bool video = false,
                                                    bool voice = false) noexcept;

/// Its name as a person says it: `mlx-lm`, `mlx-vlm`, `mlx-embeddings`,
/// `mlx-video`, `mlx-audio`.
[[nodiscard]] std::string_view to_string(MlxConverterKind converter) noexcept;

/// Its Python package, where its types are read from: `mlx_lm`, `mlx_vlm`,
/// `mlx_embeddings`, `mlx_video`, `mlx_audio`.
[[nodiscard]] std::string_view mlx_converter_package(MlxConverterKind converter) noexcept;

/// Its own entry, as a record's transform names it: `mlx_lm.convert`,
/// `mlx_vlm.convert`, `mlx_embeddings.convert`, mlx-video's Wan converter
/// as the pinned commit holds it, `mlx_video.models.wan_2.convert` (53k), and
/// `mlx_audio.convert` (53m).
[[nodiscard]] std::string_view mlx_converter_entry(MlxConverterKind converter) noexcept;

/// The requirement set that installs it: `mlx`, `mlx-vlm`, `mlx-embeddings`,
/// `mlx-video`, `mlx-audio`.
[[nodiscard]] RequirementSet mlx_converter_set(MlxConverterKind converter) noexcept;

/// mlx-video's Wan converter as the environment at `venv` holds it (53k):
/// the module file `mlx_converter_entry` names -- `mlx_video/models/wan_2/
/// convert.py` under site-packages -- with the `torch` it loads Wan's `.pth`
/// checkpoints through beside it. Read from files, never imported (27a's
/// rule); empty when either is not there. What "mlx-video is installed" means
/// to every surface that offers or refuses the conversion.
[[nodiscard]] std::filesystem::path find_mlx_video_converter(const std::filesystem::path& venv);

/// mlx-video's Wan generator, the module the pinned commit holds it in (53l):
/// `mlx_video.models.wan_2.generate`, its entry `generate_video` -- the
/// package's console script `mlx_video.wan_2.generate`, which both READMEs
/// spell as a module, runs that module's `main()`.
inline constexpr std::string_view kMlxVideoGenerator = "mlx_video.models.wan_2.generate";

/// mlx-video's Wan generator as the environment at `venv` holds it (53l): the
/// module file `kMlxVideoGenerator` names, with the `torch` the set brings
/// beside it -- 53k's reading of "installed", asked of the generator. Read
/// from files, never imported; empty when either is not there.
[[nodiscard]] std::filesystem::path find_mlx_video_generator(const std::filesystem::path& venv);

/// mlx-audio's converter (53m): `mlx_audio.convert`, the module its README
/// runs (`python -m mlx_audio.convert`) and whose `convert(hf_path,
/// mlx_path, ...)` the seeded driver calls.
inline constexpr std::string_view kMlxAudioConverter = "mlx_audio.convert";

/// mlx-audio's Kokoro (53m): the package its TTS loader builds a Kokoro
/// directory with -- `mlx_audio.tts.models.kokoro` -- what "builds Kokoro"
/// means, read from its files.
inline constexpr std::string_view kMlxAudioKokoro = "mlx_audio.tts.models.kokoro";

/// mlx-audio's converter as the environment at `venv` holds it, able to
/// build Kokoro (53m): the module file `kMlxAudioConverter` names
/// (`mlx_audio/convert.py` under site-packages), Kokoro's package beside it
/// (`kMlxAudioKokoro`), and the `torch` the driver reads Kokoro's PyTorch
/// checkpoint and voice packs through. Read from files, never imported (27a's
/// rule); empty when any is not there. What "mlx-audio is installed and
/// builds Kokoro" means to every surface that offers or refuses a voice
/// model's MLX conversion.
[[nodiscard]] std::filesystem::path find_mlx_audio_converter(const std::filesystem::path& venv);

/// mlx-audio's Kokoro as the environment at `venv` holds it to speak with
/// (53m): Kokoro's package (`kMlxAudioKokoro`, its directory returned), with
/// the TTS loader beside it (`mlx_audio/tts/utils.py`) and `misaki`, the
/// phonemizer Kokoro reads text through. Read from files, never imported;
/// empty when any is not there.
[[nodiscard]] std::filesystem::path find_mlx_audio_kokoro(const std::filesystem::path& venv);

/// One precision `models convert --mlx` writes, spelled the way a community
/// build's name ends (`-4bit`, `-bf16`): `mlx_lm.convert`'s own choices --
/// bits in its affine scheme with its default group of 64, its `mxfp4`
/// mode, or a floating-point type unquantized -- and nothing it does not
/// expose.
struct MlxPrecision {
    std::string_view name;
    /// Bits a weight; 0 when unquantized.
    int bits = 0;
    /// `mlx-lm`'s quantization mode; empty for unquantized.
    std::string_view mode;
    /// `mlx-lm`'s dtype for an unquantized conversion; empty when quantized.
    std::string_view dtype;
    std::string_view summary;
};

/// Every precision, the default (`4bit`) first.
[[nodiscard]] std::span<const MlxPrecision> mlx_precisions() noexcept;

/// The precision named `name`, or null.
[[nodiscard]] const MlxPrecision* find_mlx_precision(std::string_view name) noexcept;

/// Every precision's name, in order -- for a flag's help and its check.
[[nodiscard]] std::vector<std::string> mlx_precision_names();

/// Whether `converter` writes `precision` (53k): mlx-lm, mlx-vlm,
/// mlx-embeddings and mlx-audio (53m: its `q_mode` takes affine and mxfp4,
/// its `--dtype` bfloat16 and float16) every one; mlx-video's Wan converter 4 or 8 bits in its
/// affine groups (`--quantize --bits`, its group 64) or a floating-point type
/// unquantized (`--dtype`) -- never 3 or 6 bits, nor mxfp4.
[[nodiscard]] bool mlx_converter_writes(MlxConverterKind converter,
                                        const MlxPrecision& precision) noexcept;

/// The precisions `converter` writes, by name, in the table's order: what
/// `--type` offers and takes on its route.
[[nodiscard]] std::vector<std::string> mlx_precision_names(MlxConverterKind converter);

/// Where the seeded driver lives: `training/scripts/mlx_convert.py`.
[[nodiscard]] std::filesystem::path mlx_converter_script();

/// The driver's arguments: `--hf-path <input> --mlx-path <output>`, then
/// `--q-bits`, `--q-group-size` and `--q-mode` for a quantized precision, or
/// `--dtype` for one that is not -- the one table mapped for every converter.
/// mlx-vlm's route leads with `--with mlx-vlm`, mlx-embeddings' with `--with
/// mlx-embeddings` (53h), mlx-video's with `--with mlx-video` (53k),
/// mlx-audio's with `--with mlx-audio` (53m); mlx-lm's is exactly what it was
/// before there were two.
[[nodiscard]] std::vector<std::string> mlx_converter_arguments(
    const MlxPrecision& precision, const std::filesystem::path& input,
    const std::filesystem::path& output, MlxConverterKind converter = MlxConverterKind::MlxLm);

/// Writes the MLX directory `output` from the snapshot `input`; the error, or
/// empty -- "cancelled" when the token fired.
using MlxConverter = std::function<std::string(
    const std::filesystem::path& input, const std::filesystem::path& output,
    const MessageSink& on_message, const harness::CancellationToken& cancellation)>;

/// The driver under `interpreter`, as an MlxConverter. Its `{"message"}`
/// lines go to the sink; its stderr is captured as a tail and folded into
/// the error, never inherited.
///
/// `package` is the converter's installed package directory (M15): when a
/// conversion fails because it knows no model for the snapshot's
/// `model_type`, its own lists of the types it does are read from there to
/// name the way out (`fold_model_type_failure`). Empty: the door alone.
/// `converter` picks the route (53f); a seeded driver older than the
/// route asked for is said as such, with `apogee check --fix` named.
[[nodiscard]] MlxConverter script_mlx_converter(
    std::filesystem::path interpreter, std::filesystem::path script, const MlxPrecision& precision,
    std::filesystem::path package = {}, MlxConverterKind converter = MlxConverterKind::MlxLm);

/// The model types an installed converter builds (M15), read from its files,
/// never imported: mlx-lm's modules under `models/` that define a `Model`
/// (mlx-embeddings' are the same, 53h), mlx-vlm's packages there whose
/// `__init__.py` names one (53f: its loader takes `models.<type>.Model`), and
/// every key of `utils.py`'s `MODEL_REMAPPING`, which all three keep. Sorted, each once; empty when
/// there is no such directory. Apogee keeps no table of its own: it would rot with every release
/// and every vendor's variant.
[[nodiscard]] std::vector<std::string> mlx_model_types(const std::filesystem::path& package);

/// The type in an mlx-lm failure saying `Model type <type> not supported`, or
/// empty.
[[nodiscard]] std::string unsupported_model_type(std::string_view error);

/// `types` nearest `wanted` first -- by the prefix they share, longest
/// first, then the shorter name -- at most `most`, none sharing fewer than
/// three characters.
[[nodiscard]] std::vector<std::string> nearest_model_types(std::string_view wanted,
                                                           const std::vector<std::string>& types,
                                                           std::size_t most = 5);

/// `error`, and when it is the converter refusing a `model_type`, its door:
/// the `--model-type` override named, and the nearest of `types` --
/// suggested, never applied.
[[nodiscard]] std::string fold_model_type_failure(
    const std::string& error, const std::vector<std::string>& types,
    MlxConverterKind converter = MlxConverterKind::MlxLm);

}  // namespace apogee::training
