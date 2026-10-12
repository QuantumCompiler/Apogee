#pragma once

#include <cstddef>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "tui/pump.h"

/// The progress seam (37e): the one widget a long run's narration is drawn
/// through in the shell -- a task run today, training and dataset runs next
/// (37f) -- so no view grows a progress path of its own.
///
/// **Produced off the shell's thread** (the monitor bar's sampler idiom,
/// 32e): `start` runs the work on a thread of its own, and every line it
/// hands `say` is posted to the pump, in order, never waited for -- a stalled
/// producer holds back lines, never a frame. The lines are kept in a bounded
/// scrollback, the newest last, read on the shell's thread by the view that
/// draws them (`ListOptions::progress`).
///
/// **It owns no protocol.** What a line says is the producer's -- a task
/// run's events worded by its view -- and stopping a run goes through the
/// run's own channel (`task cancel`'s request, a turn's token), handed to
/// `start` and called at most once however often `cancel` is asked.
namespace apogee::tui {

class Progress {
public:
    /// What the work hands each line to: any thread, never blocking.
    using Say = std::function<void(std::string)>;

    /// `keep`: the scrollback's bound, the oldest lines dropped past it.
    explicit Progress(Pump& pump, std::size_t keep = kKeep);
    /// A run still going is stopped through its channel, and its thread
    /// joined: the shell quitting never leaves a run behind it.
    ~Progress();

    Progress(const Progress&) = delete;
    Progress& operator=(const Progress&) = delete;
    Progress(Progress&&) = delete;
    Progress& operator=(Progress&&) = delete;

    /// Starts `work` on a thread of its own under `heading` -- the lines of
    /// the last run cleared -- with `cancel` the way to stop it. False, and
    /// nothing started, while a run is going. `ended` is told on the shell's
    /// thread once the work has returned and its every line is in. Any
    /// thread: a view's worker starts one as readily as its keys do.
    bool start(std::string heading, std::function<void(const Say& say)> work,
               std::function<void()> cancel = {}, std::function<void()> ended = {});

    /// Stops the run through the channel `start` was given, once: a second
    /// ask, or one with no run, does nothing. Returns whether it asked. Any
    /// thread.
    bool cancel();

    /// A run was started and its end is not yet in. Any thread.
    [[nodiscard]] bool running() const;

    // --- the shell's thread ----------------------------------------------------

    /// Something was started, running or ended: there is a run to draw.
    [[nodiscard]] bool started() const;
    [[nodiscard]] const std::string& heading() const;
    /// The narration, oldest first, at most `keep` lines.
    [[nodiscard]] const std::vector<std::string>& lines() const;

    /// Waits for the run's thread, for a test (its lines may still be posted
    /// and not yet drained). Any thread but the run's.
    void join();

    inline static constexpr std::size_t kKeep = 500;

    struct State;

private:
    std::shared_ptr<State> state_;
};

}  // namespace apogee::tui
