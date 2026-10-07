#include "contracts/symphony_walk.h"

#include <algorithm>
#include <cctype>
#include <limits>
#include <map>
#include <optional>
#include <utility>

namespace apogee::harness {
namespace {

std::string folded(std::string_view name) {
    std::string out{name};
    std::ranges::transform(out, out.begin(), [](char c) {
        return static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    });
    return out;
}

/// `a + b`, held at the largest count rather than wrapped: a walk of a
/// wide, deep chain is counted, never overflowed.
std::int64_t saturating_add(std::int64_t a, std::int64_t b) noexcept {
    return a > std::numeric_limits<std::int64_t>::max() - b
               ? std::numeric_limits<std::int64_t>::max()
               : a + b;
}

/// What one symphony's subtree measures.
struct Measure {
    std::int64_t height = 1;
    std::int64_t calls = 0;
};

class Walker {
public:
    Walker(const SymphonyLookup& lookup, std::int64_t max_depth, bool complete)
        : lookup_{lookup}, max_depth_{max_depth}, complete_{complete} {}

    SymphonyWalk run(const SymphonySpec& spec) {
        path_.push_back(spec.name);
        if (const std::optional<Measure> measured = walk(spec); measured.has_value()) {
            out_.depth = measured->height;
            out_.stage_calls = measured->calls;
        }
        return std::move(out_);
    }

private:
    /// `stage 2 (verify)` for the root's own stages, `outer → inner, stage 2
    /// (verify)` below it.
    [[nodiscard]] std::string position(std::size_t index, const SymphonyStage& stage) const {
        std::string at = path_.size() == 1 ? std::string{} : symphony_path(path_) + ", ";
        at += "stage " + std::to_string(index + 1) + " (" + stage.name + ")";
        return at;
    }

    /// The path with `name` played from its end.
    [[nodiscard]] std::string path_to(const std::string& name) const {
        std::vector<std::string> names = path_;
        names.push_back(name);
        return symphony_path(names);
    }

    [[nodiscard]] bool on_path(std::string_view name) const {
        const std::string wanted = folded(name);
        return std::ranges::any_of(path_,
                                   [&](const std::string& each) { return folded(each) == wanted; });
    }

    /// The symphony at the end of the path, walked: its height and calls, or
    /// nothing once a problem is recorded.
    // NOLINTNEXTLINE(misc-no-recursion): bounded by the depth cap, checked on the way down.
    std::optional<Measure> walk(const SymphonySpec& spec) {
        Measure measure;
        for (std::size_t index = 0; index < spec.stages.size(); ++index) {
            const SymphonyStage& stage = spec.stages[index];
            if (!stage.plays()) {
                measure.calls = saturating_add(measure.calls, 1);
                continue;
            }
            const std::optional<Measure> below = play(index, stage);
            if (!below.has_value()) {
                return std::nullopt;
            }
            measure.height = std::max(measure.height, below->height + 1);
            measure.calls = saturating_add(measure.calls, below->calls);
        }
        return measure;
    }

    /// Stage `index` of the symphony at the end of the path, which plays
    /// `stage.play`: what that symphony measures, or nothing on a problem.
    // NOLINTNEXTLINE(misc-no-recursion): bounded by the depth cap, checked on the way down.
    std::optional<Measure> play(std::size_t index, const SymphonyStage& stage) {
        const std::string& name = stage.play;
        // A loop first: a symphony playing itself at the cap is a loop, not a
        // nesting that merely went deep.
        if (on_path(name)) {
            out_.problem = position(index, stage) + ": plays '" + name +
                           "', which is already playing -- a loop, " + path_to(name) +
                           "; a symphony may not reach itself, directly or through another";
            return std::nullopt;
        }
        const auto depth = static_cast<std::int64_t>(path_.size()) + 1;
        if (depth > max_depth_) {
            out_.problem = position(index, stage) + ": plays '" + name + "', which nests " +
                           std::to_string(depth) + " deep -- " + path_to(name) +
                           ", and the cap is " + std::to_string(max_depth_) +
                           " (symphony_caps.depth)";
            return std::nullopt;
        }
        const SymphonySpec* played = lookup_ ? lookup_(name) : nullptr;
        if (played == nullptr) {
            if (complete_) {
                out_.problem = position(index, stage) + ": plays '" + name +
                               "', and no symphony is named '" + name + "'";
                return std::nullopt;
            }
            // Late-bound: whoever knows the name walks it again.
            return Measure{.height = 1, .calls = 1};
        }
        const std::string key = folded(name);
        if (std::ranges::none_of(out_.reached,
                                 [&](const std::string& each) { return folded(each) == key; })) {
            out_.reached.push_back(played->name);
        }
        // A subtree already walked clean is the same subtree here: no loop
        // can reach back into a path it was sound under (it would have reached
        // itself), so only the depth is asked again -- and walked when it no
        // longer fits, so the problem names the path that goes too deep.
        if (const auto known = memo_.find(key);
            known != memo_.end() && depth + known->second.height - 1 <= max_depth_) {
            return known->second;
        }
        path_.push_back(played->name);
        const std::optional<Measure> measured = walk(*played);
        path_.pop_back();
        if (measured.has_value()) {
            memo_[key] = *measured;
        }
        return measured;
    }

    const SymphonyLookup& lookup_;
    std::int64_t max_depth_;
    bool complete_;
    std::vector<std::string> path_;
    std::map<std::string, Measure> memo_;
    SymphonyWalk out_;
};

}  // namespace

SymphonyWalk walk_symphony(const SymphonySpec& spec, const SymphonyLookup& lookup,
                           std::int64_t max_depth, bool complete) {
    return Walker{lookup, max_depth, complete}.run(spec);
}

std::string symphony_path(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        out += out.empty() ? "" : " → ";
        out += name;
    }
    return out;
}

}  // namespace apogee::harness
