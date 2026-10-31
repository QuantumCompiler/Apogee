#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
#include "modelstore/store.h"
#include "modelstore/voice_info.h"
#include "training/script_runner.h"
#include "video/runtime.h"
#include "video/voice.h"

/// `apogee voice say` composed (50a): the text checked against the caps,
/// the voice model and voice resolved, the runtime made ready, the text
/// spoken into one WAV, and its record -- the `voice.speech` document,
/// printed under `--output-format json` and written beside a file that
/// landed as `<file>.json`. The composer (50c) speaks its narration through
/// the same resolution and the same core. Since 53m on either engine: a voice
/// model as Kokoro publishes it on PyTorch, mlx-audio's MLX build of one on
/// MLX -- the model's directory says which, and the record says `engine`.
namespace apogee::commands {

/// The most text one run speaks.
inline constexpr std::size_t kMaxVoiceCharacters = 4000;
/// The speeds a voice may speak at, and the default.
inline constexpr double kMinVoiceSpeed = 0.5;
inline constexpr double kMaxVoiceSpeed = 2.0;
/// The voice spoken when none is named and the model has it: Kokoro's own
/// recommendation; else the model's first voice.
inline constexpr std::string_view kDefaultVoice = "af_heart";

struct VoiceSayOptions {
    std::string text;
    /// `--model`: a stored voice model by handle, id or model, or a path.
    std::string model{};
    std::string voice{};
    std::optional<double> speed{};
    std::filesystem::path output{};
    /// `--engine` (53m): `auto`, `torch` or `mlx`, or empty when not given.
    /// It only names the engine the model's directory says, or is refused.
    std::string engine{};
};

/// The voice model a run speaks with, resolved and read: a voice model as
/// Kokoro publishes it, or since 53m an MLX voice model -- mlx-audio's build
/// of one, its voices the presets that build carries.
struct ResolvedVoiceModel {
    std::string handle{};
    std::filesystem::path dir{};
    std::string ref{};
    std::string revision{};
    models::VoiceInfo info{};
    /// What its directory speaks on (53m, `video::voice_engine_of_directory`).
    video::VideoEngine engine = video::VideoEngine::Torch;
};

/// `given`, else the store's one voice model: a handle, a bare id, a model
/// holding exactly one, or a path -- a voice model as Kokoro publishes it,
/// or since 53m an MLX directory whose kind is `voice`, counted among the
/// store's voice models as one. The refusal names what to do instead -- with
/// several in the store, the verb's own flag for one, `flag` (`--model` for
/// `voice say`, `--voice-model` for `video compose`, M29); empty means `out`
/// is set.
[[nodiscard]] std::string resolve_voice_model(const models::StoreRoots& roots,
                                              std::string_view given, ResolvedVoiceModel& out,
                                              std::string_view flag = "--model");

/// The voice to speak with: `given` when the model has it -- refused,
/// naming its voices, when not -- else `kDefaultVoice`, else the first.
[[nodiscard]] std::string choose_voice(const models::VoiceInfo& info, std::string_view given,
                                       std::string& out);

/// What a run needs from outside.
struct VoiceSaySeams {
    /// The ladder for the resolved model on its engine -- in the command, on
    /// PyTorch the consent to install the runtime first; on MLX the ladder
    /// alone (53m).
    std::function<video::VideoReadiness(const ResolvedVoiceModel&)> ready;
    training::Spawner spawn = training::default_spawner();
    video::VoiceEventSink on_event{};
    std::function<void(const std::string&)> say{};
    /// `cache/video`.
    std::filesystem::path work_root{};
    std::string platform{};
};

struct VoiceSayResult {
    int exit_code = 0;
    /// The `voice.speech` document -- null when refused before it started.
    nlohmann::json document;
    std::vector<std::string> lines;
    std::string error;
};

/// One `voice say`, composed: refused before anything runs on an empty or
/// overlong text, a speed outside the caps, an existing output or a missing
/// directory, no voice model, an unknown voice, an engine asked for that is
/// not the model's (53m) or the ladder; else one run on the model's engine,
/// its record written beside the file on success.
[[nodiscard]] VoiceSayResult say_text(const models::StoreRoots& roots,
                                      const VoiceSayOptions& options, const VoiceSaySeams& seams,
                                      const harness::CancellationToken& cancellation);

/// The line the narration says for `event`.
[[nodiscard]] std::optional<std::string> narrate_voice_event(const video::VoiceEvent& event);

}  // namespace apogee::commands
