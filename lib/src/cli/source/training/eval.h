#pragma once

#include <nlohmann/json_fwd.hpp>

#include <filesystem>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

#include "harness/cancellation.h"
#include "training/kit.h"
#include "training/trainer.h"

/// The eval gate: a suite of `{prompt, expected?}` items scored against a
/// trained adapter through its candidate runner.
///
/// An item WITH `expected` is a deterministic substring check. One WITHOUT
/// is a **pairwise judge** comparison of the candidate against its own
/// untuned base through the same runner -- the question promotion asks (did
/// the adapter help?), not the reference implementation's comparison
/// against whatever `models.default` was. The judge answers `A`, `B` or
/// `TIE`; anything else is a tie, and a judge or baseline failure is a tie
/// too, under the **never-fail contract** the rerank judge keeps: a broken
/// judge costs nothing but the signal. With no judge such items `skip` and
/// auto-pass -- loudly, with the count, so nobody mistakes an ungated suite
/// for a passed one.
///
/// **The gate is 100%.** `score` is reported; `passed` is true only when
/// every item passed.
namespace apogee::training {

/// The judge's generation budget. The reference used 10 -- enough for one
/// word -- but a REASONING judge spends its budget thinking before the word
/// appears, and under the never-fail contract a verdict that never arrives
/// is a tie, which passes: a small budget would silently ungate every judged
/// item on such a model. The rerank judge found the same live (gpt-oss at
/// 256 never reached its final channel).
inline constexpr int kJudgeMaxTokens = 1024;

struct EvalItemResult {
    std::string prompt;
    std::string expected;
    std::string got;
    /// `contains`, `judge`, or `skip`.
    std::string check_type;
    bool passed = false;
    /// Judge items only.
    std::string baseline_got;
    /// `candidate`, `baseline`, or `tie`.
    std::string judge_winner;
};

struct EvalResults {
    bool passed = false;
    /// `num_passed / total`.
    double score = 0.0;
    int total = 0;
    int num_passed = 0;
    int num_skipped = 0;
    std::vector<EvalItemResult> items;
    std::string judge_backend;
    /// Where the items came from, for the record.
    std::string suite;
    std::string run_at;
};

[[nodiscard]] nlohmann::json eval_results_to_json(const EvalResults& results);
/// Throws std::runtime_error on a wrong shape.
[[nodiscard]] EvalResults eval_results_from_json(const nlohmann::json& json);

/// Reads a `{prompt, expected?}` JSONL suite: blank lines skipped, a line
/// that is not a JSON object named with its number, an item with no prompt
/// skipped. Empty with `error` set when the file cannot be read or holds no
/// item.
[[nodiscard]] std::vector<EvalItem> load_eval_suite(const std::filesystem::path& path,
                                                    std::string& error);

struct JudgeReply {
    bool ok = false;
    std::string text;
    std::string error;
};

/// One judge call: the prompt and the two answers, the reply's text. The
/// backend arrives as this closure so the package never names one.
using JudgeFn = std::function<JudgeReply(std::string_view prompt, std::string_view candidate,
                                         std::string_view baseline,
                                         const harness::CancellationToken& cancellation)>;

/// The fixed judge prompt; the candidate is always "Response A".
[[nodiscard]] std::string judge_prompt(std::string_view prompt, std::string_view candidate,
                                       std::string_view baseline);

/// `candidate` for a reply starting with `A`, `baseline` for `B`, else
/// `tie` -- case-insensitively, whitespace trimmed.
[[nodiscard]] std::string parse_judge_verdict(std::string_view reply);

struct EvalOptions {
    /// Recorded in the results; empty means no judge.
    std::string judge_backend;
    JudgeFn judge;
    /// The untuned base through the same runner; null means judge items
    /// skip.
    CandidateRunner* baseline = nullptr;
    std::function<void(int done, int total)> on_progress;
    harness::CancellationToken cancellation;
};

struct EvalRun {
    bool ok = false;
    bool cancelled = false;
    /// A candidate failure aborts the eval: a gate that cannot ask the
    /// candidate has nothing to say about it.
    std::string error;
    EvalResults results;
};

[[nodiscard]] EvalRun run_eval(const std::vector<EvalItem>& items, CandidateRunner& candidate,
                               const EvalOptions& options);

/// The current time as `YYYY-MM-DDTHH:MM:SSZ`, for every record here.
[[nodiscard]] std::string rfc3339_now();

}  // namespace apogee::training
