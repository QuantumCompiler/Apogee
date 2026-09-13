#include "httpserver/sse_writer.h"

#include <nlohmann/json.hpp>

#include <utility>

namespace apogee::httpserver {

std::string sse_frame(const nlohmann::json& payload) {
    return "data: " + payload.dump() + "\n\n";
}

nlohmann::json status_event_json(const harness::StatusEvent& event) {
    nlohmann::json out{{"type", std::string{harness::to_string(event.type)}},
                       {"phase", std::string{harness::to_string(event.phase)}}};
    if (!event.name.empty()) {
        out["name"] = event.name;
    }
    if (!event.detail.empty()) {
        out["detail"] = event.detail;
    }
    if (event.rag.has_value()) {
        // Flattened beside the other fields, the way Ommi's frames read, and
        // always with the retriever: a score shown without its scale invites a
        // comparison that cannot be made.
        out["collection"] = event.rag->db;
        out["chunks_found"] = event.rag->chunks_found;
        out["top_score"] = event.rag->top_score;
        out["retriever"] = event.rag->retriever;
        out["reranked"] = event.rag->reranked;
        if (event.rag->graph_entities > 0) {
            out["graph_entities"] = event.rag->graph_entities;
        }
    }
    if (event.tokens.has_value()) {
        out["tokens"] = *event.tokens;
    }
    if (event.tokens_per_second.has_value()) {
        out["tokens_per_second"] = *event.tokens_per_second;
    }
    if (event.used_tokens.has_value()) {
        out["used_tokens"] = *event.used_tokens;
    }
    if (event.context_size.has_value()) {
        out["context_size"] = *event.context_size;
    }
    if (event.usage_percent.has_value()) {
        out["usage_percent"] = *event.usage_percent;
    }
    return out;
}

nlohmann::json chat_chunk(const StreamIdentity& identity, nlohmann::json delta,
                          const std::optional<std::string>& finish_reason) {
    nlohmann::json choice{{"index", 0}, {"delta", std::move(delta)}};
    if (finish_reason.has_value()) {
        choice["finish_reason"] = *finish_reason;
    } else {
        choice["finish_reason"] = nullptr;
    }
    return nlohmann::json{{"id", identity.id},
                          {"object", "chat.completion.chunk"},
                          {"created", identity.created},
                          {"model", identity.model},
                          {"choices", nlohmann::json::array({std::move(choice)})}};
}

nlohmann::json meta_chunk(const StreamIdentity& identity, const harness::StatusEvent& event) {
    nlohmann::json chunk = chat_chunk(identity, nlohmann::json{{"content", ""}}, std::nullopt);
    chunk["meta"] = status_event_json(event);
    return chunk;
}

nlohmann::json completion_chunk(const StreamIdentity& identity, std::string_view text,
                                const std::optional<std::string>& finish_reason) {
    nlohmann::json choice{{"index", 0}, {"text", std::string{text}}};
    if (finish_reason.has_value()) {
        choice["finish_reason"] = *finish_reason;
    } else {
        choice["finish_reason"] = nullptr;
    }
    return nlohmann::json{{"id", identity.id},
                          {"object", "text_completion"},
                          {"created", identity.created},
                          {"model", identity.model},
                          {"choices", nlohmann::json::array({std::move(choice)})}};
}

SseWriter::SseWriter(WriteFn write, harness::CancellationToken cancellation)
    : write_{std::move(write)}, cancellation_{std::move(cancellation)} {}

bool SseWriter::send(const nlohmann::json& payload) {
    return send_raw(sse_frame(payload));
}

bool SseWriter::send_raw(std::string_view bytes) {
    if (closed_) {
        return false;
    }
    if (!write_ || !write_(bytes)) {
        // The client hung up. Nothing further can reach it, and the turn that
        // is feeding this writer should stop spending on an answer nobody
        // will read -- the token is how it finds out.
        closed_ = true;
        cancellation_.cancel();
        return false;
    }
    ++frames_;
    return true;
}

void SseWriter::done() {
    (void)send_raw(kSseDone);
}

}  // namespace apogee::httpserver
