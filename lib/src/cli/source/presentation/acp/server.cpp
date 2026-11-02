#include "acp/server.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <exception>
#include <filesystem>
#include <optional>
#include <random>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

#include "acp/host_servers.h"
#include "version/version.h"

namespace apogee::acp {

namespace {

/// How long a session's child is given to end -- when the host leaves, or a
/// chat it still holds is opened again -- before it is terminated.
constexpr std::chrono::seconds kEndBudget{30};

/// A session's id until its child names its chat: unguessable enough that a stale one never names a
/// new session, and plain to read in a log.
[[nodiscard]] std::string mint_session_id() {
    static thread_local std::mt19937_64 random{std::random_device{}()};
    static constexpr std::string_view kHex = "0123456789abcdef";
    std::string id = "apogee-";
    std::uint64_t value = random();
    for (int i = 0; i < 16; ++i) {
        id += kHex[static_cast<std::size_t>(value & 0xFU)];
        value >>= 4U;
    }
    return id;
}

[[nodiscard]] std::string string_of(const nlohmann::json& object, std::string_view key) {
    if (!object.is_object()) {
        return {};
    }
    const auto found = object.find(std::string{key});
    return found != object.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

/// Why a `session/load` or `session/resume` naming no chat's id is refused:
/// a chat's name or title, or a path (56c).
[[nodiscard]] std::string no_chat_with_id(const std::string& id) {
    return "no chat has the id '" + id +
           "': a saved chat opens by its id, as session/list gives it, never by its name";
}

/// Whether `object.<outer>.<inner>` is present and not null -- how ACP
/// advertises a capability object.
[[nodiscard]] bool advertised(const nlohmann::json& object, std::string_view outer,
                              std::string_view inner) {
    if (!object.is_object()) {
        return false;
    }
    const auto first = object.find(std::string{outer});
    if (first == object.end() || !first->is_object()) {
        return false;
    }
    const auto second = first->find(std::string{inner});
    return second != first->end() && !second->is_null();
}

}  // namespace

Agent::Agent(AgentSetup setup, std::ostream& out, std::ostream& err)
    : setup_{std::move(setup)}, peer_{out}, diagnostics_{err} {}

Agent::~Agent() {
    finish();
}

void Agent::handle(std::string_view line) {
    if (line.empty() || line.find_first_not_of(" \t\r") == std::string_view::npos) {
        return;  // a blank line is no message, and needs no answer
    }
    const Frame frame = parse_frame(line);
    switch (frame.kind) {
        case Frame::Kind::Unparsable:
            peer_.send(error_response(nullptr, kParseError, "parse error: " + frame.problem));
            return;
        case Frame::Kind::Invalid:
            peer_.send(
                error_response(frame.id, kInvalidRequest, "invalid request: " + frame.problem));
            return;
        case Frame::Kind::Response:
            if (!peer_.deliver(frame)) {
                diagnostics_.line("a response to no request of ours was dropped");
            }
            return;
        case Frame::Kind::Notification:
            try {
                notify(frame);
            } catch (const std::exception& e) {
                diagnostics_.line(frame.method + " was dropped: " + e.what());
            }
            return;
        case Frame::Kind::Request:
            // A request whose params are not what its method takes is
            // answered, never the end of the agent.
            try {
                dispatch(frame);
            } catch (const std::exception& e) {
                peer_.send(error_response(frame.id, kInternalError,
                                          frame.method + " failed: " + e.what()));
            }
            return;
    }
}

void Agent::notify(const Frame& frame) {
    if (frame.method == "session/cancel") {
        const std::string id = string_of(frame.params, "sessionId");
        for (Held& held : sessions_) {
            if (!held.retired && held.session->id() == id) {
                held.session->cancel();
            }
        }
        return;
    }
    if (frame.method == "$/cancel_request" && frame.params.is_object()) {
        // A prompt cancelled by its request id is a prompt cancelled.
        const auto request = frame.params.find("requestId");
        if (request != frame.params.end()) {
            for (Held& held : sessions_) {
                if (held.session->prompting(*request)) {
                    held.session->cancel();
                }
            }
        }
        return;
    }
    // Any other notification needs no answer, and this agent acts on none.
}

void Agent::dispatch(const Frame& frame) {
    if (frame.method == "initialize") {
        initialize(frame);
        return;
    }
    if (frame.method == "session/new") {
        open_session(frame, OpenAs::New);
        return;
    }
    if (frame.method == "session/load") {
        open_session(frame, OpenAs::Load);
        return;
    }
    if (frame.method == "session/resume") {
        open_session(frame, OpenAs::Resume);
        return;
    }
    if (frame.method == "session/list") {
        list_sessions(frame);
        return;
    }
    if (frame.method == "session/close") {
        close_session(frame);
        return;
    }
    if (frame.method == "session/prompt" || frame.method == "session/set_mode" ||
        frame.method == "session/set_config_option") {
        Session* session = named(frame);
        if (session == nullptr) {
            return;
        }
        if (frame.method == "session/prompt") {
            session->prompt(frame.id, frame.params);
        } else if (frame.method == "session/set_mode") {
            session->set_mode(frame.id, frame.params);
        } else {
            session->set_config_option(frame.id, frame.params);
        }
        return;
    }
    peer_.send(error_response(frame.id, kMethodNotFound, "method not found: " + frame.method));
}

void Agent::initialize(const Frame& frame) {
    const nlohmann::json& params = frame.params;
    nlohmann::json capabilities = nlohmann::json::object();
    if (params.is_object()) {
        if (const auto found = params.find("clientCapabilities");
            found != params.end() && found->is_object()) {
            capabilities = *found;
        }
        if (const auto info = params.find("clientInfo"); info != params.end()) {
            host_.name = string_of(*info, "name");
            host_.version = string_of(*info, "version");
        }
    }
    host_.notices = advertised(capabilities, "session", "notices");
    host_.elicitation_form = advertised(capabilities, "elicitation", "form");
    if (const auto asked = params.is_object() ? params.find("protocolVersion") : params.end();
        params.is_object() && asked != params.end() && *asked != kProtocolVersion) {
        diagnostics_.line("the host asked for protocol version " + asked->dump() + "; answered " +
                          std::to_string(kProtocolVersion));
    }
    peer_.send(response(
        frame.id,
        nlohmann::json{{"protocolVersion", kProtocolVersion},
                       {"agentCapabilities",
                        // Saved sessions (56c): each advertised only once it
                        // works, or a host would reopen chats into errors.
                        {{"loadSession", true},
                         {"sessionCapabilities",
                          {{"list", nlohmann::json::object()},
                           {"resume", nlohmann::json::object()},
                           {"close", nlohmann::json::object()}}},
                         {"promptCapabilities",
                          {{"image", false}, {"audio", false}, {"embeddedContext", false}}},
                         {"mcpCapabilities", {{"http", false}, {"sse", false}}}}},
                       {"agentInfo",
                        {{"name", "apogee"},
                         {"title", "Apogee"},
                         {"version", std::string{version::semantic()}}}},
                       {"authMethods", nlohmann::json::array()}}));
}

std::string Agent::folder(const Frame& frame) {
    std::string cwd = string_of(frame.params, "cwd");
    std::error_code code;
    if (cwd.empty() || !std::filesystem::path{cwd}.is_absolute() ||
        !std::filesystem::is_directory(cwd, code)) {
        peer_.send(
            error_response(frame.id, kInvalidParams,
                           "cwd must be an absolute path to a folder that exists: '" + cwd + "'"));
        return {};
    }
    return cwd;
}

void Agent::open_session(const Frame& frame, OpenAs as) {
    const std::string cwd = folder(frame);
    if (cwd.empty()) {
        return;
    }
    // A saved chat (56c): any chat's id, since the store is one -- its id
    // alone. The store reads a name or a title too, as `--resume` does, but
    // a session's id is its chat's, so the session holding a chat is found
    // by that id: a chat opened by another spelling would slip past it, and
    // two children would write one chat. It is read first, so one no chat
    // answers to is refused before anything ends.
    std::string id;
    std::vector<SavedEntry> replay;
    if (as != OpenAs::New) {
        id = string_of(frame.params, "sessionId");
        if (id.empty()) {
            peer_.send(
                error_response(frame.id, kInvalidParams, "sessionId names the saved chat to open"));
            return;
        }
        const auto refuse = [&](const std::string& message) {
            nlohmann::json refused = error_response(frame.id, kResourceNotFound, message);
            refused["error"]["data"] = nlohmann::json{{"sessionId", id}};
            peer_.send(refused);
        };
        // A chat's id names a file in the store, never a path to one.
        if (id.find_first_of("/\\") != std::string::npos) {
            refuse(no_chat_with_id(id));
            return;
        }
        const auto read = [&]() -> std::optional<std::vector<SavedEntry>> {
            std::string error;
            std::optional<SavedChat> chat;
            if (setup_.chats.read) {
                chat = setup_.chats.read(id, error);
            }
            if (!chat.has_value()) {
                refuse(error.empty() ? "no chat named '" + id + "'" : error);
                return std::nullopt;
            }
            if (chat->id != id) {
                refuse(no_chat_with_id(id));  // a chat's name or title
                return std::nullopt;
            }
            return std::move(chat->entries);
        };
        std::optional<std::vector<SavedEntry>> saved = read();
        if (!saved.has_value()) {
            return;
        }
        // A session of this chat still held -- live, or still ending -- is
        // ended and its child waited for: one chat, one writer. Its id is
        // the chat's, as `id` is. The chat is read again after it, holding
        // whatever that child saved last.
        bool waited = false;
        for (Held& held : sessions_) {
            if (held.session->id() == id) {
                if (!held.retired) {
                    retire(held, "it was opened again");
                }
                held.session->wait(kEndBudget);
                waited = true;
            }
        }
        if (waited) {
            saved = read();
            if (!saved.has_value()) {
                return;
            }
        }
        if (as == OpenAs::Load) {
            replay = std::move(*saved);
        }
        ended_.erase(id);
    }
    // The host's MCP servers (56b): this session's alone, handed to its
    // child -- the stdio ones taken, the rest set aside for its notice.
    const HostServers servers =
        host_servers(frame.params.value("mcpServers", nlohmann::json::array()));
    // One live session: the one before ends, its child's stdin closed.
    for (Held& held : sessions_) {
        if (!held.retired) {
            retire(held, "a newer session replaced it");
        }
    }
    // A host that keeps one process across chats opens a session per chat:
    // each one whose child has ended, with no question of it left with the
    // host, is let go -- its threads joined, its pipes closed. Its id stays
    // known, so a request naming it is still told why it ended.
    std::erase_if(sessions_,
                  [](const Held& held) { return held.retired && held.session->finished(); });
    sessions_.push_back(Held{.session = std::make_unique<Session>(
                                 as == OpenAs::New ? mint_session_id() : id,
                                 std::filesystem::path{cwd}, setup_, host_, peer_, diagnostics_),
                             .retired = false});
    sessions_.back().session->open(frame.id, servers, as, std::move(replay));
}

void Agent::list_sessions(const Frame& frame) {
    std::string cwd;
    if (frame.params.is_object()) {
        if (const auto found = frame.params.find("cwd");
            found != frame.params.end() && !found->is_null()) {
            if (!found->is_string() ||
                !std::filesystem::path{found->get<std::string>()}.is_absolute()) {
                peer_.send(error_response(frame.id, kInvalidParams,
                                          "cwd, when given, is an absolute path"));
                return;
            }
            cwd = found->get<std::string>();
        }
    }
    // One page holds every chat, so it carries no nextCursor -- and a page
    // after it, asked for anyway, is empty.
    const bool later_page = frame.params.is_object() && frame.params.contains("cursor") &&
                            !frame.params.at("cursor").is_null();
    nlohmann::json sessions = nlohmann::json::array();
    if (!later_page && setup_.chats.list) {
        sessions = listed_sessions(setup_.chats.list(), cwd);
    }
    peer_.send(response(frame.id, nlohmann::json{{"sessions", std::move(sessions)}}));
}

void Agent::close_session(const Frame& frame) {
    // Its turn cancelled and its child ended; its chat is already saved,
    // after its last answered turn. An unknown or closed session is closed.
    const std::string id = string_of(frame.params, "sessionId");
    for (Held& held : sessions_) {
        if (!held.retired && held.session->id() == id) {
            retire(held, "the host closed it");
        }
    }
    peer_.send(response(frame.id, nlohmann::json::object()));
}

void Agent::retire(Held& held, std::string why) {
    held.retired = true;
    ended_.insert_or_assign(held.session->id(), std::move(why));
    held.session->end();
}

Session* Agent::named(const Frame& frame) {
    const std::string id = string_of(frame.params, "sessionId");
    for (Held& held : sessions_) {
        if (!held.retired && held.session->id() == id) {
            return held.session.get();
        }
    }
    if (const auto ended = ended_.find(id); ended != ended_.end()) {
        peer_.send(error_response(frame.id, kInvalidParams,
                                  "session " + id + " has ended: " + ended->second));
        return nullptr;
    }
    peer_.send(error_response(frame.id, kInvalidParams, "no session '" + id + "'"));
    return nullptr;
}

void Agent::finish() {
    if (finished_) {
        return;
    }
    finished_ = true;
    for (Held& held : sessions_) {
        held.session->end();
    }
    // Requests the host never answered reach nobody now: each session is
    // ending, so an answer would go nowhere.
    peer_.close();
    for (Held& held : sessions_) {
        held.session->wait(kEndBudget);
    }
}

int serve_acp(AgentSetup setup, std::istream& in, std::ostream& out, std::ostream& err) {
    Agent agent{std::move(setup), out, err};
    std::string line;
    while (std::getline(in, line)) {
        agent.handle(line);
    }
    agent.finish();
    return 0;
}

}  // namespace apogee::acp
