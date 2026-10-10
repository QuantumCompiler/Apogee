#include "tui/pump.h"

#include <ftxui/component/app.hpp>
#include <ftxui/component/event.hpp>
#include <iostream>
#include <streambuf>
#include <string>
#include <utility>

#include "tui/shell_state.h"

namespace apogee::tui {

namespace {

/// std::cerr while the shell holds the screen: each line handed to `say`
/// instead of written over the frame. Thread-safe -- a turn's thread and a
/// library's alike may write -- and restored, with any unfinished line
/// written where it was going, when it ends.
class StderrToNotices final : public std::streambuf {
public:
    explicit StderrToNotices(std::function<void(std::string)> say)
        : say_{std::move(say)}, previous_{std::cerr.rdbuf(this)} {}

    ~StderrToNotices() override {
        std::cerr.rdbuf(previous_);
        if (!line_.empty()) {
            std::cerr << line_ << "\n";
        }
    }

    StderrToNotices(const StderrToNotices&) = delete;
    StderrToNotices& operator=(const StderrToNotices&) = delete;
    StderrToNotices(StderrToNotices&&) = delete;
    StderrToNotices& operator=(StderrToNotices&&) = delete;

protected:
    int_type overflow(int_type character) override {
        if (traits_type::eq_int_type(character, traits_type::eof())) {
            return traits_type::not_eof(character);
        }
        const char byte = traits_type::to_char_type(character);
        write(&byte, 1);
        return character;
    }

    std::streamsize xsputn(const char* bytes, std::streamsize count) override {
        write(bytes, count);
        return count;
    }

private:
    void write(const char* bytes, std::streamsize count) {
        const std::lock_guard lock{mutex_};
        for (std::streamsize i = 0; i < count; ++i) {
            // NOLINTNEXTLINE(cppcoreguidelines-pro-bounds-pointer-arithmetic)
            const char byte = bytes[i];
            if (byte == '\n') {
                if (!line_.empty()) {
                    say_(std::exchange(line_, {}));
                }
            } else if (byte != '\r') {
                line_ += byte;
            }
        }
    }

    std::function<void(std::string)> say_;
    std::streambuf* previous_;
    std::mutex mutex_;
    std::string line_;
};

}  // namespace

struct TerminalPump::Impl {
    ftxui::App app = ftxui::App::FullscreenAlternateScreen();
};

TerminalPump::TerminalPump() : impl_{std::make_unique<Impl>()} {}

TerminalPump::~TerminalPump() = default;

void TerminalPump::post(std::function<void()> task) {
    impl_->app.Post(std::move(task));
    // A posted closure changes what is drawn: a custom event redraws.
    impl_->app.PostEvent(ftxui::Event::Custom);
}

void TerminalPump::exit() {
    impl_->app.Exit();
}

int TerminalPump::run(Shell& shell) {
    ftxui::App& app = impl_->app;
    // Ctrl-C goes to the view first: a session stops its turn with it, and
    // only a Ctrl-C nothing handles quits (the shell's own key).
    app.ForceHandleCtrlC(false);
    app.TrackMouse(false);
    shell.on_quit([this]() { exit(); });
    const StderrToNotices capture{[this, &shell](std::string line) {
        post([&shell, said = std::move(line)]() mutable { shell.notice(std::move(said)); });
    }};
    app.Loop(shell.state().root);
    shell.on_quit({});
    return 0;
}

void ManualPump::post(std::function<void()> task) {
    const std::lock_guard lock{mutex_};
    tasks_.push_back(std::move(task));
}

void ManualPump::exit() {
    const std::lock_guard lock{mutex_};
    exited_ = true;
}

std::size_t ManualPump::drain() {
    std::size_t ran = 0;
    for (;;) {
        std::function<void()> task;
        {
            const std::lock_guard lock{mutex_};
            if (tasks_.empty()) {
                return ran;
            }
            task = std::move(tasks_.front());
            tasks_.pop_front();
        }
        task();
        ++ran;
    }
}

bool ManualPump::exited() const {
    const std::lock_guard lock{mutex_};
    return exited_;
}

}  // namespace apogee::tui
