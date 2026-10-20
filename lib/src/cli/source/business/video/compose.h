#pragma once

#include <nlohmann/json.hpp>

#include <cstdint>
#include <filesystem>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/cancellation.h"
#include "contracts/captions.h"
#include "contracts/video_project.h"
#include "platform/ffmpeg.h"

/// The composer (50c): a brief into a narrated, captioned video.
///
/// One structured member call plans the scenes -- each a narration line, a
/// visual prompt and a length, held to a schema with the caps in it. Then
/// every scene's narration is spoken through the voice core (cheap, and the
/// proof each fits its scene before anything expensive runs), every scene's
/// clip renders through the clip core -- or takes the user's own asset --
/// and the seam assembles them: joined with crossfades, the narration laid
/// at each scene's start over an optional ducked bed, the captions burned.
///
/// **Captions are arithmetic, never transcription.** The script is the
/// plan's own text and every sentence's length was counted from its samples
/// (50a), so each cue is a sentence at its scene's start plus its offset.
///
/// **The project document is the only state** (`contracts/video_project`):
/// saved after every step, so a stopped run renders again only what its
/// document does not hold. Every model reaches this as a closure: the plan
/// call, the narration and the clip -- the composer names no backend.
namespace apogee::video {

// --- the caps, quoted in the plan's schema and every refusal that meets one ----------

/// The most scenes one composition has.
inline constexpr std::size_t kMaxComposeScenes = 8;
/// A scene's length: at least this, at most one clip run's.
inline constexpr double kMinSceneSeconds = 2.0;
inline constexpr double kMaxSceneSeconds = 10.0;
/// A scene's narration and visual prompt, at most.
inline constexpr std::size_t kMaxNarrationCharacters = 240;
inline constexpr std::size_t kMaxVisualCharacters = 600;
/// The brief, at most.
inline constexpr std::size_t kMaxBriefCharacters = 2000;
/// A scene's narration starts this far into it, and ends at least this
/// before the next scene's fade begins.
inline constexpr double kNarrationLead = 0.25;
inline constexpr double kNarrationRoom = 0.25;

/// The plan's schema, as text: each scene's properties in the order a held
/// answer writes them -- the narration, the visual it shows, its length, the
/// asset. With `assets`, a scene's `asset` is one of their names or null, so
/// an asset the directory lacks fails the plan's validation; without, a
/// scene names none.
[[nodiscard]] std::string plan_schema(const std::vector<std::string>& assets);

/// What the planner is told, and asked.
[[nodiscard]] std::string plan_system_prompt();
[[nodiscard]] std::string plan_user_message(std::string_view brief,
                                            const std::vector<std::string>& assets);

/// What the plan call gave: the answer parsed, whether it held to the
/// schema, the turns it took, and why not.
struct PlanAnswer {
    std::optional<nlohmann::json> json{};
    bool conforms = false;
    int attempts = 0;
    std::string error{};
};

/// The structured member call: the system prompt, the user message and the
/// schema's text in; the answer out.
using PlanFn = std::function<PlanAnswer(const std::string& system, const std::string& user,
                                        const std::string& schema)>;

/// The plan, read.
struct PlannedScenes {
    std::string title;
    std::vector<harness::ProjectScene> scenes;
    int attempts = 0;
    /// Why there is no plan; empty when there is one.
    std::string error;
};

/// Plans `brief` through `plan`: one call, the schema held with its one
/// correction, an answer that still does not hold refused with the caps
/// quoted.
[[nodiscard]] PlannedScenes plan_scenes(std::string_view brief,
                                        const std::vector<std::string>& assets, const PlanFn& plan);

/// The files `dir` offers the plan, by name: stills (`png`, `jpg`, `jpeg`,
/// `webp`) and clips (`mp4`, `mov`, `m4v`, `webm`), sorted.
[[nodiscard]] std::vector<std::string> list_assets(const std::filesystem::path& dir);

/// Whether an asset's name is a still's.
[[nodiscard]] bool is_still(std::string_view name);

/// What a scene's narration needs of its clip: the lead, the speech, the
/// room after it and the next scene's fade.
[[nodiscard]] double narration_needs(double spoken, double crossfade);

/// A scene's clip length: the plan's, stretched to hold its narration, at
/// least `kMinSceneSeconds` -- nullopt when the narration needs more than
/// `kMaxSceneSeconds`.
[[nodiscard]] std::optional<double> scene_length(double planned, double spoken, double crossfade);

/// Where each scene starts in the joined video: the clips' measured lengths
/// summed, less a crossfade at each boundary.
[[nodiscard]] std::vector<double> scene_starts(const harness::VideoProject& project);

/// The captions: each sentence of each scene's narration one cue, from its
/// scene's start, the lead and its offset, for its counted seconds.
[[nodiscard]] std::vector<harness::CaptionCue> caption_cues(const harness::VideoProject& project);

/// The narration as the mix lays it: each scene's WAV at its start and lead.
[[nodiscard]] std::vector<platform::NarrationPiece> narration_pieces(
    const harness::VideoProject& project);

/// One scene's narration as the voice core spoke it.
struct SpokenScene {
    bool ok = false;
    bool cancelled = false;
    std::string error{};
    /// Measured from the WAV.
    double seconds = 0.0;
    std::vector<harness::ProjectSentence> sentences{};
};

/// What one scene's clip run is asked for.
struct ClipAsk {
    /// The scene, from 1.
    std::size_t scene = 0;
    std::string prompt;
    double seconds = 0.0;
    std::int64_t seed = 0;
    /// A still to start from (an asset); empty for text-to-video.
    std::filesystem::path image{};
};

struct RenderedClip {
    bool ok = false;
    bool cancelled = false;
    std::string error{};
};

/// What a composition needs from outside.
struct ComposeSeams {
    /// One scene's narration spoken into `wav` (its record beside it).
    std::function<SpokenScene(const std::string& text, const std::filesystem::path& wav)> speak;
    /// One scene's clip rendered into `mp4` (its record beside it).
    std::function<RenderedClip(const ClipAsk& ask, const std::filesystem::path& mp4)> render;
    /// The seam's own probe and assembly, bounded and stopped by the run's
    /// cancellation; a test hands in its own.
    std::function<std::optional<platform::MediaInfo>(const std::filesystem::path&,
                                                     const platform::FfmpegLimits&, std::string&)>
        probe = platform::probe_media;
    std::function<std::optional<platform::Assembled>(const std::vector<std::filesystem::path>&,
                                                     double, const std::filesystem::path&,
                                                     const platform::FfmpegLimits&, std::string&)>
        join = platform::concat_clips;
    std::function<std::optional<platform::Assembled>(
        const std::filesystem::path&, const platform::AudioMix&, const std::filesystem::path&,
        const platform::FfmpegLimits&, std::string&)>
        mix = platform::mix_audio;
    std::function<std::optional<platform::Assembled>(
        const std::filesystem::path&, const std::filesystem::path&, const std::filesystem::path&,
        const platform::FfmpegLimits&, std::string&)>
        burn = platform::burn_captions;
    /// Whether this ffmpeg can burn captions (`subtitles`, libass); when
    /// not, the captions are kept beside the video as an SRT.
    bool can_burn = true;
    /// A line the run says as it goes.
    std::function<void(const std::string&)> say{};
    /// A scene begun (52b), beside the line `say` gets for it: `scene` of
    /// `scenes`, from 1, at `speaking` (its narration), `rendering` (its
    /// clip) or `asset` (its asset taken as it is). Unset says nothing.
    std::function<void(std::size_t scene, std::size_t scenes, std::string_view state)> on_scene{};
    /// The project as it stands, after every step: the caller writes it.
    std::function<void(const harness::VideoProject&)> save{};
};

struct ComposeOutcome {
    /// The video is at the project's output.
    bool ok = false;
    bool cancelled = false;
    std::string error;
};

/// Renders `project` from wherever it stands. Every scene's narration first
/// -- a scene whose narration needs more than the longest scene is refused
/// there, before any clip runs, and so is an asset clip too short for its
/// narration -- then every clip, an existing one kept when it is long enough
/// for its narration, then the captions and the assembly, the output touched
/// once, by a rename, at the end. A file a scene's document does not record
/// is a stopped run's leftover and is replaced. `project` is updated and
/// saved after each step; on a failure or a stop its status says which, and
/// every finished scene stays.
[[nodiscard]] ComposeOutcome render_project(harness::VideoProject& project,
                                            const ComposeSeams& seams,
                                            const harness::CancellationToken& cancellation);

// --- editing a project (50d) ------------------------------------------------------------
//
// An edit is a document rewrite plus record comparison: one structured pass
// rewrites the plan -- each scene naming the scene it came `from`, or null
// for a new one -- and a scene renders again exactly where its record
// changed. Its narration speaks again when its text changed (or the voice
// did); its clip renders again when its prompt, length, asset or seed
// changed (or the pipeline did), its seed held unless re-rolled. Everything
// else is carried over byte for byte.

/// The edit's schema: the plan's, each scene first naming the scene it came
/// `from` -- 1 to `scenes`, or null for a new one.
[[nodiscard]] std::string edit_schema(const std::vector<std::string>& assets, std::size_t scenes);

/// What the editor is told, and asked: the brief, the plan as it stands
/// (each scene numbered as its `from`), and the instruction.
[[nodiscard]] std::string edit_system_prompt();
[[nodiscard]] std::string edit_user_message(const harness::VideoProject& project,
                                            std::string_view instruction,
                                            const std::vector<std::string>& assets);

/// What became of one scene of the edited plan.
struct SceneChange {
    /// The parent's scene it came from, from 1; 0 for a new one.
    std::size_t from = 0;
    bool narration = false;
    bool visual = false;
    bool seconds = false;
    bool asset = false;
    /// Its seed re-rolled (`--redo-scene`).
    bool reseeded = false;
    /// Out of the parent's order.
    bool moved = false;

