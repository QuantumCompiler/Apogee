#include "machine/driver_input.h"

#include <condition_variable>
#include <deque>
#include <mutex>
#include <thread>
#include <utility>

#include "machine/json_reporter.h"

namespace apogee::commands {

struct DriverInput::State {
    std::mutex mutex;
    std::condition_variable changed;
    std::deque<DriverLine> lines;
    /// The driver closed its end and the reader has stopped.
    bool closed = false;
    /// The open turn: the last `user` line read whose turn has not ended.
    std::optional<DriverLine> open;
    std::uint64_t next_sequence = 1;
    std::thread reader;
};

DriverInput::DriverInput(std::istream& in) : state_{std::make_shared<State>()} {
    // The reader owns a share of the state, so a reader left blocked on an
    // open stdin outlives this object safely.
    state_->reader = std::thread([state = state_, &in]() {
        std::string text;
        while (std::getline(in, text)) {
            const DriverMessage message = parse_driver_line(text);
            const std::lock_guard lock{state->mutex};
            if (message.kind == DriverMessage::Kind::Cancel) {
                if (state->open.has_value()) {
                    state->open->turn.cancel();
                }
                state->changed.notify_all();
                continue;
            }
            DriverLine line{std::move(text), {}, 0};
            if (message.kind == DriverMessage::Kind::User) {
                line.turn = harness::CancellationToken::create();
                line.sequence = state->next_sequence++;
                state->open = line;
            }
            state->lines.push_back(std::move(line));
            state->changed.notify_all();
        }
        const std::lock_guard lock{state->mutex};
        state->closed = true;
        state->changed.notify_all();
    });
}

DriverInput::~DriverInput() {
    bool closed = false;
    {
        const std::lock_guard lock{state_->mutex};
        closed = state_->closed;
    }
    if (closed) {
        state_->reader.join();
    } else {
        state_->reader.detach();
    }
}

std::optional<DriverLine> DriverInput::next_line() {
    std::unique_lock lock{state_->mutex};
    state_->changed.wait(lock, [this] { return !state_->lines.empty() || state_->closed; });
    if (state_->lines.empty()) {
        return std::nullopt;
    }
    DriverLine line = std::move(state_->lines.front());
    state_->lines.pop_front();
    return line;
}

std::optional<DriverLine> DriverInput::next_line_in_turn(bool& cancelled) {
    cancelled = false;
    std::unique_lock lock{state_->mutex};
    const auto stopped = [this] {
        return state_->open.has_value() && state_->open->turn.stop_requested();
    };
    state_->changed.wait(lock,
                         [&] { return !state_->lines.empty() || state_->closed || stopped(); });
    if (!state_->lines.empty()) {
        DriverLine line = std::move(state_->lines.front());
        state_->lines.pop_front();
        return line;
    }
    cancelled = stopped();
    return std::nullopt;
}

void DriverInput::end_turn(std::uint64_t sequence) {
    const std::lock_guard lock{state_->mutex};
    if (state_->open.has_value() && state_->open->sequence == sequence) {
        state_->open.reset();
    }
}

}  // namespace apogee::commands
