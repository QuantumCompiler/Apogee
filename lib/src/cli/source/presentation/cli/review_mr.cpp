#include "cli/review_mr.h"

#include <set>
#include <stdexcept>

#include "contracts/review_skill.h"
#include "review/diff.h"
#include "review/identity.h"

namespace apogee::commands {
namespace {

[[nodiscard]] std::string first_line(std::string_view text, std::size_t limit = 160) {
    std::string_view line = text.substr(0, text.find('\n'));
    if (line.size() > limit) {
        return std::string{line.substr(0, limit)} + "…";
    }
    return std::string{line};
}

[[nodiscard]] std::string short_sha(std::string_view sha) {
    return std::string{sha.substr(0, 7)};
}

[[nodiscard]] std::string joined(const std::vector<std::string>& items) {
    std::string out;
    for (const std::string& item : items) {
        out += (out.empty() ? "" : ", ") + item;
    }
    return out;
}

}  // namespace

ReviewMrResult review_merge_request(const ReviewMrOptions& options, const ReviewMrSeams& seams) {
    std::string error;
    const std::optional<forge::MergeRequestRef> ref =
        options.parsed.has_value() ? options.parsed : forge::parse_ref(options.ref, error);
    if (!ref.has_value()) {
        throw std::runtime_error(error);
    }
    const std::string target = forge::to_string(*ref);
    const auto say = [&seams](std::string_view line) {
        if (seams.progress) {
            seams.progress(line);
        }
    };
    std::unique_ptr<forge::ForgeClient> client = seams.forge(ref->forge);

    say("reading " + target);
    const forge::MergeRequest request = client->merge_request(*ref);
    const std::string diff_text = client->diff(*ref);

    // --- what this login posted before: hints, trusted from it alone ---------
    const std::string login = client->current_user(*ref);
    std::optional<std::string> previous_head;
    std::set<std::string> known_identities;
    std::vector<review::KnownFinding> known;
    for (const forge::PostedComment& comment : client->comments(*ref)) {
        if (comment.author != login) {
            continue;
        }
        const review::Markers markers = review::read_markers(comment.body);
        if (markers.head.has_value()) {
            previous_head = markers.head;  // the latest summary's head wins
        }
        for (const std::string& identity : markers.findings) {
            known_identities.insert(identity);
            known.push_back(review::KnownFinding{
                .file = comment.path, .line = comment.line, .excerpt = first_line(comment.body)});
        }
    }

    // --- the change, its exclusions, and on a re-review what is new ---------
    std::vector<std::string> excluded;
    const review::ParsedDiff diff =
        review::exclude_files(review::parse_unified_diff(diff_text),
                              harness::review_exclusions(seams.skills_dir), excluded);
    std::vector<std::string> notes;
    std::optional<review::ParsedDiff> since;
    std::string mode = "full";
    if (previous_head.has_value() && !request.head_sha.empty()) {
        if (*previous_head == request.head_sha || request.head_sha.starts_with(*previous_head)) {
            since = review::ParsedDiff{};
            mode = "already reviewed";
            notes.push_back("head " + short_sha(request.head_sha) +
                            " was reviewed already: nothing new to review");
        } else {
            try {
                say("comparing " + short_sha(*previous_head) + ".." + short_sha(request.head_sha));
                since = review::parse_unified_diff(
                    client->compare(*ref, *previous_head, request.head_sha));
                mode = "incremental";
            } catch (const forge::ForgeError& e) {
                notes.push_back(std::string{"the change since the last review could not be had ("} +
                                e.what() +
                                "): the whole change was reviewed, the posted findings left out");
            }
        }
    }

    // --- plan and review --------------------------------------------------------
    const harness::ReviewSkillBook book = harness::review_skill_book(seams.skills_dir);
    const review::PassFn* narrow = seams.narrow ? &seams.narrow : nullptr;
    const review::Plan plan =
        review::plan_review(book, diff.paths(), options.skills, narrow, seams.progress);
    review::ReviewInput input;
    input.title = request.title;
    input.description = request.description;
    input.diff = diff;
    input.since = since;
    input.known_identities = known_identities;
    input.known = known;
    // An already-reviewed head has nothing new: no pass runs.
    if (mode != "already reviewed") {
        input.skills = plan.skills;
    }
    input.chunk_tokens = seams.chunk_tokens;
    const review::ReviewOutcome outcome = review::run_review(input, seams.pass, seams.progress);
    notes.insert(notes.end(), outcome.notes.begin(), outcome.notes.end());

    // --- post: one batch, through the gate -------------------------------------
    nlohmann::json posted{{"done", false}};
    std::vector<forge::InlineComment> comments;
    for (const review::Finding& finding : outcome.inline_findings) {
        comments.push_back(forge::InlineComment{
            .path = finding.file, .line = finding.line, .body = review::comment_body(finding)});
    }
    if (outcome.silent()) {
        posted["reason"] = "nothing to say: silence is a complete review";
    } else if (options.dry_run) {
        posted["reason"] = "dry run: nothing posted";
    } else {
        std::string why_not;
        if (!seams.gate || !seams.gate(target, comments.size(), why_not)) {
            posted["reason"] = why_not.empty() ? std::string{"posting was not allowed"} : why_not;
        } else {
            say("posting " + std::to_string(comments.size()) + " comment(s) to " + target);
            const forge::PublishOutcome publish = client->publish_batch(
                request, comments, review::summary_body(outcome, request.head_sha));
            posted = nlohmann::json{{"done", true},
                                    {"comments", publish.posted},
                                    {"summary", true},
                                    {"batched", publish.batched}};
            if (!publish.note.empty()) {
                posted["note"] = publish.note;
            }
        }
    }

    // --- the record ------------------------------------------------------------
    nlohmann::json findings = nlohmann::json::array();
    for (const review::Finding& finding : outcome.inline_findings) {
        nlohmann::json row = review::to_json(finding);
        row["inline"] = true;
        findings.push_back(std::move(row));
    }
    for (const review::Finding& finding : outcome.over_cap) {
        nlohmann::json row = review::to_json(finding);
        row["inline"] = false;
        findings.push_back(std::move(row));
    }
    nlohmann::json skills = nlohmann::json::array();
    for (const harness::ReviewSkillEntry* entry : plan.skills) {
        skills.push_back(entry->skill.name);
    }
    ReviewMrResult result;
    result.document = nlohmann::json{
        {"object", "review.run"},
        {"merge_request", target},
        {"url", request.web_url},
        {"title", request.title},
        {"head", request.head_sha},
        {"mode", mode},
        {"since", previous_head.has_value() ? nlohmann::json(*previous_head) : nlohmann::json()},
        {"model", options.model},
        {"skills", skills},
        {"plan", plan.how},
        {"left_out_skills", plan.left_out},
        {"excluded_files", excluded},
        {"chunks", outcome.chunks},
        {"findings", findings},
        {"counts",
         {{"candidates", outcome.counts.candidates},
          {"off_change", outcome.counts.off_change},
          {"not_new", outcome.counts.not_new},
          {"known", outcome.counts.known},
          {"duplicates", outcome.counts.duplicates},
          {"dismissed", outcome.counts.dismissed},
          {"tightened", outcome.counts.tightened}}},
        {"posted", posted},
        {"dry_run", options.dry_run},
        {"notes", notes},
    };

    std::vector<std::string>& lines = result.lines;
    lines.push_back("reviewed " + target + " -- " + first_line(request.title, 80) + " (head " +
                    short_sha(request.head_sha) + ", " + mode + ")");
    lines.push_back("lenses: " +
                    (skills.empty() ? std::string{"none apply"}
                                    : joined(skills.get<std::vector<std::string>>())) +
                    " (" + plan.how + ")");
    if (!excluded.empty()) {
        lines.push_back("left out: " + joined(excluded));
    }
    if (outcome.silent()) {
        lines.push_back("no findings -- silence is a complete review");
    } else {
        lines.push_back(std::to_string(outcome.inline_findings.size() + outcome.over_cap.size()) +
                        " finding(s):");
        for (const std::vector<review::Finding>* list :
             {&outcome.inline_findings, &outcome.over_cap}) {
            for (const review::Finding& finding : *list) {
                lines.push_back("  [" + finding.severity + "] " + finding.file + ":" +
                                std::to_string(finding.line) + " (" + joined(finding.found_by) +
                                ") " + first_line(finding.claim));
            }
        }
    }
    for (const std::string& note : notes) {
        lines.push_back("note: " + note);
    }
    if (posted.value("done", false)) {
        lines.push_back("posted " + std::to_string(posted.value("comments", 0)) +
                        " comment(s) and a summary to " + target +
                        (posted.value("batched", false) ? " as one notification" : ""));
    } else {
        lines.push_back("posted nothing: " + posted.value("reason", std::string{}));
    }
    return result;
}

}  // namespace apogee::commands