    [[nodiscard]] bool untouched() const noexcept {
        return !narration && !visual && !seconds && !asset && !reseeded;
    }
};

/// The plan after an edit: the scenes, what became of each, and the parent's
/// scenes no longer in it.
struct EditedScenes {
    std::string title;
    std::vector<harness::ProjectScene> scenes;
    std::vector<SceneChange> changes;
    /// The parent's scenes removed, from 1.
    std::vector<std::size_t> removed;
    int attempts = 0;
    /// Why the edit changed nothing; empty when it holds.
    std::string error;
};

/// The parent's plan through one edit pass on `edit`: refused -- nothing
/// changed -- when the answer does not hold to the schema; else each scene
/// compared with the one it came from, its seed held, a new scene's drawn
/// from `seed`.
[[nodiscard]] EditedScenes edit_scenes(const harness::VideoProject& parent,
                                       std::string_view instruction,
                                       const std::vector<std::string>& assets, const PlanFn& edit,
                                       const std::function<std::int64_t()>& seed);

/// The parent's plan with no edit: every scene untouched.
[[nodiscard]] EditedScenes same_scenes(const harness::VideoProject& parent);

/// Re-rolls `scene`'s seed (from 1) to `seed`; the refusal when there is no
/// such scene.
[[nodiscard]] std::string redo_scene(EditedScenes& edited, std::size_t scene, std::int64_t seed);

/// Which of a scene's records differ from the scene it came from, in the
/// diff's order -- `narration`, `prompt`, `duration`, `asset`, `seed` (52c):
/// the one reading both the said diff and the document's `changes` take.
[[nodiscard]] std::vector<std::string> changed_records(const SceneChange& change);

/// The diff, scene by scene, in words -- said before anything renders.
[[nodiscard]] std::vector<std::string> diff_lines(const EditedScenes& edited,
                                                  std::size_t parent_scenes);

/// The edit's comparison written onto `project`, whose scenes are
/// `edited`'s (52c): each scene's `from`, `changes` and `moved`, and the
/// parent's scenes `removed` -- the facts `diff_lines` says, as data.
void record_provenance(harness::VideoProject& project, const EditedScenes& edited);

/// Carries what each edited scene keeps from its parent into
/// `scenes_dir`, copied byte for byte under its new number: its narration
/// when its text is unchanged and `same_voice`, its clip when its prompt,
/// length, asset and seed are unchanged and `same_pipeline`. The error, or
/// empty.
[[nodiscard]] std::string carry_scenes(const harness::VideoProject& parent, EditedScenes& edited,
                                       const std::filesystem::path& scenes_dir, bool same_voice,
                                       bool same_pipeline);

}  // namespace apogee::video
