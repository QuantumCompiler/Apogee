#include "support/fake_mcp_server.h"

#include <thread>

namespace apogee::testing {

FakeMcpTransport::FakeMcpTransport(Personality personality, std::string name)
    : personality_{personality}, name_{std::move(name)} {
    if (personality_ == Personality::Chatty || personality_ == Personality::Dying) {
        tail_.write("[9999] NOISE_DISCOVERING_OAUTH_CONFIGURATION\n");
        tail_.write("[9999] NOISE_USING_EXISTING_CLIENT_PORT: 3736\n");
    }
    if (personality_ == Personality::Dying) {
        tail_.write("fatal: could not reach upstream\n");
        // NOT hung up yet: a real child that dies mid-handshake still takes
        // the initialize frame into its pipe buffer; only the READ then hits
        // EOF. That is the read loop's error path, the one that must release
        // the waiter -- a refused write would let the send path do it.
    }
}

nlohmann::json FakeMcpTransport::tools_list() {
    return nlohmann::json{
        {"tools", nlohmann::json::array(
                      {{{"name", "echo"},
                        {"description", "Echo the text"},
                        {"inputSchema",
                         {{"type", "object"}, {"properties", {{"text", {{"type", "string"}}}}}}},
                        {"annotations", {{"readOnlyHint", true}}}},
                       {{"name", "write"},
                        {"description", "Write something"},
                        {"inputSchema", {{"type", "object"}}}}})}};
}

void FakeMcpTransport::reply(const nlohmann::json& request) {
    const std::string method = request.value("method", std::string{});
    if (!request.contains("id")) {
        return;  // a notification gets no reply
    }
    const nlohmann::json id = request["id"];
    if (personality_ == Personality::Malformed) {
        outbox_.push_back("this is not json");
        outbox_.push_back("{\"jsonrpc\":\"2.0\",\"id\":999999,\"result\":{}}");
        outbox_.push_back("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/tools/list_changed\"}");
    }
    if (method == "initialize") {
        outbox_.push_back(
            mcp::make_result(id, {{"protocolVersion", "2025-03-26"},
                                  {"capabilities", {{"tools", nlohmann::json::object()}}},
                                  {"serverInfo", {{"name", name_}, {"version", "0.1"}}}})
                .dump());
    } else if (method == "tools/list") {
        outbox_.push_back(mcp::make_result(id, tools_list()).dump());
    } else if (method == "tools/call") {
        const nlohmann::json params = request.value("params", nlohmann::json::object());
        const std::string name = params.value("name", std::string{});
        const nlohmann::json args = params.value("arguments", nlohmann::json::object());
        if (personality_ == Personality::Erroring) {
            outbox_.push_back(
                mcp::make_result(
                    id, {{"content", nlohmann::json::array(
                                         {{{"type", "text"}, {"text", "the server declined"}}})},
                         {"isError", true}})
                    .dump());
        } else if (name == "echo") {
            outbox_.push_back(
                mcp::make_result(
                    id, {{"content", nlohmann::json::array(
                                         {{{"type", "text"},
                                           {"text", "echo: " + args.value("text", std::string{})}},
                                          {{"type", "image"}, {"data", "ignored"}}})},
                         {"isError", false}})
                    .dump());
        } else {
            outbox_.push_back(
                mcp::make_error(id, mcp::kMethodNotFound, "unknown tool " + name).dump());
        }
    } else {
        outbox_.push_back(
            mcp::make_error(id, mcp::kMethodNotFound, "method not found: " + method).dump());
    }
}

bool FakeMcpTransport::send(const nlohmann::json& message) {
    const std::lock_guard<std::mutex> lock{mutex_};
    if (closed_ || hung_up_) {
        return false;
    }
    sent_.push_back(message);
    if (personality_ == Personality::Slow) {
        return true;  // accepted, never answered
    }
    if (personality_ == Personality::Dying) {
        hung_up_ = true;  // took the frame, then died: the next recv is EOF
        return true;
    }
    if (personality_ == Personality::SlowCalls &&
        message.value("method", std::string{}) == "tools/call") {
        return true;  // the handshake works; a call hangs
    }
    reply(message);
    return true;
}

std::optional<std::string> FakeMcpTransport::recv(std::chrono::milliseconds timeout) {
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        if (closed_ || hung_up_) {
            return std::nullopt;
        }
        if (!outbox_.empty()) {
            std::string line = std::move(outbox_.front());
            outbox_.pop_front();
            return line;
        }
    }
    std::this_thread::sleep_for(std::min(timeout, std::chrono::milliseconds{10}));
    return std::string{};
}

std::string FakeMcpTransport::stderr_tail() const {
    return tail_.tail();
}

void FakeMcpTransport::close() {
    const std::lock_guard<std::mutex> lock{mutex_};
    closed_ = true;
}

std::vector<nlohmann::json> FakeMcpTransport::sent() const {
    const std::lock_guard<std::mutex> lock{mutex_};
    return sent_;
}

bool FakeMcpTransport::closed() const {
    const std::lock_guard<std::mutex> lock{mutex_};
    return closed_;
}

std::function<std::unique_ptr<mcp::Transport>(const mcp::ServerSpec&, mcp::StderrTail::Sink,
                                              std::string&)>
fake_fleet(std::vector<std::shared_ptr<FakeMcpTransport>>* made) {
    return [made](const mcp::ServerSpec& spec, mcp::StderrTail::Sink,
                  std::string& error) -> std::unique_ptr<mcp::Transport> {
        FakeMcpTransport::Personality personality = FakeMcpTransport::Personality::Well;
        if (spec.command == "chatty") {
            personality = FakeMcpTransport::Personality::Chatty;
        } else if (spec.command == "slow") {
            personality = FakeMcpTransport::Personality::Slow;
        } else if (spec.command == "dying") {
            personality = FakeMcpTransport::Personality::Dying;
        } else if (spec.command == "malformed") {
            personality = FakeMcpTransport::Personality::Malformed;
        } else if (spec.command == "erroring") {
            personality = FakeMcpTransport::Personality::Erroring;
        } else if (spec.command == "slowcalls") {
            personality = FakeMcpTransport::Personality::SlowCalls;
        } else if (spec.command == "missing") {
            error = "could not start missing: no such file";
            return nullptr;
        }
        // The transport is owned by the client; a test keeps a handle to
        // inspect it. A wrapper forwards to the shared fake.
        struct Forwarder final : mcp::Transport {
            std::shared_ptr<FakeMcpTransport> inner;
            explicit Forwarder(std::shared_ptr<FakeMcpTransport> fake) : inner{std::move(fake)} {}
            bool send(const nlohmann::json& m) override {
                return inner->send(m);
            }
            std::optional<std::string> recv(std::chrono::milliseconds t) override {
                return inner->recv(t);
            }
            std::string stderr_tail() const override {
                return inner->stderr_tail();
            }
            void close() override {
                inner->close();
            }
        };
        auto fake = std::make_shared<FakeMcpTransport>(personality, spec.name);
        if (made != nullptr) {
            made->push_back(fake);
        }
        return std::make_unique<Forwarder>(fake);
    };
}

}  // namespace apogee::testing
