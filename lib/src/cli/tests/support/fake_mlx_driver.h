#pragma once

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

#include "platform/child_process.h"

/// A scripted MLX driver (27a), shared by the backend's suite and the suites
/// that put an `mlx` entry behind another surface -- the served plane's
/// conformance case, the attachments (27c).
///
/// It answers requests the way the driver does -- `ready` at spawn, a
/// scripted reply per `generate` with its id, `done cancelled` for a cancel
/// -- so every path a real model will not take on demand is an ordinary
/// test: a stream cut between any two bytes, a driver that dies mid-turn
/// with a traceback on stderr, one that goes silent, a cancel it never
/// answers, a load that fails.
namespace apogee::testing {

/// A driver's `ready` once it has loaded its model through mlx-vlm (27c).
inline constexpr std::string_view kVisionReady =
    R"({"type":"ready","protocol":1,"model_type":"qwen3_vl","chat_template":true,)"
    R"("tool_parser":"json_tools","thinking":false,"mlx_lm":"0.32.0","vision":true,)"
    R"("mlx_vlm":"0.3.9"})"
    "\n";

/// What a fake driver does, and what was done to it.
struct DriverState {
    /// Written at spawn: `ready`, or a fatal error.
    std::string startup = R"({"type":"ready","protocol":1,"model_type":"llama",)"
                          R"("chat_template":true,"tool_parser":null,"thinking":false,)"
                          R"("mlx_lm":"0.32.0"})"
                          "\n";
    /// Written instead of `startup` when the spawn asked for `--vision`
    /// (27c); empty means `startup` either way -- a driver from before 27c.
    std::string vision_startup;
    /// Set by the spawner: this spawn asked for `--vision`.
    bool vision_spawn = false;
    /// One per `generate`, `{id}` replaced by its id; the last repeats.
    std::vector<std::string> replies;
    std::string stderr_text;
    std::size_t chunk = 0;
    /// End of file once everything written so far has been read.
    bool die_after_startup = false;
    bool die_after_reply = false;
    /// Never answer a cancel.
    bool deaf = false;

    std::string out;
    std::size_t offset = 0;
    bool stderr_read = false;
    std::vector<std::string> writes;
    std::size_t replied = 0;
    bool dying = false;
    bool stdin_closed = false;
    bool terminated = false;
};

class FakeDriver final : public platform::ChildProcess {
public:
    explicit FakeDriver(std::shared_ptr<DriverState> state) : state_{std::move(state)} {
        state_->out += state_->vision_spawn && !state_->vision_startup.empty()
                           ? state_->vision_startup
                           : state_->startup;
        state_->dying = state_->die_after_startup;
    }

    [[nodiscard]] bool write_stdin(std::string_view bytes) override {
        if (state_->stdin_closed || state_->terminated) {
            return false;
        }
        const std::string text{bytes};
        std::size_t start = 0;
        for (std::size_t end = text.find('\n'); end != std::string::npos;
             start = end + 1, end = text.find('\n', start)) {
            const std::string line = text.substr(start, end - start);
            state_->writes.push_back(line);
            const nlohmann::json request = nlohmann::json::parse(line, nullptr, false);
            const std::string id =
                request.is_object() ? request.value("id", nlohmann::json{}).dump() : "0";
            if (request.value("type", "") == "generate" && !state_->replies.empty()) {
                std::string reply =
                    state_->replies[std::min(state_->replied, state_->replies.size() - 1)];
                ++state_->replied;
                for (std::size_t at = reply.find("{id}"); at != std::string::npos;
                     at = reply.find("{id}", at)) {
                    reply.replace(at, 4, id);
                }
                state_->out += reply;
                state_->dying = state_->dying || state_->die_after_reply;
            } else if (request.value("type", "") == "cancel" && !state_->deaf) {
                state_->out += R"({"type":"done","id":)" + id +
                               R"(,"finish":"cancelled","prompt_tokens":5,"cached_tokens":0,)"
                               R"("completion_tokens":1})"
                               "\n";
            }
        }
        return true;
    }

    void close_stdin() override {
        state_->stdin_closed = true;
    }

    [[nodiscard]] platform::ReadStatus read_stdout(std::string& out,
                                                   std::chrono::milliseconds timeout) override {
        out.clear();
        if (state_->offset < state_->out.size()) {
            const std::size_t left = state_->out.size() - state_->offset;
            const std::size_t take = state_->chunk == 0 ? left : std::min(state_->chunk, left);
            out = state_->out.substr(state_->offset, take);
            state_->offset += take;
            return platform::ReadStatus::Data;
        }
        if (state_->dying || state_->stdin_closed || state_->terminated) {
            return platform::ReadStatus::Eof;
        }
        std::this_thread::sleep_for(std::min(timeout, std::chrono::milliseconds{2}));
        return platform::ReadStatus::Timeout;
    }

    [[nodiscard]] platform::ReadStatus read_stderr(std::string& out,
                                                   std::chrono::milliseconds /*timeout*/) override {
        out.clear();
        if (state_->stderr_read || state_->stderr_text.empty()) {
            return platform::ReadStatus::Eof;
        }
        state_->stderr_read = true;
        out = state_->stderr_text;
        return platform::ReadStatus::Data;
    }

    [[nodiscard]] bool exited() override {
        return state_->terminated || state_->stdin_closed ||
               (state_->dying && state_->offset >= state_->out.size());
    }

    [[nodiscard]] std::optional<int> wait_for_exit(std::chrono::milliseconds /*timeout*/) override {
        return 0;
    }

    void terminate() override {
        state_->terminated = true;
    }

private:
    std::shared_ptr<DriverState> state_;
};

}  // namespace apogee::testing
