#include "support/terminal_output.h"

#include <stdexcept>

#if !defined(_WIN32)
#include <fcntl.h>
#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>

#include "support/env_guard.h"
#endif

namespace apogee::testing {

std::string terminal_output(const std::function<void()>& action) {
#if defined(_WIN32)
    (void)action;
    throw std::runtime_error{"terminal_output reads descriptors 1 and 2 on a POSIX host only"};
#else
    const TempDir dir{"terminal-output-" + std::to_string(std::random_device{}())};
    const std::filesystem::path capture = dir.path() / "descriptors";
    const int file = ::open(capture.c_str(), O_CREAT | O_TRUNC | O_WRONLY | O_CLOEXEC, 0600);
    if (file < 0) {
        throw std::runtime_error{"could not open " + capture.string()};
    }
    std::fflush(stdout);
    std::fflush(stderr);
    const int saved_out = ::dup(STDOUT_FILENO);
    const int saved_err = ::dup(STDERR_FILENO);
    if (saved_out < 0 || saved_err < 0 || ::dup2(file, STDOUT_FILENO) < 0 ||
        ::dup2(file, STDERR_FILENO) < 0) {
        ::dup2(saved_out, STDOUT_FILENO);
        ::dup2(saved_err, STDERR_FILENO);
        ::close(saved_out);
        ::close(saved_err);
        ::close(file);
        throw std::runtime_error{"could not point descriptors 1 and 2 at " + capture.string()};
    }
    const auto restore = [&] {
        std::fflush(stdout);
        std::fflush(stderr);
        ::dup2(saved_out, STDOUT_FILENO);
        ::dup2(saved_err, STDERR_FILENO);
        ::close(saved_out);
        ::close(saved_err);
        ::close(file);
    };
    try {
        action();
    } catch (...) {
        restore();
        throw;
    }
    restore();
    std::ifstream in{capture, std::ios::binary};
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
#endif
}

}  // namespace apogee::testing
