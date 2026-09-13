#include "logger/session.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#include "harness/config_edit.h"
#include "harness/layout.h"
#include "harness/paths.h"

namespace apogee::logger {
namespace {

std::string timestamp_now() {
    const auto now = std::chrono::system_clock::now();
    const std::time_t as_time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &as_time);
#else
    gmtime_r(&as_time, &utc);
#endif
    std::ostringstream out;
    out << std::put_time(&utc, "%Y-%m-%dT%H:%M:%SZ");
    return out.str();
}

/// Reads a string field, recording a warning rather than throwing when it is
/// the wrong shape.
std::string string_field(const nlohmann::json& object, std::string_view key,
                         std::vector<ResumeWarning>& warnings) {
    const auto it = object.find(key);
    if (it == object.end() || it->is_null()) {
        return {};
    }
    if (!it->is_string()) {
        warnings.push_back(ResumeWarning{
            WarningKind::FieldDropped, std::string{key},
            "field '" + std::string{key} + "' had an unexpected shape and was ignored"});
        return {};
    }
    return it->get<std::string>();
}

}  // namespace

std::string_view to_string(WarningKind kind) noexcept {
    switch (kind) {
        case WarningKind::SchemaLegacy:
            return "schema_legacy";
        case WarningKind::BackendMissing:
            return "backend_missing";
        case WarningKind::FieldDropped:
            return "field_dropped";
        case WarningKind::RerankBackendMissing:
            return "rerank_backend_missing";
    }
    return "unknown";
}

std::string Session::display_name() const {
    if (!custom_name.empty()) {
        return custom_name;
    }
    if (!title.empty()) {
        return title;
    }
    return chat_id;
}

std::filesystem::path sessions_dir() {
    // Forwards to harness/layout.h -- the single declaration of the layout.
    return harness::sessions_dir();
}

std::filesystem::path session_path(const std::string& chat_id) {
    return sessions_dir() / (chat_id + ".json");
}

std::string new_chat_id() {
    // Time-ordered plus random: the prefix makes a directory listing sort
    // chronologically, and the suffix keeps two sessions started in the same
    // second from colliding.
    const auto now = std::chrono::system_clock::now();
    const std::time_t as_time = std::chrono::system_clock::to_time_t(now);
    std::tm utc{};
#if defined(_WIN32)
    gmtime_s(&utc, &as_time);
#else
    gmtime_r(&as_time, &utc);
#endif

    std::ostringstream out;
    out << std::put_time(&utc, "%Y%m%d-%H%M%S");

    std::random_device entropy;
    out << "-" << std::hex << std::setw(4) << std::setfill('0') << (entropy() & 0xFFFFU);
    return out.str();
}

std::string serialize(const Session& session) {
    nlohmann::json out{
        {"schema_version", kCurrentSchemaVersion},
        {"chat_id", session.chat_id},
        {"backend", session.backend},
        {"started_at", session.started_at},
        {"updated_at", session.updated_at},
        {"turns", session.turns},
        {"compactions", session.compactions},
        {"messages", session.messages},
    };
    if (!session.custom_name.empty()) {
        out["custom_name"] = session.custom_name;
    }
    if (!session.retriever.empty()) {
        out["retriever"] = session.retriever;
    }
    if (!session.rerank.empty()) {
        out["rerank"] = session.rerank;
    }
    if (!session.title.empty()) {
        out["title"] = session.title;
    }
    if (!session.provider_session_id.empty()) {
        out["provider_session_id"] = session.provider_session_id;
    }

    nlohmann::json params = nlohmann::json::object();
    if (session.params.temperature.has_value()) {
        params["temperature"] = *session.params.temperature;
    }
    if (session.params.max_tokens.has_value()) {
        params["max_tokens"] = *session.params.max_tokens;
    }
    if (!session.params.system_prompt.empty()) {
        params["system_prompt"] = session.params.system_prompt;
    }
    if (!params.empty()) {
        out["params"] = std::move(params);
    }

    // Indented: a session file is something a user opens to see what was said,
    // and a one-line blob is not that.
    return out.dump(2) + "\n";
}

