#include "acp/server.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <exception>
#include <filesystem>
#include <random>
#include <string>
#include <system_error>
#include <utility>

#include "version/version.h"

namespace apogee::acp {

namespace {

/// A session's id: unguessable enough that a stale one never names a new
/// session, and plain to read in a log.
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
        for (const std::unique_ptr<Session>& session : sessions_) {
            if (session->id() == id) {
                session->cancel();
            }
        }
        return;
    }
    if (frame.method == "$/cancel_request" && frame.params.is_object()) {
        // A prompt cancelled by its request id is a prompt cancelled.
        const auto request = frame.params.find("requestId");
        if (request != frame.params.end()) {
            for (const std::unique_ptr<Session>& session : sessions_) {
                if (session->prompting(*request)) {
                    session->cancel();
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
        open_session(frame);
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
                        {{"loadSession", false},
                         {"promptCapabilities",
                          {{"image", false}, {"audio", false}, {"embeddedContext", false}}},
                         {"mcpCapabilities", {{"http", false}, {"sse", false}}}}},
                       {"agentInfo",
                        {{"name", "apogee"},
                         {"title", "Apogee"},
                         {"version", std::string{version::semantic()}}}},
                       {"authMethods", nlohmann::json::array()}}));
}

void Agent::open_session(const Frame& frame) {
    const std::string cwd = string_of(frame.params, "cwd");
    std::error_code code;
    if (cwd.empty() || !std::filesystem::path{cwd}.is_absolute() ||
        !std::filesystem::is_directory(cwd, code)) {
        peer_.send(
            error_response(frame.id, kInvalidParams,
                           "cwd must be an absolute path to a folder that exists: '" + cwd + "'"));
        return;
    }
    if (frame.params.contains("mcpServers") && frame.params["mcpServers"].is_array() &&
        !frame.params["mcpServers"].empty()) {
        diagnostics_.line("the host's " + std::to_string(frame.params["mcpServers"].size()) +
                          " MCP server(s) are accepted and left unused: the host's tools join a "
                          "session in a later Apogee (56b)");
    }
    // One live session: the one before ends, its child's stdin closed.
    for (const std::unique_ptr<Session>& session : sessions_) {
        if (!replaced_.contains(session->id())) {
            replaced_.insert(session->id());
            session->end();
        }
    }
    // A host that keeps one process across chats opens a session per chat:
    // each one whose child has ended, with no question of it left with the
    // host, is let go -- its threads joined, its pipes closed. Its id stays
    // known, so a request naming it is still told it was replaced.
    std::erase_if(sessions_,
                  [](const std::unique_ptr<Session>& session) { return session->finished(); });
    sessions_.push_back(std::make_unique<Session>(mint_session_id(), std::filesystem::path{cwd},
                                                  setup_, host_, peer_, diagnostics_));
    sessions_.back()->open(frame.id);
}

Session* Agent::named(const Frame& frame) {
    const std::string id = string_of(frame.params, "sessionId");
    if (replaced_.contains(id)) {
        peer_.send(error_response(frame.id, kInvalidParams,
                                  "session " + id + " has ended: a newer session replaced it"));
        return nullptr;
    }
    for (const std::unique_ptr<Session>& session : sessions_) {
        if (session->id() == id) {
            return session.get();
        }
    }
    peer_.send(error_response(frame.id, kInvalidParams, "no session '" + id + "'"));
    return nullptr;
}

void Agent::finish() {
    if (finished_) {
        return;
    }
    finished_ = true;
    for (const std::unique_ptr<Session>& session : sessions_) {
        session->end();
    }
    // Requests the host never answered reach nobody now: each session is
    // ending, so an answer would go nowhere.
    peer_.close();
    for (const std::unique_ptr<Session>& session : sessions_) {
        session->wait(std::chrono::seconds{30});
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
