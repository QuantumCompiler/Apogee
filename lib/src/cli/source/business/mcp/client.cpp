#include "mcp/client.h"

#include <utility>

namespace apogee::mcp {

Client::Client(std::string name, std::unique_ptr<Transport> transport, ClientOptions options)
    : name_{std::move(name)}, transport_{std::move(transport)}, options_{std::move(options)} {}

Client::~Client() {
    close();
}

void Client::log(const std::string& line) const {
    if (options_.log) {
        options_.log(line);
    }
}

std::unique_ptr<Client> Client::connect(std::string name, std::unique_ptr<Transport> transport,
                                        const ClientOptions& options, std::string& error) {
    // A private constructor and a unique_ptr: the read thread needs a stable
    // address before the first frame goes out.
    std::unique_ptr<Client> client{new Client{std::move(name), std::move(transport), options}};
    client->reader_ = std::thread{[raw = client.get()] { raw->read_loop(); }};

    const auto deadline = std::chrono::steady_clock::now() + options.connect_timeout;
    const CallOutcome init =
        client->call("initialize", initialize_params(options.client_version), deadline, nullptr);
    if (!init.response.has_value()) {
        error = "initialize: " + init.error + stderr_note(client->stderr_tail());
        client->close();
        return nullptr;
    }
    if (init.response->error.has_value()) {
        error = "initialize: " + init.response->error->message;
        client->close();
        return nullptr;
    }
    client->protocol_version_ = init.response->result.value("protocolVersion", std::string{});
    if (const auto info = init.response->result.find("serverInfo");
        info != init.response->result.end() && info->is_object()) {
        client->server_name_ = info->value("name", std::string{});
    }
    // Required by the spec; no reply expected.
    if (!client->transport_->send(make_notification("notifications/initialized"))) {
        error = "initialized: connection closed" + stderr_note(client->stderr_tail());
        client->close();
        return nullptr;
    }
    const CallOutcome listed =
        client->call("tools/list", nlohmann::json::object(), deadline, nullptr);
    if (!listed.response.has_value()) {
        error = "tools/list: " + listed.error + stderr_note(client->stderr_tail());
        client->close();
        return nullptr;
    }
    if (listed.response->error.has_value()) {
        error = "tools/list: " + listed.response->error->message;
        client->close();
        return nullptr;
    }
    client->tools_ = parse_tools_list(listed.response->result);
    return client;
}

Client::CallOutcome Client::call(std::string_view method, nlohmann::json params,
                                 std::chrono::steady_clock::time_point deadline,
                                 const harness::CancellationToken* cancellation) {
    CallOutcome outcome;
    if (done_.load()) {
        outcome.error = "connection closed";
        return outcome;
    }
    const std::int64_t id = next_id_.fetch_add(1);
    {
        const std::lock_guard<std::mutex> lock{mutex_};
        pending_.emplace(id, std::nullopt);
    }
    if (!transport_->send(make_request(id, method, std::move(params)))) {
        {
            const std::lock_guard<std::mutex> lock{mutex_};
            pending_.erase(id);
        }
        mark_done();
        outcome.error = "connection closed";
        return outcome;
    }

    std::unique_lock<std::mutex> lock{mutex_};
    for (;;) {
        const auto it = pending_.find(id);
        if (it != pending_.end() && it->second.has_value()) {
            outcome.response = std::move(it->second);
            pending_.erase(it);
            return outcome;
        }
        if (done_.load()) {
            pending_.erase(id);
            outcome.error = "connection closed";
            return outcome;
        }
        if (cancellation != nullptr && cancellation->stop_requested()) {
            pending_.erase(id);
            outcome.error = "cancelled";
            return outcome;
        }
        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            pending_.erase(id);
            outcome.error = "timed out";
            return outcome;
        }
        cv_.wait_for(lock, std::min(deadline - now, std::chrono::steady_clock::duration{
                                                        std::chrono::milliseconds{50}}));
    }
}

void Client::read_loop() {
    for (;;) {
        if (closing_.load()) {
            break;
        }
        const std::optional<std::string> line = transport_->recv(std::chrono::milliseconds{100});
        if (!line.has_value()) {
            // The far end is gone. Nothing pending can ever be answered, so
            // release every waiter NOW rather than letting each wait out its
            // deadline with a frozen status line above it.
            if (!closing_.load()) {
                log("[mcp] " + name_ + ": connection closed by the server");
            }
            mark_done();
            return;
        }
        if (line->empty()) {
            continue;
        }
        const std::optional<IncomingMessage> message = parse_incoming(*line);
        if (!message.has_value()) {
            log("[mcp] " + name_ + ": bad frame: " + line->substr(0, 80));
            continue;
        }
        if (message->is_notification()) {
            // Logged and dropped: tools/list_changed would be the one worth
            // acting on, and live refresh is a later item.
            log("[mcp] " + name_ + ": notification " + message->method);
            continue;
        }
        const std::lock_guard<std::mutex> lock{mutex_};
        const auto it = pending_.find(*message->id);
        if (it == pending_.end()) {
            log("[mcp] " + name_ + ": unexpected response id " + std::to_string(*message->id));
            continue;
        }
        it->second = *message;
        cv_.notify_all();
    }
}

void Client::mark_done() {
    // Exactly once, from either the read loop's error path or close():
    // a server dying while the caller is closing it must not race.
    bool expected = false;
    if (!done_.compare_exchange_strong(expected, true)) {
        return;
    }
    const std::lock_guard<std::mutex> lock{mutex_};
    cv_.notify_all();
}

ToolCallResult Client::call_tool(std::string_view tool, std::string_view arguments_json,
                                 const harness::CancellationToken& cancellation) {
    nlohmann::json arguments = nlohmann::json::parse(arguments_json, nullptr, false);
    if (arguments.is_discarded() || !arguments.is_object()) {
        arguments = nlohmann::json::object();
    }
    const CallOutcome outcome =
        call("tools/call", nlohmann::json{{"name", std::string{tool}}, {"arguments", arguments}},
             std::chrono::steady_clock::now() + options_.call_timeout, &cancellation);
    ToolCallResult result;
    if (!outcome.response.has_value()) {
        result.is_error = true;
        result.text = "Error: mcp " + name_ + ": " + outcome.error + stderr_note(stderr_tail());
        return result;
    }
    if (outcome.response->error.has_value()) {
        result.is_error = true;
        result.text = "Error: mcp " + name_ + ": " + outcome.response->error->message;
        return result;
    }
    result = parse_tool_call(outcome.response->result);
    if (result.is_error && !result.text.starts_with("Error")) {
        result.text = "Error: " + result.text;
    }
    return result;
}

std::string Client::stderr_tail() const {
    return transport_ != nullptr ? transport_->stderr_tail() : std::string{};
}

void Client::close() {
    if (closing_.exchange(true)) {
        if (reader_.joinable() && std::this_thread::get_id() != reader_.get_id()) {
            reader_.join();
        }
        return;
    }
    mark_done();
    if (transport_ != nullptr) {
        transport_->close();
    }
    if (reader_.joinable()) {
        reader_.join();
    }
}

}  // namespace apogee::mcp
