#include "agentloop/validate.h"

#include <algorithm>
#include <array>
#include <span>
#include <stdexcept>
#include <utility>

#include "agentloop/content.h"

namespace apogee::agentloop {
namespace {

/// What every brief ends with: the reply's shape, so it can be read.
constexpr std::string_view kVerdictInstruction =
    "Reply with AGREE or OBJECT as your first word. After OBJECT, say in one or two sentences "
    "what is wrong and what would be right.";

/// The most of a request, an argument list or a description a tool brief
/// quotes.
constexpr std::size_t kBriefRequest = 2000;
constexpr std::size_t kBriefArguments = 2000;
constexpr std::size_t kBriefDescription = 400;

bool is_space(char c) noexcept {
    return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

std::string_view trimmed(std::string_view text) noexcept {
    while (!text.empty() && is_space(text.front())) {
        text.remove_prefix(1);
    }
    while (!text.empty() && is_space(text.back())) {
        text.remove_suffix(1);
    }
    return text;
}

bool is_letter(char c) noexcept {
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

std::string upper(std::string_view text) {
    std::string out{text};
    for (char& c : out) {
        if (c >= 'a' && c <= 'z') {
            c = static_cast<char>(c - 'a' + 'A');
        }
    }
    return out;
}

constexpr std::array<std::string_view, 3> kAgreeWords{"AGREE", "AGREES", "AGREED"};
constexpr std::array<std::string_view, 3> kObjectWords{"OBJECT", "OBJECTS", "OBJECTION"};

bool one_of(std::string_view word, std::span<const std::string_view> words) {
    return std::ranges::find(words, word) != words.end();
}

/// What follows a verdict word: past emphasis, punctuation and dashes.
std::string reason_after(std::string_view rest) {
    for (;;) {
        if (rest.empty()) {
            break;
        }
        const char c = rest.front();
        if (is_space(c) || c == '*' || c == '_' || c == '`' || c == '"' || c == '\'' || c == ':' ||
            c == '.' || c == '-' || c == ',' || c == ';' || c == '!') {
            rest.remove_prefix(1);
            continue;
        }
        // An em or en dash, as models write them: E2 80 93 / E2 80 94.
        if (rest.size() >= 3 && static_cast<unsigned char>(rest[0]) == 0xE2U &&
            static_cast<unsigned char>(rest[1]) == 0x80U &&
            (static_cast<unsigned char>(rest[2]) == 0x93U ||
             static_cast<unsigned char>(rest[2]) == 0x94U)) {
            rest.remove_prefix(3);
            continue;
        }
        break;
    }
    return std::string{trimmed(rest)};
}

/// The arguments as one canonical JSON line, so `{"a":1, "b":2}` and
/// `{"b":2,"a":1}` are one call; the text as given when it is not JSON.
std::string canonical(std::string_view arguments) {
    const nlohmann::json parsed = nlohmann::json::parse(arguments, nullptr, false);
    return parsed.is_discarded() ? std::string{arguments} : parsed.dump();
}

/// A block quoted in a brief, so the member can tell where it ends.
std::string fenced(std::string_view text) {
    return "<<<\n" + std::string{trimmed(text)} + "\n>>>";
}

/// A cut inside a brief: verbatim when it fits, else the head and how much
/// more there was.
std::string brief_cut(std::string_view text, std::size_t limit) {
    text = trimmed(text);
    if (text.size() <= limit) {
        return std::string{text};
    }
    std::size_t cut = limit;
    while (cut > 0 && (static_cast<unsigned char>(text[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    return std::string{text.substr(0, cut)} + " [… " + std::to_string(text.size() - cut) +
           " more characters not shown]";
}

/// The verifier named for a line: `utility (l3b)`, or `the check` for
/// structure.
std::string who(std::string_view by) {
    return by == kStructure ? std::string{"the check"} : std::string{by};
}

}  // namespace

std::string_view to_string(Seam seam) noexcept {
    switch (seam) {
        case Seam::ToolArgs:
            return "tool_args";
        case Seam::Extraction:
            return "extraction";
        case Seam::Answer:
            return "answers";
    }
    return "answers";
}

std::string_view to_string(Validated::Result result) noexcept {
    switch (result) {
        case Validated::Result::Passed:
            return "passed";
        case Validated::Result::Revised:
            return "revised";
        case Validated::Result::Disputed:
            return "disputed";
        case Validated::Result::Unchecked:
            return "unchecked";
    }
    return "unchecked";
}

Verdict read_verdict(std::string_view reply) {
    Verdict out;
    const std::string_view text = trimmed(reply);
    std::size_t begin = 0;
    while (begin < text.size() &&
           (text[begin] == '*' || text[begin] == '_' || text[begin] == '#' || text[begin] == '`' ||
            text[begin] == '"' || text[begin] == '\'' || text[begin] == '>' ||
            is_space(text[begin]))) {
        ++begin;
    }
    std::size_t end = begin;
    while (end < text.size() && is_letter(text[end])) {
        ++end;
    }
    // Every verdict written in capitals, as a whole word -- `DISAGREE` is
    // not `AGREE` -- past the first word.
    bool agree = false;
    bool object = false;
    for (std::size_t at = end; at < text.size();) {
        if (!is_letter(text[at])) {
            ++at;
            continue;
        }
        std::size_t stop = at;
        while (stop < text.size() && is_letter(text[stop])) {
            ++stop;
        }
        const std::string_view word = text.substr(at, stop - at);
        agree = agree || one_of(word, kAgreeWords);
        object = object || one_of(word, kObjectWords);
        at = stop;
    }
    const std::string first = upper(text.substr(begin, end - begin));
    if (one_of(first, kObjectWords)) {
        out.kind = Verdict::Kind::Object;
        out.reason = reason_after(text.substr(end));
        if (out.reason.empty()) {
            out.reason = "(no reason given)";
        }
        return out;
    }
    if (one_of(first, kAgreeWords) && !object) {
        out.kind = Verdict::Kind::Agree;
        out.reason = reason_after(text.substr(end));
        return out;
    }
    if (one_of(first, kAgreeWords)) {
        // An agreement that objects in the same breath is no verdict: an
        // objection is never read away.
        out.reason = excerpt(text, 120);
        return out;
    }
    // Not where it was asked for: exactly one verdict, in capitals, as a
    // whole word anywhere.
    if (object != agree) {
        out.kind = object ? Verdict::Kind::Object : Verdict::Kind::Agree;
        out.reason = std::string{text};
        return out;
    }
    out.reason = excerpt(text, 120);
    return out;
}

Check run_checks(const std::vector<StructuralCheck>& structure, const Verifier* verifier,
                 const std::function<std::string()>& brief) {
    Check out;
    // Structure first: what it answers, no model is asked.
    for (const StructuralCheck& check : structure) {
        if (std::string objection = check(); !objection.empty()) {
            out.outcome = Check::Outcome::Object;
            out.by = std::string{kStructure};
            out.objection = std::move(objection);
            return out;
        }
    }
    if (verifier == nullptr) {
        out.by = std::string{kStructure};
        return out;
    }
    out.by = verifier->role;
    if (verifier->unavailable) {
        if (std::string why = verifier->unavailable(); !why.empty()) {
            out.outcome = Check::Outcome::Unchecked;
            out.note = std::move(why);
            return out;
        }
    }
    const std::string sent = brief();
    if (const std::int64_t tokens = estimate_tokens(sent).tokens; tokens > verifier->brief_tokens) {
        out.outcome = Check::Outcome::Unchecked;
        out.note = "the brief would be about " + std::to_string(tokens) +
                   " tokens, over the suite's cap of " + std::to_string(verifier->brief_tokens) +
                   " (consult_caps.brief_tokens) -- not checked by " + verifier->role;
        return out;
    }
    const MemberAnswer answer = verifier->ask(sent);
    if (!answer.backend.empty()) {
        out.by = verifier->role + " (" + answer.backend + ")";
    }
    if (!answer.refused.empty()) {
        out.outcome = Check::Outcome::Unchecked;
        out.note = out.by + " could not be asked: " + answer.refused;
        return out;
    }
    out.model_calls = 1;
    if (!answer.failed.empty()) {
        out.outcome = Check::Outcome::Unchecked;
        out.note = out.by + " failed to check: " + answer.failed;
        return out;
    }
    const Verdict verdict = read_verdict(answer.text);
    switch (verdict.kind) {
        case Verdict::Kind::Agree:
            out.outcome = Check::Outcome::Pass;
            return out;
        case Verdict::Kind::Object:
            out.outcome = Check::Outcome::Object;
            out.objection = verdict.reason;
            return out;
        case Verdict::Kind::Unread:
            break;
    }
    out.outcome = Check::Outcome::Unchecked;
    out.note = out.by + "'s reply was neither AGREE nor OBJECT: \"" + verdict.reason + "\"";
    return out;
}

Rounds::Step Rounds::next(const Check& check) {
    if (over_) {
        throw std::logic_error("validation's rounds are over: there is no round three");
    }
    if (check.outcome != Check::Outcome::Object) {
        over_ = true;
        return Step::Proceed;
    }
    ++objections_;
    if (objections_ < kMaxRounds) {
        return Step::Revise;
    }
    over_ = true;
    return Step::Surface;
}

Validated validate_artifact(std::string artifact,
                            const std::function<Check(const std::string&)>& check,
                            const ReviseFn& revise, bool recheck) {
    Validated out;
    out.original = artifact;
    out.artifact = std::move(artifact);
    Rounds rounds;
    for (;;) {
        const Check checked = check(out.artifact);
        out.model_calls += checked.model_calls;
        if (!checked.note.empty()) {
            out.notes.push_back(checked.note);
        }
        switch (rounds.next(checked)) {
            case Rounds::Step::Proceed:
                if (checked.outcome == Check::Outcome::Pass) {
                    out.by = checked.by;
                    out.result =
                        out.revisions == 0 ? Validated::Result::Passed : Validated::Result::Revised;
                } else if (out.revisions == 0) {
                    out.by = checked.by;
                    out.result = Validated::Result::Unchecked;
                } else {
                    // A revision the verifier could not check leaves the
                    // objection unanswered: still a dispute, said as one.
                    out.result = Validated::Result::Disputed;
                }
                return out;
            case Rounds::Step::Surface:
                out.objection = checked.objection;
                out.by = checked.by;
                out.result = Validated::Result::Disputed;
                return out;
            case Rounds::Step::Revise:
                break;
        }
        out.objection = checked.objection;
        out.by = checked.by;
        std::string note;
        ++out.revisions;
        std::optional<std::string> revised = revise(out.artifact, out.objection, note);
        if (!revised.has_value()) {
            if (!note.empty()) {
                out.notes.push_back(std::move(note));
            }
            out.result = Validated::Result::Disputed;
            return out;
        }
        if (*revised == out.artifact) {
            out.insisted = true;
            out.result = Validated::Result::Disputed;
            return out;
        }
        out.artifact = std::move(*revised);
        if (!recheck) {
            out.result = Validated::Result::Disputed;
            return out;
        }
    }
}

ToolArgChecks::Decision ToolArgChecks::check(std::string_view tool, std::string_view arguments,
                                             const std::vector<StructuralCheck>& structure,
                                             const Verifier* verifier,
                                             const std::function<std::string()>& brief) {
    Decision out;
    const std::string call = std::string{tool} + " " + excerpt(canonical(arguments), 200);
    auto awaited = pending_.find(tool);
    if (awaited != pending_.end() && awaited->second.arguments == canonical(arguments)) {
        // The producer stood by the call objected to: surfaced, and the
        // verifier is not asked the same question twice.
        out.said.push_back("validate: the model made the same call again, and " + call +
                           " runs over " + who(awaited->second.by) + "'s objection -- " +
                           excerpt(awaited->second.objection));
        pending_.erase(awaited);
        return out;
    }
    Rounds rounds = awaited != pending_.end() ? awaited->second.rounds : Rounds{};
    const Check checked = run_checks(structure, verifier, brief);
    out.model_calls = checked.model_calls;
    switch (rounds.next(checked)) {
        case Rounds::Step::Proceed:
            if (checked.outcome == Check::Outcome::Unchecked && awaited != pending_.end()) {
                // A revision nobody could check leaves round one's objection
                // unanswered: the call runs, and the objection is said.
                out.said.push_back("validate: " + call +
                                   " runs as the model revised it, not "
                                   "checked -- " +
                                   checked.note + "; " + who(awaited->second.by) +
                                   " had objected -- " + excerpt(awaited->second.objection));
            } else if (checked.outcome == Check::Outcome::Unchecked) {
                out.said.push_back("validate: " + call + " not checked -- " + checked.note +
                                   "; its structure passed");
            }
            if (awaited != pending_.end()) {
                pending_.erase(awaited);
            }
            return out;
        case Rounds::Step::Surface:
            out.said.push_back("validate: " + call + " runs as the model revised it, over " +
                               who(checked.by) + "'s objection -- " + excerpt(checked.objection));
            if (awaited != pending_.end()) {
                pending_.erase(awaited);
            }
            return out;
        case Rounds::Step::Revise:
            break;
    }
    out.result = objection_result(tool, checked);
    out.said.push_back("validate: " +
                       (checked.structural() ? call + " failed its check"
                                             : who(checked.by) + " objected to " + call) +
                       " -- " + excerpt(checked.objection) +
                       " -- returned to the model for one revision");
    Pending next{.rounds = rounds,
                 .arguments = canonical(arguments),
                 .objection = checked.objection,
                 .by = checked.by};
    pending_.insert_or_assign(std::string{tool}, std::move(next));
    return out;
}

bool ToolArgChecks::pending(std::string_view tool) const {
    return pending_.contains(tool);
}

std::string tool_args_brief(std::string_view request, std::string_view tool,
                            std::string_view description, std::string_view arguments) {
    return "Check a tool call before it runs: " + std::string{tool} + " " +
           brief_cut(canonical(arguments), kBriefArguments) +
           "\n\nThe request it serves, the user's latest message:\n" +
           fenced(brief_cut(request, kBriefRequest)) + "\n\nWhat " + std::string{tool} +
           " does: " + brief_cut(description, kBriefDescription) +
           "\n\nObject only if an argument is wrong for the request -- the wrong file or "
           "target, an action the request does not ask for, a value the request contradicts. "
           "Not to style.\n\n" +
           std::string{kVerdictInstruction};
}

std::string extraction_brief(std::string_view source, std::string_view record,
                             const std::vector<std::string>& required) {
    std::string fields;
    for (const std::string& field : required) {
        fields += (fields.empty() ? "" : ", ") + field;
    }
    return "Check a record extracted from a source text, against that source.\n\n"
           "Object only to a fact the source does not support:\n"
           "1. A required field (" +
           fields +
           ") that is empty, or says what the source does not.\n"
           "2. A name, number, count, date or link that differs from the source's.\n"
           "3. A field the source contradicts.\n"
           "The record paraphrases the source: other wording, a shorter or longer phrasing, or a "
           "summary is not an error. If no field breaks these rules, reply AGREE.\n\n"
           "SOURCE:\n" +
           fenced(source) + "\n\nRECORD:\n" + fenced(record) + "\n\n" +
           std::string{kVerdictInstruction} +
           " Name the field that is wrong and what the source says.";
}

std::string answer_brief(std::string_view question, std::string_view answer) {
    return "Check an answer against the question it answers.\n\nQUESTION:\n" + fenced(question) +
           "\n\nANSWER:\n" + fenced(answer) +
           "\n\nObject only if the answer is wrong, contradicts itself, or does not answer the "
           "question -- not to style or length.\n\n" +
           std::string{kVerdictInstruction};
}

std::vector<std::string> required_fields(const nlohmann::json& schema) {
    // Depth first, a nested object's fields right after its own name: a
    // stack of the names still to say, each with the schema under it.
    struct Field {
        std::string name;
        const nlohmann::json* schema = nullptr;
    };

    const auto fields_of = [](const nlohmann::json& object, const std::string& prefix) {
        std::vector<Field> out;
        if (!object.is_object()) {
            return out;
        }
        const auto required = object.find("required");
        if (required == object.end() || !required->is_array()) {
            return out;
        }
        const auto properties = object.find("properties");
        for (const nlohmann::json& name : *required) {
            if (!name.is_string()) {
                continue;
            }
            Field field{.name = prefix + name.get<std::string>()};
            if (properties != object.end() && properties->is_object()) {
                if (const auto child = properties->find(name.get<std::string>());
                    child != properties->end()) {
                    field.schema = &*child;
                }
            }
            out.push_back(std::move(field));
        }
        return out;
    };
    std::vector<std::string> out;
    std::vector<Field> stack = fields_of(schema, "");
    std::ranges::reverse(stack);
    while (!stack.empty()) {
        const Field field = std::move(stack.back());
        stack.pop_back();
        out.push_back(field.name);
        if (field.schema != nullptr) {
            const std::vector<Field> children = fields_of(*field.schema, field.name + ".");
            stack.insert(stack.end(), children.rbegin(), children.rend());
        }
    }
    return out;
}

bool seam_on(const harness::Config& config, Seam seam) {
    const harness::SuiteConfig* suite = harness::active_suite(config);
    if (suite == nullptr || !suite->validate.any()) {
        return false;
    }
    const harness::ValidatePolicy policy = harness::validate_policy(suite->validate);
    switch (seam) {
        case Seam::ToolArgs:
            return policy.tool_args;
        case Seam::Extraction:
            return policy.extraction;
        case Seam::Answer:
            return policy.answers_always;
    }
    return false;
}

VerifierRole verifier_role(const harness::Config& config) {
    VerifierRole out;
    const harness::SuiteConfig* suite = harness::active_suite(config);
    if (suite == nullptr) {
        out.missing =
            "no suite is active -- a check is one member of a suite checking another's work";
        return out;
    }
    const std::string role = harness::validate_policy(suite->validate).verifier;
    if (!suite->members.contains(role)) {
        out.missing = "suite " + config.models.default_suite + " has no " + role +
                      " member to check with -- name one, or another verifier: apogee config "
                      "set-suite " +
                      config.models.default_suite + " --verifier <role>";
        return out;
    }
    out.role = role;
    return out;
}

Verifier bind_verifier(const harness::Harness& harness, MemberCalls& calls,
                       const std::string& role) {
    const harness::SuiteConfig* suite = harness::active_suite(harness.config());
    const harness::ConsultLimits limits =
        suite == nullptr ? harness::ConsultLimits{} : harness::consult_limits(suite->consult_caps);
    Verifier out;
    out.role = role;
    out.brief_tokens = limits.brief_tokens;
    out.ask = [&calls, role, limits](const std::string& brief) {
        MemberAnswer answer;
        const std::optional<harness::ModelRole> named = role_named(role);
        if (!named.has_value()) {
            answer.refused = "'" + role + "' is not a role";
            return answer;
        }
        MemberCall call;
        call.role = *named;
        call.brief = brief;
        call.brief_tokens = limits.brief_tokens;
        call.answer_tokens = limits.answer_tokens;
        call.label = "validate";
        return calls.call(call, limits.per_turn);
    };
    out.unavailable = [&calls, limits]() -> std::string {
        if (!calls.in_turn()) {
            return "no turn is open to check in";
        }
        if (calls.used() >= limits.per_turn) {
            return "the turn's member-call budget is spent (" + std::to_string(calls.used()) +
                   " of " + std::to_string(limits.per_turn) +
                   ", consults and checks together) -- checked by structure only";
        }
        return {};
    };
    return out;
}

std::string excerpt(std::string_view text, std::size_t limit) {
    std::string flat;
    for (const char c : trimmed(text)) {
        if (is_space(c)) {
            if (!flat.empty() && flat.back() != ' ') {
                flat += ' ';
            }
        } else {
            flat += c;
        }
    }
    if (flat.size() <= limit) {
        return flat;
    }
    std::size_t cut = flat.rfind(' ', limit);
    if (cut == std::string::npos || cut < limit / 2) {
        cut = limit;
    }
    while (cut > 0 && (static_cast<unsigned char>(flat[cut]) & 0xC0U) == 0x80U) {
        --cut;
    }
    return flat.substr(0, cut) + "…";
}

std::string objection_result(std::string_view tool, const Check& check) {
    if (check.structural()) {
        return "Not run: " + std::string{tool} +
               "'s arguments failed a check made before it runs -- " + check.objection +
               "\nCorrect the call. If it is right as it is, make the same call again: it will "
               "run.";
    }
    return "Not run: before " + std::string{tool} + " ran, " + check.by +
           " checked it and objected -- " + check.objection +
           "\nCorrect the call if the objection is right. If it is wrong, make the same call "
           "again: it will run, and the user is shown the objection.";
}

std::vector<std::string> answer_lines(const Validated& validated, std::string_view label) {
    const std::string tag = std::string{label} + ": ";
    std::vector<std::string> out;
    switch (validated.result) {
        case Validated::Result::Passed:
            out.push_back(tag + who(validated.by) + " agrees with the answer");
            break;
        case Validated::Result::Unchecked: {
            std::string notes;
            for (const std::string& note : validated.notes) {
                notes += (notes.empty() ? "" : "; ") + note;
            }
            out.push_back(tag + "not checked -- " + (notes.empty() ? "no verifier" : notes));
            break;
        }
        case Validated::Result::Revised:
            out.push_back(tag + who(validated.by) + " objected -- \"" +
                          excerpt(validated.objection) + "\" -- and agreed with the revision");
            out.push_back(tag + "the model's revision -- \"" + excerpt(validated.artifact) + "\"");
            break;
        case Validated::Result::Disputed:
            out.push_back(tag + who(validated.by) + " objects to the answer -- \"" +
                          excerpt(validated.objection) + "\"");
            if (validated.revisions > 0 && validated.artifact != validated.original) {
                out.push_back(tag + "shown the objection, the model answered -- \"" +
                              excerpt(validated.artifact) + "\"");
            } else if (validated.insisted) {
                out.push_back(tag + "shown the objection, the model gave its answer again");
            } else {
                std::string notes;
                for (const std::string& note : validated.notes) {
                    notes += (notes.empty() ? "" : "; ") + note;
                }
                out.push_back(tag + "the model did not answer the objection" +
                              (notes.empty() ? std::string{} : " -- " + notes));
            }
            break;
    }
    return out;
}

std::vector<std::string> extraction_lines(const Validated& validated) {
    std::vector<std::string> out;
    switch (validated.result) {
        case Validated::Result::Passed:
            out.push_back("validated by " + who(validated.by) +
                          ": the record agrees with its source");
            break;
        case Validated::Result::Revised:
            out.push_back("validated by " + who(validated.by) +
                          " after one revision -- it had objected: \"" +
                          excerpt(validated.objection) + "\"");
            break;
        case Validated::Result::Unchecked: {
            std::string notes;
            for (const std::string& note : validated.notes) {
                notes += (notes.empty() ? "" : "; ") + note;
            }
            out.push_back("not validated -- " + (notes.empty() ? "no verifier" : notes));
            break;
        }
        case Validated::Result::Disputed: {
            std::string kept = "the record is the clerk's revision";
            if (validated.insisted) {
                kept = "the clerk returned the same record";
            } else if (validated.revisions == 0 || validated.artifact == validated.original) {
                kept = "the record is the clerk's first";
                for (const std::string& note : validated.notes) {
                    kept += " -- " + note;
                }
            }
            out.push_back("disputed by " + who(validated.by) + ": \"" +
                          excerpt(validated.objection) + "\" -- " + kept);
            break;
        }
    }
    return out;
}

nlohmann::json validation_json(const Validated& validated) {
    nlohmann::json out{{"result", std::string{to_string(validated.result)}},
                       {"verifier", who(validated.by)}};
    if (!validated.objection.empty()) {
        out["objection"] = validated.objection;
    }
    out["revisions"] = validated.revisions;
    out["verifier_calls"] = validated.model_calls;
    if (validated.insisted) {
        out["insisted"] = true;
    }
    if (!validated.notes.empty()) {
        out["notes"] = validated.notes;
    }
    return out;
}

}  // namespace apogee::agentloop
