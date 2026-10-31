#include "cli/video_runtime.h"

#include <CLI/CLI.hpp>

#include <functional>
#include <iostream>

#include "cli/helpers.h"
#include "cli/train.h"
#include "platform/platform.h"

namespace apogee::commands {

std::string install_runtime_set(training::PythonEnv& env, training::RequirementSet set,
                                std::string_view needs, std::string_view size, bool interactive,
                                std::istream& in, std::ostream& out, std::string_view purpose) {
    // A set this environment's Python cannot run is refused before it is
    // offered (M19): the answer would only be pip's refusal.
    if (std::string unfit = env.unfit_reason(set); !unfit.empty()) {
        return unfit;
    }
    const std::string name{training::to_string(set)};
    const std::string remedy = "apogee train setup --with " + name;
    std::string packages;
    for (const std::string_view package : training::packages_for(set)) {
        packages += (packages.empty() ? "" : " ") + std::string{package};
    }
    if (!interactive) {
        return std::string{needs} + " " +
               (needs.find(" and ") != std::string_view::npos ? "are" : "is") +
               " not installed in Apogee's Python environment -- " + remedy;
    }
    out << purpose << " needs " << needs << " in Apogee's Python environment at\n  "
        << env.dir().string() << "\n(the '" << name << "' set: " << packages << " -- " << size
        << ", downloaded by that environment's pip and installed there alone). Install it now? "
           "[y/N] "
        << std::flush;
    std::string answer;
    std::getline(in, answer);
    if (answer != "y" && answer != "Y" && answer != "yes") {
        return "not installed. Run '" + remedy + "' when ready";
    }
    out << "installing the '" << name << "' set: " << packages << " ...\n" << std::flush;
    if (std::string failure = env.install(set); !failure.empty()) {
        return failure;
    }
    out << "installed\n";
    return {};
}

std::string install_video_set(training::PythonEnv& env, bool interactive, std::istream& in,
                              std::ostream& out, std::string_view purpose) {
    return install_runtime_set(env, training::RequirementSet::Video, "torch and diffusers",
                               "several gigabytes", interactive, in, out, purpose);
}

namespace {

/// The environment, then `set` after a yes at a terminal when `missing` says
/// a package rung refused, then the ladder again.
[[nodiscard]] video::VideoReadiness ready_runtime(
    const harness::Config& config, std::string_view purpose, training::RequirementSet set,
    std::string_view needs, std::string_view size,
    const std::function<video::VideoReadiness(const video::VideoHost&)>& probe,
    const std::function<bool(video::VideoRefusal)>& missing) {
    training::PythonEnv env = require_python_env(config, purpose);
    const video::VideoHost host = video::VideoHost::current();
    video::VideoReadiness readiness = probe(host);
    if (!missing(readiness.refusal)) {
        return readiness;
    }
    const bool interactive =
        platform::is_terminal(platform::StandardStream::In) && !stdin_is_piped();
    if (const std::string refused =
            install_runtime_set(env, set, needs, size, interactive, std::cin, std::cerr, purpose);
        !refused.empty()) {
        std::cerr << "apogee " << purpose << ": " << refused << "\n";
        throw CLI::RuntimeError(kUserError);
    }
    return probe(host);
}

}  // namespace

video::VideoReadiness ready_video_runtime(const harness::Config& config, std::string_view purpose) {
    return ready_runtime(
        config, purpose, training::RequirementSet::Video, "torch and diffusers",
        "several gigabytes", [](const video::VideoHost& host) { return video::probe_video(host); },
        [](video::VideoRefusal refusal) {
            return refusal == video::VideoRefusal::NoTorch ||
                   refusal == video::VideoRefusal::NoDiffusers;
        });
}

video::VideoReadiness ready_voice_runtime(const harness::Config& config, std::string_view purpose) {
    return ready_runtime(
        config, purpose, training::RequirementSet::Voice, "torch and kokoro",
        "a few gigabytes with torch, far less without",
        [](const video::VideoHost& host) { return video::probe_voice(host); },
        [](video::VideoRefusal refusal) {
            return refusal == video::VideoRefusal::NoTorch ||
                   refusal == video::VideoRefusal::NoKokoro;
        });
}

}  // namespace apogee::commands
