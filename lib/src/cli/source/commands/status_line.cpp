#include "commands/status_line.h"

#include <array>
#include <sstream>
#include <utility>

namespace apogee::commands {
namespace {

constexpr std::array<std::string_view, 4> kSpinnerFrames{"✻", "✽", "✼", "✺"};

}  // namespace

std::string spinner_frame(std::string_view label, std::size_t tick, std::int64_t elapsed_seconds,
                          std::int64_t estimated_tokens) {
    std::ostringstream out;
    out << kSpinnerFrames[tick % kSpinnerFrames.size()] << " " << label;

    // The token estimate makes a wait indicator genuinely informative rather
    // than merely animated -- and on a redacted-thinking model, where no
    // reasoning text ever arrives, the counter is the ONLY live signal there is.
    if (elapsed_seconds > 0 || estimated_tokens > 0) {
        out << " (";
        if (elapsed_seconds > 0) {
            out << elapsed_seconds << "s";
        }
        if (estimated_tokens > 0) {
            if (elapsed_seconds > 0) {
                out << " · ";
            }
            out << "~" << estimated_tokens << " tokens";
        }
        out << ")";
    }
    return out.str();
}

StatusLine::StatusLine(TerminalWriter& writer, Options options)
    : writer_{writer}, options_{std::move(options)} {}

StatusLine::~StatusLine() {
    stop_spinner();
}

void StatusLine::paint(std::string_view text) {
    writer_.with_lock([this, text](std::ostream& out) {
        out << ansi::kEraseLine << text;
        line_shown_.store(!text.empty());
    });
}

void StatusLine::set(std::string_view text) {
    if (options_.verbosity == ansi::Verbosity::Quiet) {
        return;
    }
    if (options_.verbosity == ansi::Verbosity::Verbose || !options_.active) {
        // No transient line to overwrite: every status becomes a permanent one,
        // which is what a log wants and what a pipe must not receive at all.
        if (options_.active || options_.verbosity == ansi::Verbosity::Verbose) {
            print_line(text);
        }
        return;
    }
    paint(text);
}

void StatusLine::clear() {
    if (!options_.active || !line_shown_.load()) {
        return;
    }
    writer_.with_lock([this](std::ostream& out) {
        out << ansi::kEraseLine;
        line_shown_.store(false);
    });
}

void StatusLine::print_line(std::string_view text) {
    // Bump FIRST: an in-flight spinner tick that has already decided to paint
    // must see a stale generation and drop its frame, or it lands after this
    // text and leaves a spinner frame in the scrollback forever.
    generation_.fetch_add(1);

    writer_.with_lock([this, text](std::ostream& out) {
        if (options_.active) {
            out << ansi::kEraseLine;
        }
        out << text << "\n";
        line_shown_.store(false);
    });
}

void StatusLine::start_spinner(std::string label) {
    if (!options_.active || options_.verbosity != ansi::Verbosity::Line) {
        // A pipe gets no frames; a verbose run gets permanent lines instead.
        return;
    }
    stop_spinner();

    spinner_label_ = std::move(label);
    token_estimate_.store(0);
    spinner_running_.store(true);

    const std::uint64_t generation = generation_.load();
    spinner_ = std::make_unique<std::thread>([this, generation]() {
        const auto started = std::chrono::steady_clock::now();
        std::size_t tick = 0;
        while (spinner_running_.load()) {
            // The generation check: if anything printed permanently since this
            // spinner started, stop rather than paint over it.
            if (generation_.load() != generation) {
                return;
            }
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count();
            paint(spinner_frame(spinner_label_, tick, elapsed, token_estimate_.load()));
            ++tick;
            std::this_thread::sleep_for(options_.spinner_interval);
        }
    });
}

void StatusLine::set_token_estimate(std::int64_t tokens) {
    token_estimate_.store(tokens);
}

void StatusLine::stop_spinner() {
    if (!spinner_running_.exchange(false)) {
        return;
    }
    if (spinner_ && spinner_->joinable()) {
        spinner_->join();
    }
    spinner_.reset();
    clear();
}

}  // namespace apogee::commands
