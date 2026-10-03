#include "commands/status_line.h"

#include <algorithm>
#include <array>
#include <iostream>
#include <sstream>
#include <utility>

#include "ansi/text_width.h"
#include "platform/platform.h"

namespace apogee::commands {
namespace {

constexpr std::array<std::string_view, 4> kSpinnerFrames{"✻", "✽", "✼", "✺"};

constexpr std::string_view kEllipsis = "…";

/// A width nothing could measure: wide enough to say something, narrow
/// enough to fit any terminal still in use.
constexpr std::size_t kAssumedWidth = 80;

/// `text` in at most `cells` display cells, keeping its START behind a
/// trailing "…" -- a phase reads from its first word. Codepoint-safe.
std::string fit_head(std::string_view text, std::size_t cells) {
    if (ansi::display_width(text) <= cells) {
        return std::string{text};
    }
    if (cells == 0) {
        return {};
    }
    const std::size_t room = cells - 1;  // the ellipsis takes one
    std::size_t used = 0;
    std::size_t cut = 0;
    while (cut < text.size()) {
        const std::size_t length = std::min(
            ansi::utf8_sequence_length(static_cast<unsigned char>(text[cut])), text.size() - cut);
        const std::size_t here = ansi::codepoint_cells(ansi::decode_utf8(text, cut, length));
        if (used + here > room) {
            break;
        }
        used += here;
        cut += length;
    }
    return std::string{text.substr(0, cut)} + std::string{kEllipsis};
}

/// Stderr's width, where the busy line paints.
std::size_t stderr_width() {
    const std::optional<int> width = platform::terminal_width(platform::StandardStream::Err);
    return width.has_value() && *width > 0 ? static_cast<std::size_t>(*width) : kAssumedWidth;
}

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

std::string spinner_frame(std::string_view label, std::size_t tick, std::int64_t elapsed_seconds,
                          std::optional<BusyCount> count, std::size_t width) {
    const std::string glyph = std::string{kSpinnerFrames[tick % kSpinnerFrames.size()]} + " ";
    const bool timed = elapsed_seconds >= kBusyElapsedFrom;
    std::string suffix;
    if (count.has_value() || timed) {
        suffix = " (";
        if (count.has_value()) {
            suffix += std::to_string(count->done) + "/" + std::to_string(count->total);
        }
        if (timed) {
            suffix += (count.has_value() ? " · " : "") + std::to_string(elapsed_seconds) + "s";
        }
        suffix += ")";
    }
    if (width == 0) {
        return glyph + std::string{label} + suffix;
    }
    // The last column is never written: a cursor there wraps on some
    // terminals and not others, and the erase that follows misses the row.
    const std::size_t room = width - 1;
    const std::size_t fixed = ansi::display_width(glyph) + ansi::display_width(suffix);
    if (fixed < room) {
        return glyph + fit_head(label, room - fixed) + suffix;
    }
    // Too narrow even for the count: cut the frame itself.
    return fit_head(glyph + suffix.substr(std::min<std::size_t>(1, suffix.size())), room);
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

void StatusLine::print_above(const std::function<void(std::ostream&)>& write) {
    writer_.with_lock([this, &write](std::ostream& out) {
        if (options_.active && line_shown_.load()) {
            out << ansi::kEraseLine;
            line_shown_.store(false);
        }
        write(out);
    });
}

bool StatusLine::wait_running(std::chrono::milliseconds duration) {
    std::unique_lock lock{wake_mutex_};
    return !wake_.wait_for(lock, duration, [this] { return !spinner_running_.load(); });
}

std::string StatusLine::frame(std::size_t tick, std::int64_t elapsed) {
    std::string label;
    BusyCount count;
    bool busy = false;
    std::function<std::size_t()> width;
    {
        const std::scoped_lock lock{label_mutex_};
        label = spinner_label_;
        count = count_;
        busy = busy_;
        width = width_;
    }
    if (!busy) {
        return spinner_frame(label, tick, elapsed, token_estimate_.load());
    }
    // Measured at every repaint: a terminal narrowed mid-sweep must not get a
    // frame that wraps, which the next erase would not reach.
    return spinner_frame(label, tick, elapsed,
                         count.total > 0 ? std::optional<BusyCount>{count} : std::nullopt,
                         width ? width() : stderr_width());
}

void StatusLine::start_spinner(std::string label) {
    if (!options_.active || options_.verbosity != ansi::Verbosity::Line) {
        // A pipe gets no frames; a verbose run gets permanent lines instead.
        return;
    }
    stop_spinner();

    {
        const std::scoped_lock lock{label_mutex_};
        spinner_label_ = std::move(label);
        count_ = {};
        busy_ = false;
        width_ = nullptr;
    }
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
            paint(frame(tick, elapsed));
            ++tick;
            if (!wait_running(options_.spinner_interval)) {
                return;
            }
        }
    });
}

