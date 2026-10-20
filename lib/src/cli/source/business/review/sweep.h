#pragma once

#include <nlohmann/json.hpp>

#include <cstddef>
#include <filesystem>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/config.h"
#include "forge/forge.h"
#include "review/engine.h"

/// The review sweep (48d): the watch, without a daemon.
///
/// One shot over the configured targets: each repository -- or each of an
/// owner's, listed at sweep time -- asked through the seam for its open
/// merge requests updated since its watermark, oldest first; each reviewed
/// (a `review mr` run, re-review rules and all), its watermark advanced and
/// saved at once, so a crash resumes exactly where it stopped; then the
/// sweep exits. The user's cron or CI is the cadence. A target seen for the
/// first time starts at now -- history stays unreviewed unless `--since`
/// asks -- drafts are skipped unless the target counts them, and past the
/// cap the rest wait for the next sweep, counted and said.
///
/// Posting is the target's declared grant or the run's `--allow-post`;
/// without either a merge request is reviewed into the document and nothing
/// is posted, said. This core only passes the grant on: the command's gate
/// decides.
namespace apogee::review {

/// The most merge requests one sweep reviews; the rest wait, counted.
inline constexpr std::size_t kMaxMergeRequestsPerSweep = 20;

/// A configured target, by its name.
struct SweepTarget {
    std::string name;
    harness::ReviewTargetConfig config;
};

/// What a sweep keeps between runs: per repository, the update time of the
/// newest merge request handled. One JSON object in a file, rewritten
/// atomically at every change.
class WatermarkStore {
public:
    /// Reads `path`; a missing file is an empty store. Throws
    /// `std::runtime_error` for a file that is not the store's.
    explicit WatermarkStore(std::filesystem::path path);

    [[nodiscard]] std::optional<std::string> get(const std::string& key) const;
    /// Sets and saves at once.
    void set(const std::string& key, std::string value);

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
    std::map<std::string, std::string> marks_;
};

/// A repository's key in the store: `github.com/owner/repo`.
[[nodiscard]] std::string watermark_key(const forge::ForgeProject& project);

/// The seam project a target names (its owner, for an owner target).
[[nodiscard]] forge::ForgeProject target_project(const harness::ReviewTargetConfig& target);

struct SweepOptions {
    /// `--target`: only these, by name; empty for every target.
    std::vector<std::string> only;
    /// `--since`: where a target seen for the first time starts.
    std::optional<std::string> since;
    /// `--allow-post`: the run grants posting everywhere it sweeps.
    bool allow_post = false;
    bool dry_run = false;
    std::size_t cap = kMaxMergeRequestsPerSweep;
    /// Now, ISO 8601 in UTC: where a first sweep starts without `--since`.
    std::string now;
};

/// The client for a target's forge.
using ClientFor =
    std::function<std::unique_ptr<forge::ForgeClient>(const harness::ReviewTargetConfig& target)>;

/// Reviews one merge request for `target`, `post` its grant, and returns its
/// `review.run` document. Throws for a review that could not be done.
using ReviewOne = std::function<nlohmann::json(const forge::MergeRequestRef& ref,
                                               const SweepTarget& target, bool post)>;

/// One sweep. Returns its `review.sweep` document. Throws
/// `std::runtime_error` for a `--target` name the config does not hold.
[[nodiscard]] nlohmann::json run_sweep(const std::vector<SweepTarget>& targets,
                                       WatermarkStore& store, const SweepOptions& options,
                                       const ClientFor& client_for, const ReviewOne& review_one,
                                       const ProgressFn& progress = nullptr);

}  // namespace apogee::review
