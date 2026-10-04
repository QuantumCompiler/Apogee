#include "training/eval.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cctype>
#include <chrono>
#include <ctime>
#include <fstream>

namespace apogee::training {

GateMode gate_mode_from_string(std::string_view name) noexcept {
    return name == "soft" ? GateMode::Soft : GateMode::Hard;
}

namespace {

std::string trimmed_upper(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
        ++begin;
    }
    std::string out;
    for (std::size_t i = begin; i < text.size(); ++i) {
        out.push_back(static_cast<char>(std::toupper(static_cast<unsigned char>(text[i]))));
    }
    return out;
}

}  // namespace

nlohmann::json eval_results_to_json(const EvalResults& results) {
    nlohmann::json items = nlohmann::json::array();
    for (const EvalItemResult& item : results.items) {
        nlohmann::json row{{"prompt", item.prompt},
                           {"got", item.got},
                           {"check_type", item.check_type},
                           {"passed", item.passed}};
        if (!item.expected.empty()) {
            row["expected"] = item.expected;
        }
        if (item.check_type == "judge") {
            row["baseline_got"] = item.baseline_got;
            row["judge_winner"] = item.judge_winner;
        }
        items.push_back(std::move(row));
    }
    nlohmann::json out{{"passed", results.passed},
                       {"score", results.score},
                       {"total", results.total},
                       {"num_passed", results.num_passed},
                       {"num_skipped", results.num_skipped},
                       {"items", std::move(items)},
                       {"run_at", results.run_at}};
    if (!results.judge_backend.empty()) {
        out["judge_backend"] = results.judge_backend;
    }
    if (!results.suite.empty()) {
        out["suite"] = results.suite;
    }
    return out;
}

EvalResults eval_results_from_json(const nlohmann::json& json) {
    if (!json.is_object()) {
        throw std::runtime_error("eval_results: expected an object");
    }
    EvalResults results;
    results.passed = json.value("passed", false);
    results.score = json.value("score", 0.0);
    results.total = json.value("total", 0);
    results.num_passed = json.value("num_passed", 0);
    results.num_skipped = json.value("num_skipped", 0);
    results.judge_backend = json.value("judge_backend", std::string{});
    results.suite = json.value("suite", std::string{});
    results.run_at = json.value("run_at", std::string{});
    if (const auto items = json.find("items"); items != json.end() && items->is_array()) {
        for (const nlohmann::json& row : *items) {
            if (!row.is_object()) {
                throw std::runtime_error("eval_results.items: expected objects");
            }
            EvalItemResult item;
            item.prompt = row.value("prompt", std::string{});
            item.expected = row.value("expected", std::string{});
            item.got = row.value("got", std::string{});
            item.check_type = row.value("check_type", std::string{});
            item.passed = row.value("passed", false);
            item.baseline_got = row.value("baseline_got", std::string{});
            item.judge_winner = row.value("judge_winner", std::string{});
            results.items.push_back(std::move(item));
        }
    }
    return results;
}

std::vector<EvalItem> load_eval_suite(const std::filesystem::path& path, std::string& error) {
    error.clear();
    std::vector<EvalItem> items;
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        error = "cannot read the eval suite " + path.string();
        return items;
    }
    std::string line;
    int number = 0;
    while (std::getline(in, line)) {
        ++number;
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.find_first_not_of(" \t") == std::string::npos) {
            continue;
        }
        const nlohmann::json row = nlohmann::json::parse(line, nullptr, false);
        if (row.is_discarded() || !row.is_object()) {
            error = path.string() + ":" + std::to_string(number) + ": not a JSON object";
            return {};
        }
        EvalItem item;
        if (const auto prompt = row.find("prompt"); prompt != row.end() && prompt->is_string()) {
            item.prompt = prompt->get<std::string>();
        }
        if (item.prompt.empty()) {
            continue;
        }
        if (const auto expected = row.find("expected");
            expected != row.end() && expected->is_string()) {
            item.expected = expected->get<std::string>();
        }
        items.push_back(std::move(item));
    }
    if (items.empty()) {
        error = path.string() + " holds no eval item ({\"prompt\": ..., \"expected\"?: ...})";
    }
    return items;
}