LoadedSession deserialize(std::string_view text, const KnownDependencies& known) {
    LoadedSession loaded;

    const nlohmann::json parsed = nlohmann::json::parse(text, nullptr, false);
    if (parsed.is_discarded() || !parsed.is_object()) {
        throw std::runtime_error("the session file is not valid JSON");
    }

    Session& session = loaded.session;
    session.schema_version = parsed.value("schema_version", 0);
    session.chat_id = string_field(parsed, "chat_id", loaded.warnings);
    session.custom_name = string_field(parsed, "custom_name", loaded.warnings);
    session.title = string_field(parsed, "title", loaded.warnings);
    session.backend = string_field(parsed, "backend", loaded.warnings);
    session.retriever = string_field(parsed, "retriever", loaded.warnings);
    session.rerank = string_field(parsed, "rerank", loaded.warnings);
    // A judge that has since been deleted: resume without reranking, and say
    // so. `off` is a setting, not a backend, and is never checked.
    if (!known.backends.empty() && !session.rerank.empty() && session.rerank != "off" &&
        std::find(known.backends.begin(), known.backends.end(), session.rerank) ==
            known.backends.end()) {
        loaded.warnings.push_back({WarningKind::RerankBackendMissing, session.rerank,
                                   "rerank backend '" + session.rerank +
                                       "' is no longer configured -- resuming without reranking"});
        session.rerank.clear();
    }
    session.provider_session_id = string_field(parsed, "provider_session_id", loaded.warnings);
    session.started_at = string_field(parsed, "started_at", loaded.warnings);
    session.updated_at = string_field(parsed, "updated_at", loaded.warnings);
    session.turns = parsed.value("turns", 0);
    session.compactions = parsed.value("compactions", 0);

    if (const auto params = parsed.find("params"); params != parsed.end() && params->is_object()) {
        if (const auto it = params->find("temperature"); it != params->end() && it->is_number()) {
            session.params.temperature = it->get<double>();
        }
        if (const auto it = params->find("max_tokens");
            it != params->end() && it->is_number_integer()) {
            session.params.max_tokens = it->get<std::int64_t>();
        }
        session.params.system_prompt = string_field(*params, "system_prompt", loaded.warnings);
    }

    if (const auto messages = parsed.find("messages");
        messages != parsed.end() && messages->is_array()) {
        for (const auto& entry : *messages) {
            try {
                session.messages.push_back(entry.get<harness::ChatMessage>());
            } catch (const std::exception&) {
                // One unreadable message must not cost the whole conversation.
                loaded.warnings.push_back(
                    ResumeWarning{WarningKind::FieldDropped, "messages",
                                  "a message could not be read and was skipped"});
            }
        }
    }

    if (session.schema_version == 0) {
        loaded.warnings.push_back(ResumeWarning{
            WarningKind::SchemaLegacy,
            {},
            "this chat predates session metadata; resuming best-effort with current defaults"});
    }

    if (!known.backends.empty() && !session.backend.empty() &&
        std::find(known.backends.begin(), known.backends.end(), session.backend) ==
            known.backends.end()) {
        loaded.warnings.push_back(
            ResumeWarning{WarningKind::BackendMissing, session.backend,
                          "backend '" + session.backend +
                              "' is no longer configured; using the default instead"});
    }

    return loaded;
}

void save(const Session& session) {
    Session stamped = session;
    stamped.schema_version = kCurrentSchemaVersion;
    if (stamped.started_at.empty()) {
        stamped.started_at = timestamp_now();
    }
    stamped.updated_at = timestamp_now();

    // The same atomic path the config engine uses: an interrupted write leaves
    // the previous session intact rather than a truncated file.
    harness::write_file_atomically(session_path(stamped.chat_id), serialize(stamped));
}

LoadedSession load(const std::string& name, const KnownDependencies& known) {
    const std::filesystem::path direct = session_path(name);
    std::filesystem::path found;

    if (std::filesystem::exists(direct)) {
        found = direct;
    } else {
        // Not an id: try a custom name or title. A user who renamed a chat
        // should be able to resume it by the name they gave it.
        for (const Session& candidate : list_sessions()) {
            if (candidate.custom_name == name || candidate.title == name) {
                found = session_path(candidate.chat_id);
                break;
            }
        }
    }

    if (found.empty()) {
        throw std::runtime_error("no chat named '" + name + "'");
    }

    std::ifstream in(found, std::ios::binary);
    if (!in) {
        throw std::runtime_error(found.string() + ": cannot open");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return deserialize(buffer.str(), known);
}

std::vector<Session> list_sessions() {
    std::vector<Session> sessions;

    std::error_code ec;
    const std::filesystem::path directory = sessions_dir();
    if (!std::filesystem::exists(directory, ec)) {
        return sessions;
    }

    for (const auto& entry : std::filesystem::directory_iterator{directory, ec}) {
        if (ec || !entry.is_regular_file() || entry.path().extension() != ".json") {
            continue;
        }
        std::ifstream in(entry.path(), std::ios::binary);
        if (!in) {
            continue;
        }
        std::ostringstream buffer;
        buffer << in.rdbuf();
        try {
            // A corrupt file is skipped rather than fatal: one bad session must
            // not make `apogee chat list` unusable.
            sessions.push_back(deserialize(buffer.str(), {}).session);
        } catch (const std::exception&) {
            continue;
        }
    }

    std::ranges::sort(sessions, [](const Session& a, const Session& b) {
        return a.updated_at > b.updated_at;  // newest first
    });
    return sessions;
}

std::optional<Session> most_recent() {
    const std::vector<Session> sessions = list_sessions();
    if (sessions.empty()) {
        return std::nullopt;
    }
    return sessions.front();
}

}  // namespace apogee::logger
