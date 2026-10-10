#pragma once

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "platform/system_info.h"
#include "tui/pump.h"
#include "tui/theme.h"
#include "tui/view.h"

/// The system monitor (32e): the machine on the shell's bottom bar, across
/// every view -- the CPU's use and load, memory used of the total, Apogee's own
/// footprint and the models it holds, the GPU where the platform tells its
/// story -- refreshed on the pump's slow tick while the shell runs.
///
/// **One probe, two lifetimes.** The bar reads 32a's `SystemSource` -- its
/// partial entry, `sample_machine` -- and words it with `operations/
/// system_view`'s units, so it and `apogee system` never print one machine two
/// ways; `apogee system` stays one-shot, and the bar's tick lives and dies with
/// the shell's loop. **A sample is taken off the shell's thread**, one at a
/// time: a probe that stalls holds back the bar, never a frame -- the bar keeps
/// its last values and says how old they are -- and when it recovers the next
/// tick takes the next sample, never a burst to catch up. An unknown reading
/// is left out, never guessed.
namespace apogee::tui {

struct MonitorOptions {
    /// How often the bar samples (the recorded default: two seconds).
    std::chrono::milliseconds cadence{2000};
    /// The models this process holds, by backend -- the session's word
    /// (27e's residency); null when nothing holds any.
    std::function<std::vector<std::string>()> held;
};

class MonitorBar {
public:
    MonitorBar(Pump& pump, Theme theme, const platform::SystemSource& source,
               MonitorOptions options = {});
    ~MonitorBar();

    MonitorBar(const MonitorBar&) = delete;
    MonitorBar& operator=(const MonitorBar&) = delete;
    MonitorBar(MonitorBar&&) = delete;
    MonitorBar& operator=(MonitorBar&&) = delete;

    /// The bar, for `Shell::set_bottom_bar`.
    [[nodiscard]] View view();

    /// Takes a first sample and asks the pump for the tick.
    void start();

    /// Waits for a sample in flight to finish: for a test, before it looks.
    void settle();

    /// How old the last good sample would be past which the bar calls itself
    /// stale: two and a half cadences, so one slow sample is not a failure.
    [[nodiscard]] std::chrono::milliseconds stale_after() const;

    struct State;

private:
    std::shared_ptr<State> state_;
};

/// The strip's words for one sample, as the bar draws them -- for the tests
/// that hold its units to `apogee system`'s: `cpu 14%`, `load 2.31 1.98 1.75`,
/// `mem 61.2 GiB / 128.0 GiB`, `apogee 42 MiB · 2 held`, `gpu Apple M3 Max,
/// unified`; a reading not known is not there.
[[nodiscard]] std::vector<std::string> monitor_parts(const platform::MachineSample& sample,
                                                     const platform::GpuInfo& gpu,
                                                     const std::vector<std::string>& held);

}  // namespace apogee::tui
