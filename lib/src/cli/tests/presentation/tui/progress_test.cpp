#include "tui/progress.h"

#include <catch2/catch_test_macros.hpp>

#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

#include "tui/list_view.h"
#include "tui/pump.h"
#include "tui/shell.h"

/// The progress seam (37e) on the manual pump: lines in order across drains,
/// a stalled producer never holding back a frame, cancel reaching its channel
/// exactly once, the scrollback bounded, and a list view drawing the run.
namespace {

/// A gate the producer waits at until the test opens it.
struct Latch {
    std::mutex mutex;
    std::condition_variable changed;
    bool open = false;

    void release() {
        {
            const std::lock_guard lock{mutex};
            open = true;
        }
        changed.notify_all();
    }

    void wait() {
        std::unique_lock lock{mutex};
        changed.wait(lock, [this]() { return open; });
    }
};

/// Drains until `done`, or fails after a few seconds.
template <typename Done>
void until(apogee::tui::ManualPump& pump, Done done) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds{10};
    while (!done()) {
        REQUIRE(std::chrono::steady_clock::now() < deadline);
        (void)pump.drain();
        std::this_thread::sleep_for(std::chrono::milliseconds{2});
    }
}

}  // namespace

TEST_CASE("a run's lines arrive in order, and its end is told once every line is in",
          "[tui][progress]") {
    apogee::tui::ManualPump pump;
    apogee::tui::Progress progress{pump};
    CHECK_FALSE(progress.started());
    int ended = 0;
    REQUIRE(progress.start(
        "counting",
        [](const apogee::tui::Progress::Say& say) {
            for (const char* line : {"one", "two", "three"}) {
                say(line);
            }
        },
        {}, [&ended]() { ++ended; }));
    CHECK(progress.running());
    progress.join();
    CHECK(progress.running());  // until its end is drawn
    (void)pump.drain();
    CHECK(progress.heading() == "counting");
    CHECK(progress.lines() == std::vector<std::string>{"one", "two", "three"});
    CHECK_FALSE(progress.running());
    CHECK(ended == 1);
    // A second run starts afresh.
    REQUIRE(progress.start("again", [](const apogee::tui::Progress::Say& say) { say("four"); }));
    progress.join();
    (void)pump.drain();
    CHECK(progress.lines() == std::vector<std::string>{"four"});
}

TEST_CASE("a stalled producer holds back lines, never a frame", "[tui][progress]") {
    apogee::tui::ManualPump pump;
    apogee::tui::Progress progress{pump};
    Latch latch;
    REQUIRE(progress.start("slow", [&latch](const apogee::tui::Progress::Say& say) {
        say("before the stall");
        latch.wait();
        say("after it");
    }));
    until(pump, [&progress]() { return !progress.lines().empty(); });
    // The producer is stalled: draining returns at once, and the frame is
    // drawn with what has arrived.
    const auto drained_at = std::chrono::steady_clock::now();
    for (int i = 0; i < 5; ++i) {
        (void)pump.drain();
    }
    CHECK(std::chrono::steady_clock::now() - drained_at < std::chrono::milliseconds{500});
    CHECK(progress.running());
    CHECK(progress.lines() == std::vector<std::string>{"before the stall"});
    // A second run is refused while this one goes.
    CHECK_FALSE(progress.start("other", [](const apogee::tui::Progress::Say&) {}));
    latch.release();
    until(pump, [&progress]() { return !progress.running(); });
    CHECK(progress.lines() == std::vector<std::string>{"before the stall", "after it"});
}

TEST_CASE("cancel reaches the run's channel exactly once", "[tui][progress]") {
    apogee::tui::ManualPump pump;
    apogee::tui::Progress progress{pump};
    CHECK_FALSE(progress.cancel());  // nothing to stop
    Latch latch;
    std::atomic<int> cancelled{0};
    REQUIRE(progress.start(
        "stoppable",
        [&latch](const apogee::tui::Progress::Say& say) {
            latch.wait();
            say("stopped through its own channel");
        },
        [&cancelled, &latch]() {
            ++cancelled;
            latch.release();
        }));
    CHECK(progress.cancel());
    CHECK_FALSE(progress.cancel());
    progress.join();
    (void)pump.drain();
    CHECK(cancelled == 1);
    CHECK(progress.lines() == std::vector<std::string>{"stopped through its own channel"});
    CHECK_FALSE(progress.cancel());  // ended: nothing to stop
}

TEST_CASE("the scrollback keeps the newest lines, and a throw is said", "[tui][progress]") {
    apogee::tui::ManualPump pump;
    apogee::tui::Progress progress{pump, 3};
    REQUIRE(progress.start("many", [](const apogee::tui::Progress::Say& say) {
        for (int i = 1; i <= 5; ++i) {
            say("line " + std::to_string(i));
        }
        throw std::runtime_error{"the producer failed"};
    }));
    progress.join();
    (void)pump.drain();
    CHECK(progress.lines() ==
          std::vector<std::string>{"line 4", "line 5", "stopped: the producer failed"});
}

TEST_CASE("a list view draws its run under the table, and Ctrl-C cancels it",
          "[tui][progress][list]") {
    apogee::tui::ManualPump pump;
    auto progress = std::make_shared<apogee::tui::Progress>(pump);
    apogee::tui::ListOptions options;
    options.title = "Runs";
    options.columns = {"NAME"};
    options.load = []() {
        return std::pair{std::vector<std::string>{},
                         std::vector<apogee::tui::ListRow>{{.key = "a", .cells = {"a"}}}};
    };
    options.progress = progress;
    apogee::tui::Shell shell{{.title = "apogee test", .theme = {.color = false}}};
    apogee::tui::ListView view{pump, apogee::tui::Theme{.color = false}, std::move(options)};
    shell.add(view.view());
    shell.activate(0);
    const auto frame = [&]() {
        view.settle();
        (void)pump.drain();
        return shell.render_text(100, 30);
    };
    CHECK(frame().find("running") == std::string::npos);  // nothing started: no region
    Latch latch;
    std::atomic<int> cancelled{0};
    REQUIRE(progress->start(
        "a run",
        [&latch](const apogee::tui::Progress::Say& say) {
            say("first step");
            latch.wait();
        },
        [&cancelled, &latch]() {
            ++cancelled;
            latch.release();
        }));
    until(pump, [&progress]() { return !progress->lines().empty(); });
    const std::string running = frame();
    CHECK(running.find(" a run -- running · Ctrl-C stops it") != std::string::npos);
    CHECK(running.find(" first step") != std::string::npos);
    (void)shell.press(apogee::tui::Key::named(apogee::tui::Key::Name::CtrlC));
    CHECK(cancelled == 1);
    CHECK_FALSE(shell.quit_requested());  // the run stopped, the shell stays
    progress->join();
    CHECK(frame().find(" a run -- ended") != std::string::npos);
}
