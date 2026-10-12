#include "tui/monitor_bar.h"

#include <condition_variable>
#include <deque>
#include <exception>
#include <ftxui/component/component.hpp>
#include <ftxui/dom/elements.hpp>
#include <mutex>
#include <optional>
#include <thread>
#include <utility>

#include "operations/system_view.h"
#include "tui/view_body.h"

namespace apogee::tui {

std::vector<std::string> monitor_parts(const platform::MachineSample& sample,
                                       const platform::GpuInfo& gpu,
                                       const std::vector<std::string>& held) {
    std::vector<std::string> parts;
    if (sample.utilization.has_value()) {
        parts.push_back("cpu " + operations::format_percent(*sample.utilization));
    }
    if (sample.load.has_value()) {
        parts.push_back("load " + operations::format_load(*sample.load));
    }
    if (const std::optional<std::int64_t> used = sample.memory.used();
        used.has_value() && sample.memory.total.has_value()) {
        parts.push_back("mem " + operations::format_bytes(*used) + " / " +
                        operations::format_bytes(*sample.memory.total));
    }
    if (sample.process_footprint.has_value()) {
        std::string apogee = "apogee " + operations::format_bytes(*sample.process_footprint);
        if (!held.empty()) {
            apogee += " · " + std::to_string(held.size()) + " held";
        }
        parts.push_back(std::move(apogee));
    }
    // A GPU only where the platform tells its story: Apple silicon's (32a).
    if (gpu.unified_memory && gpu.name.has_value()) {
        parts.push_back("gpu " + *gpu.name + ", unified");
    }
    return parts;
}

struct MonitorBar::State : std::enable_shared_from_this<MonitorBar::State> {
    State(Pump& pump_ref, Theme theme_value, const platform::SystemSource& source_ref,
          MonitorOptions options_value)
        : pump{pump_ref},
          theme{theme_value},
          source{source_ref},
          options{std::move(options_value)} {}

    Pump& pump;
    Theme theme;
    const platform::SystemSource& source;
    MonitorOptions options;

    // --- the sampler ----------------------------------------------------------
    std::mutex mutex;
    std::condition_variable changed;
    std::optional<std::function<void()>> job;
    bool sampling = false;
    bool stopping = false;
    std::thread sampler;

    // --- the shell's thread only ------------------------------------------------
    bool in_flight = false;
    bool have_sample = false;
    bool failed = false;
    platform::MachineSample last;
    platform::GpuInfo gpu;
    std::vector<std::string> held;
    std::chrono::steady_clock::time_point taken{};
    ftxui::Component component;

    void start_sampler() {
        sampler = std::thread{[this]() {
            for (;;) {
                std::function<void()> next;
                {
                    std::unique_lock lock{mutex};
                    changed.wait(lock, [this]() { return stopping || job.has_value(); });
                    if (stopping) {
                        return;
                    }
                    next = std::move(*job);
                    job.reset();
                    sampling = true;
                }
                next();
                {
                    const std::lock_guard lock{mutex};
                    sampling = false;
                }
                changed.notify_all();
            }
        }};
    }

    void stop_sampler() {
        {
            const std::lock_guard lock{mutex};
            stopping = true;
        }
        changed.notify_all();
        if (sampler.joinable()) {
            sampler.join();
        }
    }

    /// The tick, on the shell's thread: a sample, unless one is still out.
    void tick() {
        if (in_flight) {
            return;  // a stalled probe holds back the bar, and no more pile up
        }
        in_flight = true;
        const std::optional<platform::CpuTimes> previous =
            have_sample ? last.times : std::optional<platform::CpuTimes>{};
        const bool first = !have_sample && !gpu.name.has_value() && gpu.unknown.empty();
        {
            const std::lock_guard lock{mutex};
            job = [this, previous, first]() {
                platform::MachineSample sample;
                platform::GpuInfo device;
                bool ok = true;
                try {
                    sample = platform::sample_machine(source, previous);
                    if (first) {
                        device = source.gpu();
                    }
                    ok = !sample.empty();
                } catch (const std::exception&) {
                    ok = false;
                }
                std::vector<std::string> holding =
                    options.held ? options.held() : std::vector<std::string>{};
                pump.post([self = shared_from_this(), sample = std::move(sample),
                           device = std::move(device), holding = std::move(holding), ok,
                           first]() { self->arrive(sample, device, holding, ok, first); });
            };
        }
        changed.notify_all();
    }

    void arrive(const platform::MachineSample& sample, const platform::GpuInfo& device,
                std::vector<std::string> holding, bool ok, bool first) {
        in_flight = false;
        if (first) {
            gpu = device;
        }
        held = std::move(holding);
        if (!ok) {
            failed = true;  // the last values stand, with their age
            return;
        }
        // A first reading has no utilization: the next has the one between.
        last = sample;
        have_sample = true;
        failed = false;
        taken = pump.now();
    }

    [[nodiscard]] ftxui::Element draw() const {
        using namespace ftxui;  // NOLINT(google-build-using-namespace): the DOM's vocabulary
        if (!have_sample) {
            return text(failed ? " the machine could not be read" : " reading the machine…") | dim;
        }
        std::string line;
        for (const std::string& part : monitor_parts(last, gpu, held)) {
            line += (line.empty() ? " " : "  ·  ") + part;
        }
        Elements row{text(line) | dim};
        const auto age = std::chrono::duration_cast<std::chrono::seconds>(pump.now() - taken);
        if (failed || age > options.cadence * 5 / 2) {
            Element stale = text("  ·  stale " + std::to_string(age.count()) + "s");
            row.push_back(theme.color ? stale | color(Color::Yellow) : stale | bold);
        }
        return hbox(std::move(row));
    }
};

MonitorBar::MonitorBar(Pump& pump, Theme theme, const platform::SystemSource& source,
                       MonitorOptions options)
    : state_{std::make_shared<State>(pump, theme, source, std::move(options))} {
    State* state = state_.get();
    state_->component = ftxui::Renderer([state]() { return state->draw(); });
    state_->start_sampler();
}

MonitorBar::~MonitorBar() {
    state_->stop_sampler();
}

View MonitorBar::view() {
    return View{"Monitor", std::make_shared<View::Body>(View::Body{state_->component})};
}

void MonitorBar::start() {
    state_->tick();
    const std::weak_ptr<State> state = state_;
    state_->pump.every(state_->options.cadence, [state]() {
        if (const std::shared_ptr<State> held = state.lock(); held != nullptr) {
            held->tick();
        }
    });
}

void MonitorBar::settle() {
    std::unique_lock lock{state_->mutex};
    state_->changed.wait(lock, [this]() {
        return state_->stopping || (!state_->job.has_value() && !state_->sampling);
    });
}

std::chrono::milliseconds MonitorBar::stale_after() const {
    return state_->options.cadence * 5 / 2;
}

}  // namespace apogee::tui
