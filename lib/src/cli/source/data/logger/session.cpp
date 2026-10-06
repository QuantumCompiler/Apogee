#include "logger/session.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <chrono>
#include <fstream>
#include <iomanip>
#include <random>
#include <sstream>
#include <stdexcept>

#include "contracts/config_edit.h"
#include "contracts/layout.h"
#include "contracts/paths.h"

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
        case WarningKind::SuiteMissing:
            return "suite_missing";
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
    if (session.private_chat) {
        out["private"] = true;
    }
    if (!session.custom_name.empty()) {
        out["custom_name"] = session.custom_name;
    }
    if (!session.retriever.empty()) {
        out["retriever"] = session.retriever;
    }
    if (!session.rerank.empty()) {
        out["rerank"] = session.rerank;
    }
    if (session.suite.has_value()) {
        out["suite"] = *session.suite;
    }
    if (!session.task.empty()) {
        out["task"] = session.task;
    }
    if (!session.title.empty()) {
        out["title"] = session.title;
    }
    if (!session.provider_session_id.empty()) {
        out["provider_session_id"] = session.provider_session_id;
    }
    if (!session.attachments.empty()) {
        nlohmann::json attachments = nlohmann::json::array();
        for (const Attachment& attachment : session.attachments) {
            nlohmann::json files = nlohmann::json::array();
            for (const AttachedFile& file : attachment.files) {
                files.push_back({{"name", file.name},
                                 {"path", file.path},
                                 {"sha256", file.sha256},
                                 {"reader", file.reader},
                                 {"bytes", file.bytes}});
            }
            nlohmann::json entry{{"name", attachment.name}, {"files", std::move(files)}};
            if (attachment.inline_at.has_value()) {
                entry["inline_at"] = *attachment.inline_at;
            }
            if (const std::optional<std::size_t> at = attachment.map_at; at.has_value()) {
                entry["map_at"] = *at;
            }
            if (const std::optional<AttachmentGraph> part = attachment.graph; part.has_value()) {
                // Which part of the index's code graph is this attachment's,
                // and what its notice said (27n) -- never the graph itself.
                nlohmann::json graph = nlohmann::json::object();
                if (!part->label.empty()) {
                    graph["label"] = part->label;
                    graph["supported"] = part->supported;
                    graph["skipped"] = part->skipped;
                }
                if (!part->absent.empty()) {
                    graph["absent"] = part->absent;
                }
                entry["graph"] = std::move(graph);
            }
            attachments.push_back(std::move(entry));
        }
        out["attachments"] = std::move(attachments);
    }

    nlohmann::json params = nlohmann::json::object();
    if (session.params.temperature.has_value()) {
        params["temperature"] = *session.params.temperature;
    }
    if (session.params.max_tokens.has_value()) {
        params["max_tokens"] = *session.params.max_tokens;
    }
    if (session.params.thinking.has_value()) {
        params["think"] = harness::to_string(*session.params.thinking);
    }
    if (session.params.thinking_budget.has_value()) {
        params["think_budget"] = *session.params.thinking_budget;
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
    if (const auto it = parsed.find("suite"); it != parsed.end() && !it->is_null()) {
        if (it->is_string()) {
            session.suite = it->get<std::string>();
        } else {
            loaded.warnings.push_back(
                ResumeWarning{WarningKind::FieldDropped, "suite",
                              "field 'suite' had an unexpected shape and was ignored"});
        }
    }
    // A suite since deleted: resume under the config's default, and say so.
    // "" is a suite turned off, not a name, and is never checked.
    if (known.check_suites && session.suite.has_value() && !session.suite->empty() &&
        std::none_of(known.suites.begin(), known.suites.end(), [&](const std::string& name) {
            return name.size() == session.suite->size() &&
                   std::equal(name.begin(), name.end(), session.suite->begin(), [](char a, char b) {
                       return std::tolower(static_cast<unsigned char>(a)) ==
                              std::tolower(static_cast<unsigned char>(b));
                   });
        })) {
        loaded.warnings.push_back({WarningKind::SuiteMissing, *session.suite,
                                   "suite '" + *session.suite +
                                       "' is no longer configured -- resuming under the "
                                       "config's default suite, if it has one"});
        session.suite.reset();
    }
    session.task = string_field(parsed, "task", loaded.warnings);
    session.provider_session_id = string_field(parsed, "provider_session_id", loaded.warnings);
    session.started_at = string_field(parsed, "started_at", loaded.warnings);
    session.updated_at = string_field(parsed, "updated_at", loaded.warnings);
    session.turns = parsed.value("turns", 0);
    session.compactions = parsed.value("compactions", 0);
    if (const auto it = parsed.find("private"); it != parsed.end() && it->is_boolean()) {
        session.private_chat = it->get<bool>();
    }

    if (const auto params = parsed.find("params"); params != parsed.end() && params->is_object()) {
        if (const auto it = params->find("temperature"); it != params->end() && it->is_number()) {
            session.params.temperature = it->get<double>();
        }
        if (const auto it = params->find("max_tokens");
            it != params->end() && it->is_number_integer()) {
            session.params.max_tokens = it->get<std::int64_t>();
        }
        if (const auto it = params->find("think"); it != params->end() && it->is_string()) {
            session.params.thinking = harness::thinking_mode_from_string(it->get<std::string>());
        }
        if (const auto it = params->find("think_budget");
            it != params->end() && it->is_number_integer()) {
            session.params.thinking_budget = it->get<std::int64_t>();
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

    if (const auto attachments = parsed.find("attachments");
        attachments != parsed.end() && attachments->is_array()) {
        for (const auto& entry : *attachments) {
            try {
                Attachment attachment;
                attachment.name = entry.at("name").get<std::string>();
                for (const auto& file : entry.at("files")) {
                    attachment.files.push_back(
                        AttachedFile{.name = file.at("name").get<std::string>(),
                                     .path = file.value("path", std::string{}),
                                     .sha256 = file.at("sha256").get<std::string>(),
                                     .reader = file.value("reader", std::string{}),
                                     .bytes = file.value("bytes", std::uint64_t{0})});
                }
                if (const auto at = entry.find("inline_at");
                    at != entry.end() && at->is_number_unsigned()) {
                    attachment.inline_at = at->get<std::size_t>();
                }
                if (const auto at = entry.find("map_at");
                    at != entry.end() && at->is_number_unsigned()) {
                    attachment.map_at = at->get<std::size_t>();
                }
                if (const auto graph = entry.find("graph");
                    graph != entry.end() && graph->is_object()) {
                    // A graph record that cannot be read costs the graph's
                    // line, never the attachment: the next attach rebuilds it.
                    try {
                        AttachmentGraph read;
                        read.label = graph->value("label", std::string{});
                        read.absent = graph->value("absent", std::string{});
                        if (const auto supported = graph->find("supported");
                            supported != graph->end() && supported->is_object()) {
                            read.supported = supported->get<std::map<std::string, std::int64_t>>();
                        }
                        if (const auto skipped = graph->find("skipped");
                            skipped != graph->end() && skipped->is_object()) {
                            read.skipped = skipped->get<std::map<std::string, std::int64_t>>();
                        }
                        if (!read.label.empty() || !read.absent.empty()) {
                            attachment.graph = std::move(read);
                        }
                    } catch (const std::exception&) {
                        attachment.graph.reset();
                    }
                }
                session.attachments.push_back(std::move(attachment));
            } catch (const std::exception&) {
                loaded.warnings.push_back(
                    ResumeWarning{WarningKind::FieldDropped, "attachments",
                                  "an attachment could not be read and was skipped"});
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

std::string transcript_text(const std::vector<harness::ChatMessage>& messages) {
    std::string out;
    for (const harness::ChatMessage& message : messages) {
        std::string_view label;
        if (message.role == harness::Role::User) {
            label = "User: ";
        } else if (message.role == harness::Role::Assistant) {
            label = "Assistant: ";
        } else {
            continue;
        }
        const std::string text = message.content.plain_text();
        std::size_t begin = 0;
        while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0) {
            ++begin;
        }
        std::size_t end = text.size();
        while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0) {
            --end;
        }
        if (begin == end) {
            continue;  // an assistant turn that only called tools
        }
        out += out.empty() ? "" : "\n\n";
        out += label;
        out += text.substr(begin, end - begin);
    }
    return out;
}

}  // namespace apogee::logger
