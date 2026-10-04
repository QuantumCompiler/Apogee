#include "operations/dataset_core.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <memory>
#include <stdexcept>
#include <utility>

#include "agentloop/loop.h"
#include "agentloop/reporter.h"
#include "contracts/errors.h"
#include "logger/session.h"
#include "operations/backend_names.h"

namespace apogee::commands {

struct LoadedSessions::Owned {
    std::vector<logger::Session> sessions;
};

LoadedSessions load_session_views() {
    LoadedSessions loaded;
    loaded.keep = std::make_shared<LoadedSessions::Owned>();
    loaded.keep->sessions = logger::list_sessions();
    loaded.views.reserve(loaded.keep->sessions.size());
    for (const logger::Session& session : loaded.keep->sessions) {
        loaded.views.push_back(training::SessionView{.backend = session.backend,
                                                     .started_at = session.started_at,
                                                     .messages = &session.messages});
    }
    return loaded;
}

DatasetCreateResult create_dataset(const training::DatasetStore& store,
                                   const DatasetCreateRequest& request) {
    DatasetCreateResult result;
    std::vector<std::string> lines;
    if (!request.lines.empty()) {
        lines = request.lines;
    } else {
        switch (request.source) {
            case training::CreateSource::Template:
                lines = training::template_lines();
                break;
            case training::CreateSource::Empty:
                break;
            case training::CreateSource::Sessions: {
                if (const std::string error = training::validate_session_filter(request.filter);
                    !error.empty()) {
                    throw std::runtime_error(error);
                }
                const LoadedSessions loaded = load_session_views();
                const training::MinedSessions mined =
                    training::mine_sessions(loaded.views, request.filter);
                lines = mined.lines;
                result.sessions = mined.sessions;
                result.skipped = mined.skipped;
                break;
            }
        }
    }
    if (const std::string error = store.write(request.name, lines, request.force, &result.path);
        !error.empty()) {
        throw std::runtime_error(error);
    }
    result.examples = static_cast<int>(lines.size());
    return result;
}

TeacherResolution resolve_teacher(const harness::Config& config, std::string_view name) {
    TeacherResolution resolution;
    if (name.empty()) {
        resolution.error = "a teacher is required: pass --teacher <backend>";
        return resolution;
    }
    const std::string key = configured_backend_key(config, name);
    if (key.empty()) {
        resolution.error = "teacher '" + std::string{name} + "' is not a configured backend";
        return resolution;
    }
    const harness::BackendConfig* backend = config.find_backend(key);
    if (backend == nullptr) {
        resolution.error = "teacher '" + std::string{name} + "' is not a configured backend";
        return resolution;
    }
    if (harness::is_vendor_cli(backend->type)) {
        resolution.error =
            "'" + key +
            "' is a vendor-CLI backend -- the teacher runs inside Apogee's own loop as a direct "
            "API or local call; pick an API-billing or local backend";
        return resolution;
    }
    resolution.key = key;
    resolution.parallel_safe = backend->type == harness::BackendType::Anthropic ||
                               backend->type == harness::BackendType::OpenAI ||
                               backend->type == harness::BackendType::Google;
    return resolution;
}

training::GenerateFn make_teacher(const harness::Harness& harness, std::string key) {
    return [&harness, key = std::move(key)](std::string_view system, std::string_view user,
                                            double temperature, int max_tokens,
                                            const harness::CancellationToken& cancellation) {
        training::GenerateOutcome outcome;
        std::vector<harness::ChatMessage> history{harness::ChatMessage::system(std::string{system}),
                                                  harness::ChatMessage::user(std::string{user})};
        agentloop::Options options;
        options.model = key;
        options.temperature = temperature;
        options.max_tokens = max_tokens;
        options.stream_answer = false;
        // Not a turn of anyone's conversation: a local backend runs it on its
        // own context and a session's cache is never touched.
        options.side_request = true;
        options.cancellation = cancellation;
        agentloop::NullReporter reporter;
        try {
            outcome.text = agentloop::run(harness, history, options, reporter).answer;
            outcome.ok = true;
        } catch (const harness::CancelledError& e) {
            outcome.error = e.what();
            outcome.retryable = false;
        } catch (const harness::ProviderError& e) {
            // The transport already retried a 429 with its retry-after; a
            // failure that survived that is still worth one more round of
            // longer waits before the batch is given up.
            outcome.error = e.what();
            outcome.retryable = true;
        } catch (const harness::HarnessError& e) {
            outcome.error = e.what();
            outcome.retryable = false;
        }
        return outcome;
    };
}

int effective_parallel(const TeacherResolution& teacher, int requested) noexcept {
    if (requested < 1) {
        requested = 1;
    }
    return teacher.parallel_safe ? std::min(requested, 16) : 1;
}

nlohmann::json dataset_json(const training::DatasetInfo& info) {
    return nlohmann::json{{"name", info.name},
                          {"path", info.path.string()},
                          {"lines", info.lines},
                          {"bytes", info.bytes},
                          {"shape", info.shape}};
}

nlohmann::json kit_json(const training::KitSummary& kit) {
    nlohmann::json out{{"name", kit.name},
                       {"path", kit.path.string()},
                       {"description", kit.description},
                       {"eval_items", kit.eval_items}};
    if (!kit.error.empty()) {
        out["error"] = kit.error;
    }
    return out;
}

}  // namespace apogee::commands