void StatusLine::start_busy(std::string label, std::chrono::milliseconds delay,
                            std::function<std::size_t()> width) {
    if (!options_.active || options_.verbosity != ansi::Verbosity::Line) {
        // A pipe, a log or a quiet run: no frame, and no thread either.
        return;
    }
    stop_spinner();

    {
        const std::scoped_lock lock{label_mutex_};
        spinner_label_ = std::move(label);
        count_ = {};
        busy_ = true;
        width_ = std::move(width);
    }
    spinner_running_.store(true);

    spinner_ = std::make_unique<std::thread>([this, delay]() {
        // Elapsed is wall clock from when the work began, delay included: a
        // line that first shows at 150 ms must not claim it started then.
        const auto started = std::chrono::steady_clock::now();
        if (!wait_running(delay)) {
            return;  // done before it was worth showing
        }
        std::size_t tick = 0;
        while (spinner_running_.load()) {
            const auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                                     std::chrono::steady_clock::now() - started)
                                     .count();
            paint(frame(tick, elapsed));
            ++tick;
            if (!wait_running(options_.spinner_interval)) {
                return;
            }
        }
    });
}

void StatusLine::set_spinner_label(std::string label, std::size_t done, std::size_t total) {
    const std::scoped_lock lock{label_mutex_};
    spinner_label_ = std::move(label);
    count_ = BusyCount{.done = done, .total = total};
}

void StatusLine::set_token_estimate(std::int64_t tokens) {
    token_estimate_.store(tokens);
}

void StatusLine::stop_spinner() {
    {
        // Under the wake lock, so a spinner about to wait sees it stopped
        // rather than sleeping out its interval first.
        const std::scoped_lock lock{wake_mutex_};
        if (!spinner_running_.exchange(false)) {
            return;
        }
    }
    wake_.notify_all();
    if (spinner_ && spinner_->joinable()) {
        spinner_->join();
    }
    spinner_.reset();
    clear();
}

BusyLine::BusyLine(std::ostream& out, std::string label, Options options)
    : writer_{out},
      status_{writer_, StatusLine::Options{.active = options.active,
                                           .verbosity = options.active ? ansi::Verbosity::Line
                                                                       : ansi::Verbosity::Quiet,
                                           .style = {},
                                           .spinner_interval = options.interval}},
      active_{options.active} {
    if (active_) {
        status_.start_busy(std::move(label), options.delay, std::move(options.width));
    }
}

BusyLine::~BusyLine() {
    finish();
}

void BusyLine::set(std::string label) {
    report(std::move(label), 0, 0);
}

void BusyLine::report(std::string label, std::size_t done, std::size_t total) {
    if (active_) {
        status_.set_spinner_label(std::move(label), done, total);
    }
}

BusyProgress BusyLine::sink() {
    return [this](std::string_view label, std::size_t done, std::size_t total) {
        report(std::string{label}, done, total);
    };
}

void BusyLine::above(const std::function<void()>& write) {
    status_.print_above([&write](std::ostream&) {
        write();
        // Before the lock is let go: a frame painted after this point lands
        // below what was written, never in the middle of it.
        std::cout.flush();
    });
}

void BusyLine::finish() {
    status_.stop_spinner();
}

BusyLine::Options busy_options(bool quiet) {
    BusyLine::Options options;
    options.active = !quiet && platform::is_terminal(platform::StandardStream::Err);
    return options;
}

}  // namespace apogee::commands
