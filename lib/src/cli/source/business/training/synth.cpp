#include "training/synth.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <mutex>
#include <set>
#include <thread>

namespace apogee::training {
namespace {

std::string trim(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return std::string{text};
}

std::string lower(std::string_view text) {
    std::string out{text};
    for (char& c : out) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return out;
}

/// The first non-blank string among `keys`, compared case-insensitively.
std::string first_string(const nlohmann::json& object, std::initializer_list<const char*> keys) {
    for (const char* key : keys) {
        for (const auto& [name, value] : object.items()) {
            if (lower(name) == key && value.is_string()) {
                const std::string text = value.get<std::string>();
                if (!trim(text).empty()) {
                    return text;
                }
            }
        }
    }
    return {};
}

}  // namespace

std::string synth_system_prompt(std::string_view kit_system) {
    // The contract appended to the kit's prompt, so every teacher --
    // whatever its native output habits -- returns a machine-parseable
    // array. Ommi's wording, kept.
    constexpr std::string_view kContract =
        "\n\nYou are generating a synthetic supervised fine-tuning dataset. Each example must "
        "be realistic, self-contained, and correct. Vary phrasing, length, and difficulty across "
        "examples; never repeat a prompt. Respond with ONLY a JSON array and nothing else -- no "
        "prose, no markdown code fences. Each array element is an object with exactly two string "
        "fields: \"prompt\" (the user's input) and \"completion\" (the ideal assistant response).";
    return trim(kit_system) + std::string{kContract};
}

std::string synth_user_prompt(int n, std::string_view seed, std::string_view topic) {
    std::string out =
        "Generate exactly " + std::to_string(n) + " distinct, high-quality training examples.";
    if (const std::string s = trim(seed); !s.empty()) {
        out += "\nFocus this batch on: " + s;
    }
    if (const std::string t = trim(topic); !t.empty()) {
        out += "\nAdditional focus: " + t;
    }
    out += "\nReturn ONLY the JSON array.";
    return out;
}

std::string extract_json_array(std::string_view text) {
    const std::size_t start = text.find('[');
    if (start == std::string_view::npos) {
        return {};
    }
    int depth = 0;
    bool in_string = false;
    bool escaped = false;
    for (std::size_t i = start; i < text.size(); ++i) {
        const char c = text[i];
        if (in_string) {
            if (escaped) {
                escaped = false;
            } else if (c == '\\') {
                escaped = true;
            } else if (c == '"') {
                in_string = false;
            }
            continue;
        }
        if (c == '"') {
            in_string = true;
        } else if (c == '[') {
            ++depth;
        } else if (c == ']') {
            --depth;
            if (depth == 0) {
                return std::string{text.substr(start, i - start + 1)};
            }
        }
    }
    return {};
}

std::vector<SynthExample> parse_synth_examples(std::string_view raw) {
    std::vector<SynthExample> out;
    const std::string array = extract_json_array(raw);
    if (array.empty()) {
        return out;
    }
    const nlohmann::json rows = nlohmann::json::parse(array, nullptr, false);
    if (rows.is_discarded() || !rows.is_array()) {
        return out;
    }
    for (const nlohmann::json& row : rows) {
        if (!row.is_object()) {
            continue;
        }
        const std::string prompt =
            first_string(row, {"prompt", "input", "user", "instruction", "question"});
        const std::string completion =
            first_string(row, {"completion", "output", "assistant", "response", "answer"});
        if (trim(prompt).empty() || trim(completion).empty()) {
            continue;
        }
        out.push_back({trim(prompt), trim(completion)});
    }
    return out;
}

int max_synth_calls(int target, int per_call, std::size_t seeds) noexcept {
    if (per_call <= 0) {
        per_call = kDefaultPerSeed;
    }
    return (target / per_call + 1) * 3 + static_cast<int>(seeds);
}

SynthResult synthesize(const Kit& kit, const GenerateFn& generate, const SynthOptions& options) {
    SynthResult result;
    if (!generate) {
        result.error = "synth: a teacher generate function is required";
        return result;
    }
    int target = options.count > 0 ? options.count : kit.synth.count;
    if (target <= 0) {
        target = kDefaultSynthCount;
    }
    const double temperature =
        options.temperature > 0.0 ? options.temperature : kit.synth.temperature;
    const int per_call = kit.synth.per_seed > 0 ? kit.synth.per_seed : kDefaultPerSeed;
    const std::string system = synth_system_prompt(kit.synth.system);
    std::vector<std::string> seeds = kit.synth.seeds;
    if (seeds.empty()) {
        seeds.emplace_back();
    }
    const int max_calls = max_synth_calls(target, per_call, seeds.size());
    const auto sleep = options.sleep ? options.sleep : [](std::chrono::milliseconds delay) {
        std::this_thread::sleep_for(delay);
    };

    // Shared between the workers. One mutex, held briefly around planning a
    // batch and merging its result; the teacher call itself runs unlocked.
    std::mutex mutex;
    std::set<std::string> seen;
    int calls = 0;
    int next_batch = 0;
    bool stopped = false;

    const auto worker = [&] {
        for (;;) {
            int n = 0;
            std::string seed;
            {
                const std::lock_guard<std::mutex> lock{mutex};
                if (stopped || options.cancellation.stop_requested() ||
                    static_cast<int>(result.examples.size()) >= target || calls >= max_calls) {
                    return;
                }
                const int remaining = target - static_cast<int>(result.examples.size());
                n = std::min(per_call, std::max(remaining, 1));
                seed = seeds[static_cast<std::size_t>(next_batch) % seeds.size()];
                ++next_batch;
                ++calls;
                ++result.calls;
            }
            const std::string user = synth_user_prompt(n, seed, options.topic);

            GenerateOutcome outcome;
            for (int attempt = 0;; ++attempt) {
                try {
                    outcome = generate(system, user, temperature, options.max_tokens,
                                       options.cancellation);
                } catch (const std::exception& e) {
                    outcome = GenerateOutcome{};
                    outcome.error = e.what();
                }
                if (outcome.ok || options.cancellation.stop_requested() || !outcome.retryable ||
                    attempt >= options.max_retries) {
                    break;
                }
                std::chrono::milliseconds delay = options.base_delay;
                for (int i = 0; i < attempt; ++i) {
                    delay = std::min(delay * 2, options.max_delay);
                }
                delay = std::min(delay, options.max_delay);
                {
                    const std::lock_guard<std::mutex> lock{mutex};
                    ++result.retries;
                    ++result.calls;
                }
                sleep(delay);
            }

            const std::lock_guard<std::mutex> lock{mutex};
            if (options.cancellation.stop_requested()) {
                stopped = true;
                result.cancelled = true;
                return;
            }
            if (!outcome.ok) {
                // A single failed batch is non-fatal: the next seed may do
                // better, and the call bound ends a run that never does.
                ++result.failed_batches;
                continue;
            }
            const std::vector<SynthExample> examples = parse_synth_examples(outcome.text);
            if (examples.empty()) {
                ++result.failed_batches;
                continue;
            }
            for (const SynthExample& example : examples) {
                if (static_cast<int>(result.examples.size()) >= target) {
                    break;
                }
                const std::string key = lower(trim(example.prompt));
                if (!seen.insert(key).second) {
                    continue;
                }
                result.examples.push_back(example);
            }
            if (options.on_progress) {
                options.on_progress(static_cast<int>(result.examples.size()), target);
            }
        }
    };

    const int workers = std::max(1, std::min(options.parallel, max_calls));
    if (workers == 1) {
        worker();
    } else {
        std::vector<std::thread> threads;
        threads.reserve(static_cast<std::size_t>(workers));
        for (int i = 0; i < workers; ++i) {
            threads.emplace_back(worker);
        }
        for (std::thread& thread : threads) {
            thread.join();
        }
    }

    if (options.cancellation.stop_requested()) {
        result.cancelled = true;
    }
    if (result.examples.empty() && !result.cancelled) {
        result.error = "the teacher produced no usable examples after " +
                       std::to_string(result.calls) +
                       " call(s) -- check the teacher backend and the kit's synth prompt";
    }
    return result;
}

}  // namespace apogee::training
