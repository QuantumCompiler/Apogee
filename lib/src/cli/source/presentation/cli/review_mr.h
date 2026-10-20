#pragma once

#include <nlohmann/json.hpp>

#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "forge/forge.h"
#include "review/engine.h"

/// `apogee review mr <ref>` composed (48c): the merge request fetched
/// through the forge seam, what this login posted on it before read back,
/// the change planned, hunted, filtered, deduplicated and confirmed by the
/// review core, and the findings posted as one batch -- through the gate,
/// never past it -- or, under `--dry-run`, printed and posted nowhere. The
/// run's record is one document either way.
///
/// Every outside thing arrives as a seam -- the forge client, the model
/// passes, the gate, the narration -- so the command wires the real ones and
/// a test hands in its own; nothing here builds a provider or spawns a CLI.
namespace apogee::commands {

struct ReviewMrOptions {
    std::string ref;
    /// The merge request already parsed (a sweep's listing): used over
    /// `ref` when set.
    std::optional<forge::MergeRequestRef> parsed;
    /// `--skills`: run exactly these, the plan's hints aside.
    std::vector<std::string> skills;
    bool dry_run = false;
    /// The backend the passes ran on, for the record.
    std::string model;
};

/// Whether posting `comments` comments (and a summary) to `target` may go
/// ahead -- the gate's answer. `why_not` says why when it may not.
using PostGate =
    std::function<bool(std::string_view target, std::size_t comments, std::string& why_not)>;

struct ReviewMrSeams {
    /// The client for a forge: `gh` for GitHub, `glab` for GitLab.
    std::function<std::unique_ptr<forge::ForgeClient>(forge::Forge forge)> forge;
    /// The review's model passes (the hunts and the confirm).
    review::PassFn pass;
    /// The plan's narrowing pass, on the utility member; null when none is
    /// named, so the hints stand alone.
    review::PassFn narrow;
    PostGate gate;
    review::ProgressFn progress;
    /// The data directory's `review-skills/`: the book and the exclusions.
    std::filesystem::path skills_dir;
    std::size_t chunk_tokens = review::kDefaultChunkTokens;
};

struct ReviewMrResult {
    /// `{"object": "review.run", ...}` -- machine-mode.md.
    nlohmann::json document;
    /// What the person at the terminal reads.
    std::vector<std::string> lines;
};

/// Runs one review. Throws `forge::ForgeError` for a refusal on the forge's
/// side, `std::runtime_error` for a bad reference or an unknown skill.
[[nodiscard]] ReviewMrResult review_merge_request(const ReviewMrOptions& options,
                                                  const ReviewMrSeams& seams);

}  // namespace apogee::commands
