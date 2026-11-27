#include "acp/jsonrpc.h"

#include <utility>
#include <vector>

namespace apogee::acp {

namespace {

/// An id JSON-RPC allows: a string, a number, or null.
[[nodiscard]] bool valid_id(const nlohmann::json& id) {
    return id.is_string() || id.is_number() || id.is_null();
}

}  // namespace

Frame parse_frame(std::string_view line) {
    Frame frame;
    const nlohmann::json root = nlohmann::json::parse(line, nullptr, false);
    if (root.is_discarded()) {
        frame.kind = Frame::Kind::Unparsable;
        frame.problem = "not JSON";
        return frame;
    }
    frame.kind = Frame::Kind::Invalid;
    if (!root.is_object()) {
        // A batch is JSON-RPC's, never ACP's; a bare value is nothing.
        frame.problem = root.is_array() ? "a batch: ACP sends one message a line"
                                        : "not a JSON-RPC message: a line is one object";
        return frame;
    }
    const auto id = root.find("id");
    if (id != root.end()) {
        if (!valid_id(*id)) {
            frame.problem = "an id is a string or a number";
            return frame;
        }
        frame.id = *id;
    }
    if (const auto method = root.find("method"); method != root.end()) {
        if (!method->is_string() || method->get_ref<const std::string&>().empty()) {
            frame.problem = "a method is a name";
            return frame;
        }
        frame.method = method->get<std::string>();
        if (const auto params = root.find("params"); params != root.end()) {
            frame.params = *params;
        }
        frame.kind = id == root.end() ? Frame::Kind::Notification : Frame::Kind::Request;
        return frame;
    }
    const auto result = root.find("result");
    const auto error = root.find("error");
    if (id != root.end() && (result != root.end() || error != root.end())) {
        frame.kind = Frame::Kind::Response;
        if (result != root.end()) {
            frame.result = *result;
        }
        if (error != root.end()) {
            frame.error = *error;
        }
        return frame;
    }
    frame.problem = "neither a request nor a response: no method, no result, no error";
    return frame;
}

std::string frame_text(const nlohmann::json& frame) {
    return frame.dump(-1, ' ', false, nlohmann::json::error_handler_t::replace);
}

nlohmann::json response(const nlohmann::json& id, nlohmann::json result) {
    return nlohmann::json{{"jsonrpc", "2.0"}, {"id", id}, {"result", std::move(result)}};
}

nlohmann::json error_response(const nlohmann::json& id, int code, std::string_view message) {
    return nlohmann::json{{"jsonrpc", "2.0"},
                          {"id", id},
                          {"error", {{"code", code}, {"message", std::string{message}}}}};
}

nlohmann::json notification(std::string_view method, nlohmann::json params) {
    return nlohmann::json{
        {"jsonrpc", "2.0"}, {"method", std::string{method}}, {"params", std::move(params)}};
}

nlohmann::json request(std::int64_t id, std::string_view method, nlohmann::json params) {
    return nlohmann::json{{"jsonrpc", "2.0"},
                          {"id", id},
                          {"method", std::string{method}},
                          {"params", std::move(params)}};
}

Peer::Peer(std::ostream& out) : out_{&out} {}

void Peer::send(const nlohmann::json& frame) {
    const std::string text = frame_text(frame);
    const std::lock_guard lock{write_mutex_};
    // One object per line, flushed at once: a host renders as it reads.
    (*out_) << text << "\n";
    out_->flush();
}

void Peer::ask(std::string_view method, nlohmann::json params, Answer answered) {
    std::unique_lock lock{pending_mutex_};
    if (closed_) {
        // Nobody to answer: as a host that hung up mid-question. Answered
        // outside the lock, as `deliver` answers: the answer may ask again.
        lock.unlock();
        if (answered) {
            answered(nullptr, nullptr);
        }
        return;
    }
    // Only now is `answered` handed over: the closed path above has it whole.
    const std::int64_t id = ++next_id_;
    pending_.emplace(id, std::move(answered));
    lock.unlock();
    send(request(id, method, std::move(params)));
}

bool Peer::deliver(const Frame& response) {
    if (!response.id.is_number_integer()) {
        return false;
    }
    Answer answered;
    {
        const std::lock_guard lock{pending_mutex_};
        const auto found = pending_.find(response.id.get<std::int64_t>());
        if (found == pending_.end()) {
            return false;
        }
        answered = std::move(found->second);
        pending_.erase(found);
    }
    // Outside the lock: the answer may ask again.
    if (answered) {
        answered(response.error.is_null() ? &response.result : nullptr,
                 response.error.is_null() ? nullptr : &response.error);
    }
    return true;
}

void Peer::close() {
    std::vector<Answer> waiting;
    {
        const std::lock_guard lock{pending_mutex_};
        closed_ = true;
        for (auto& [id, answered] : pending_) {
            waiting.push_back(std::move(answered));
        }
        pending_.clear();
    }
    for (const Answer& answered : waiting) {
        if (answered) {
            answered(nullptr, nullptr);
        }
    }
}

}  // namespace apogee::acp
