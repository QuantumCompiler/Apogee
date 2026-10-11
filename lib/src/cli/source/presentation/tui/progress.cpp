#include "tui/progress.h"

#include <atomic>
#include <mutex>
#include <thread>
#include <utility>

namespace apogee::tui {

struct Progress::State : std::enable_shared_from_this<Progress::State> {
    State(Pump& pump_ref, std::size_t keep_value) : pump{pump_ref}, keep{keep_value} {}

    Pump& pump;
    std::size_t keep;

    // --- the shell's thread ---------------------------------------------------
    std::string heading;
    std::vector<std::string> lines;
    bool started = false;

    // --- any thread -----------------------------------------------------------
    /// A run was started and its end not yet drawn.
    std::atomic<bool> busy{false};
    std::mutex mutex;
    std::function<void()> cancel;  // taken by the first cancel
    std::thread worker;

    void append(std::string line) {
        lines.push_back(std::move(line));
        if (lines.size() > keep) {
            lines.erase(lines.begin(),
                        lines.begin() + static_cast<std::ptrdiff_t>(lines.size() - keep));
        }
    }
};

Progress::Progress(Pump& pump, std::size_t keep)
    : state_{std::make_shared<State>(pump, keep == 0 ? 1 : keep)} {}

Progress::~Progress() {
    (void)cancel();
    join();
}

bool Progress::start(std::string heading, std::function<void(const Say& say)> work,
                     std::function<void()> cancel, std::function<void()> ended) {
    if (state_->busy.exchange(true)) {
        return false;  // a run is going
    }
    join();  // the last run's thread, done
    {
        const std::lock_guard lock{state_->mutex};
        state_->cancel = std::move(cancel);
    }
    const std::weak_ptr<State> weak = state_;
    Pump& pump = state_->pump;
    // The last run's lines cleared before any of this one's: the pump runs
    // posts in order.
    pump.post([weak, heading = std::move(heading)]() mutable {
        if (const std::shared_ptr<State> held = weak.lock(); held != nullptr) {
            held->heading = std::move(heading);
            held->lines.clear();
            held->started = true;
        }
    });
    state_->worker = std::thread{[weak, &pump, work = std::move(work), ended = std::move(ended)]() {
        // Each line posted, never waited for: the producer may stall, the
        // frame never does. A line posted after the widget is gone is dropped.
        const Say say = [weak, &pump](std::string line) {
            pump.post([weak, line = std::move(line)]() mutable {
                if (const std::shared_ptr<State> held = weak.lock(); held != nullptr) {
                    held->append(std::move(line));
                }
            });
        };
        try {
            work(say);
        } catch (const std::exception& e) {
            say(std::string{"stopped: "} + e.what());
        }
        if (const std::shared_ptr<State> held = weak.lock(); held != nullptr) {
            const std::lock_guard lock{held->mutex};
            held->cancel = nullptr;  // nothing left to stop
        }
        // After every line: told once they are all in.
        pump.post([weak, ended]() {
            if (const std::shared_ptr<State> held = weak.lock(); held != nullptr) {
                held->busy = false;
                if (ended) {
                    ended();
                }
            }
        });
    }};
    return true;
}

bool Progress::cancel() {
    std::function<void()> cancel;
    {
        const std::lock_guard lock{state_->mutex};
        cancel = std::exchange(state_->cancel, nullptr);
    }
    if (!cancel) {
        return false;
    }
    cancel();
    return true;
}

bool Progress::running() const {
    return state_->busy;
}

bool Progress::started() const {
    return state_->started;
}

const std::string& Progress::heading() const {
    return state_->heading;
}

const std::vector<std::string>& Progress::lines() const {
    return state_->lines;
}

void Progress::join() {
    if (state_->worker.joinable() && state_->worker.get_id() != std::this_thread::get_id()) {
        state_->worker.join();
    }
}

}  // namespace apogee::tui
