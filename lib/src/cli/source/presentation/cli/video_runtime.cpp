#include "cli/video_runtime.h"

#include <CLI/CLI.hpp>

#include <iostream>

#include "cli/helpers.h"
#include "cli/train.h"
#include "platform/platform.h"

namespace apogee::commands {

std::string install_video_set(training::PythonEnv& env, bool interactive, std::istream& in,
                              std::ostream& out, std::string_view purpose) {
    std::string packages;
    for (const std::string_view package : training::packages_for(training::RequirementSet::Video)) {
        packages += (packages.empty() ? "" : " ") + std::string{package};
    }
    if (!interactive) {
        return "torch and diffusers are not installed in Apogee's Python environment -- " +
               std::string{video::kVideoSetupRemedy};
    }
    out << purpose << " needs torch and diffusers in Apogee's Python environment at\n  "
        << env.dir().string() << "\n(the 'video' set: " << packages
        << " -- several gigabytes, downloaded by that environment's pip and installed there "
           "alone). Install it now? [y/N] "
        << std::flush;
    std::string answer;
    std::getline(in, answer);
    if (answer != "y" && answer != "Y" && answer != "yes") {
        return "not installed. Run '" + std::string{video::kVideoSetupRemedy} + "' when ready";
    }
    out << "installing the 'video' set: " << packages << " ...\n" << std::flush;
    if (std::string failure = env.install(training::RequirementSet::Video); !failure.empty()) {
        return failure;
    }
    out << "installed\n";
    return {};
}

video::VideoReadiness ready_video_runtime(const harness::Config& config, std::string_view purpose) {
    training::PythonEnv env = require_python_env(config, purpose);
    const video::VideoHost host = video::VideoHost::current();
    video::VideoReadiness readiness = video::probe_video(host);
    if (readiness.refusal != video::VideoRefusal::NoTorch &&
        readiness.refusal != video::VideoRefusal::NoDiffusers) {
        return readiness;
    }
    const bool interactive =
        platform::is_terminal(platform::StandardStream::In) && !stdin_is_piped();
    if (const std::string refused =
            install_video_set(env, interactive, std::cin, std::cerr, purpose);
        !refused.empty()) {
        std::cerr << "apogee " << purpose << ": " << refused << "\n";
        throw CLI::RuntimeError(kUserError);
    }
    return video::probe_video(host);
}

}  // namespace apogee::commands
