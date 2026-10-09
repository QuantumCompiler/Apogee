#include <catch2/reporters/catch_reporter_event_listener.hpp>
#include <catch2/reporters/catch_reporter_registrars.hpp>

#include <iostream>
#include <sstream>

/// The suite never reads the terminal it was started from.
///
/// Every command test runs the real command tree in this process, and a run
/// whose stdin is the developer's terminal can stop at a prompt there -- a
/// chat at `You:`, a wizard at its first question -- waiting on the real
/// keyboard. ctest and CI never see it (their stdin is a pipe), so it is
/// structural here rather than left to each test: for the whole run, std::cin
/// reads an empty stream, as on a pipe that has ended. A test that needs input
/// feeds std::cin its own buffer and puts this one back; swapping the buffer
/// clears the stream's state, so an end one test reached never leaks into the
/// next. And since `platform::is_terminal` answers a standard stream given
/// another buffer as a pipe (2026-10-07), nothing in the suite takes stdin for
/// a terminal.
namespace {

class HermeticStdin final : public Catch::EventListenerBase {
public:
    using Catch::EventListenerBase::EventListenerBase;

    void testRunStarting(const Catch::TestRunInfo& /*info*/) override {
        previous_ = std::cin.rdbuf(empty_.rdbuf());
    }

    void testRunEnded(const Catch::TestRunStats& /*stats*/) override {
        std::cin.rdbuf(previous_);
    }

private:
    std::istringstream empty_;
    std::streambuf* previous_ = nullptr;
};

}  // namespace

CATCH_REGISTER_LISTENER(HermeticStdin)