std::string judge_prompt(std::string_view prompt, std::string_view candidate,
                         std::string_view baseline) {
    std::string out = "You are evaluating two AI responses to the same prompt.\n\nPrompt: ";
    out += prompt;
    out += "\n\nResponse A: ";
    out += candidate;
    out += "\n\nResponse B: ";
    out += baseline;
    out +=
        "\n\nWhich response better answers the prompt? Reply with exactly one word: A, B, or TIE.";
    return out;
}

std::string parse_judge_verdict(std::string_view reply) {
    // The first WORD, letters only: "A" and "B" are verdicts, "Both are
    // fine" is not a vote for B (the reference read its first byte and
    // counted it as one).
    const std::string answer = trimmed_upper(reply);
    std::string word;
    for (const char c : answer) {
        if (c >= 'A' && c <= 'Z') {
            word.push_back(c);
        } else {
            break;
        }
    }
    if (word == "A") {
        return "candidate";
    }
    if (word == "B") {
        return "baseline";
    }
    return "tie";
}

EvalRun run_eval(const std::vector<EvalItem>& items, CandidateRunner& candidate,
                 const EvalOptions& options) {
    EvalRun run;
    run.results.total = static_cast<int>(items.size());
    run.results.judge_backend = options.judge_backend;
    run.results.run_at = rfc3339_now();
    if (items.empty()) {
        run.error = "the eval suite holds no item";
        return run;
    }
    const bool judging = options.judge && options.baseline != nullptr;
    int done = 0;
    for (const EvalItem& item : items) {
        if (options.cancellation.stop_requested()) {
            run.cancelled = true;
            run.error = "cancelled";
            return run;
        }
        const CandidateReply reply = candidate.run(item.prompt, options.cancellation);
        if (!reply.ok) {
            run.cancelled = options.cancellation.stop_requested();
            run.error = "candidate inference for \"" + item.prompt + "\": " + reply.error;
            return run;
        }
        EvalItemResult result;
        result.prompt = item.prompt;
        result.expected = item.expected;
        result.got = reply.text;
        if (!item.expected.empty()) {
            // Deterministic, case-sensitive: the suite author wrote the
            // exact text the answer must carry.
            result.check_type = "contains";
            result.passed = reply.text.find(item.expected) != std::string::npos;
        } else if (judging) {
            result.check_type = "judge";
            const CandidateReply base = options.baseline->run(item.prompt, options.cancellation);
            if (!base.ok) {
                // Never-fail: a baseline that cannot answer is a tie, and a
                // tie passes -- the adapter is not worse than nothing.
                result.judge_winner = "tie";
                result.passed = true;
            } else {
                result.baseline_got = base.text;
                const JudgeReply verdict =
                    options.judge(item.prompt, reply.text, base.text, options.cancellation);
                result.judge_winner = verdict.ok ? parse_judge_verdict(verdict.text) : "tie";
                result.passed = result.judge_winner != "baseline";
            }
        } else {
            result.check_type = "skip";
            result.passed = true;
            ++run.results.num_skipped;
        }
        if (result.passed) {
            ++run.results.num_passed;
        }
        run.results.items.push_back(std::move(result));
        ++done;
        if (options.on_progress) {
            options.on_progress(done, run.results.total);
        }
    }
    run.results.score =
        static_cast<double>(run.results.num_passed) / static_cast<double>(run.results.total);
    run.results.passed = run.results.num_passed == run.results.total;
    run.ok = true;
    return run;
}

std::string rfc3339_now() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &now);
#else
    gmtime_r(&now, &utc);
#endif
    std::array<char, 32> buffer{};
    std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &utc);
    return buffer.data();
}

}  // namespace apogee::training
