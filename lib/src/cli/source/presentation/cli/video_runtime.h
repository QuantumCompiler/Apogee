#pragma once

#include <filesystem>
#include <istream>
#include <ostream>
#include <string>
#include <string_view>

#include "contracts/config.h"
#include "training/python_env.h"
#include "video/runtime.h"

/// The video runtime brought into the environment Apogee owns, on demand
/// and with consent (49b): what a command that generates video asks before
/// it runs one.
///
/// **Asked, never assumed.** torch and diffusers are several gigabytes, so
/// the `video` set is installed only after a yes at a terminal -- the same
/// question the environment itself is created after (`require_python_env`)
/// -- and on a pipe the command refuses, naming `apogee train setup --with
/// video`, which installs it when asked. Nothing is installed into the
/// system Python, ever.
namespace apogee::commands {

/// The question and the install, every stream and the environment given --
/// what a test drives: `set`, whose packages are `needs` ("torch and
/// diffusers"), about `size`. `interactive` false refuses at once; a yes
/// installs the set through the environment's own pip, saying what it
/// installs; any other answer refuses. The refusal or the failure, or empty
/// once installed.
[[nodiscard]] std::string install_runtime_set(training::PythonEnv& env,
                                              training::RequirementSet set, std::string_view needs,
                                              std::string_view size, bool interactive,
                                              std::istream& in, std::ostream& out,
                                              std::string_view purpose);

/// `install_runtime_set` for the `video` set: "torch and diffusers".
[[nodiscard]] std::string install_video_set(training::PythonEnv& env, bool interactive,
                                            std::istream& in, std::ostream& out,
                                            std::string_view purpose);

/// The runtime as a command needs it: the environment (created after a yes,
/// `require_python_env`), the `video` set (installed after a yes when torch
/// or diffusers is missing), then the ladder probed again. On a pipe, or on
/// a no, throws the CLI's user error naming the command that installs it.
/// The readiness the ladder ends at -- a later rung (the driver, ffmpeg) is
/// the caller's to say.
[[nodiscard]] video::VideoReadiness ready_video_runtime(const harness::Config& config,
                                                        std::string_view purpose);

/// The same for speech (50a): the environment, then the `voice` set when
/// torch or kokoro is missing, then the voice ladder.
[[nodiscard]] video::VideoReadiness ready_voice_runtime(const harness::Config& config,
                                                        std::string_view purpose);

/// The speech runtime for the voice model at `model` (`handle` naming it)
/// on `engine`, as a command asks it (53m): on PyTorch `ready_voice_runtime`
/// -- the environment and the `voice` set asked for at a terminal -- then the
/// whole ladder over the model; on MLX the ladder alone (`probe_voice_mlx`):
/// mlx-audio is opt-in, never asked about, its rungs naming its setup.
[[nodiscard]] video::VideoReadiness ready_voice_engine(const harness::Config& config,
                                                       const std::filesystem::path& model,
                                                       std::string_view handle,
                                                       video::VideoEngine engine,
                                                       std::string_view purpose);

}  // namespace apogee::commands
