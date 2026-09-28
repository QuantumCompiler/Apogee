#pragma once

#include <filesystem>
#include <string>

#include "platform/platform.h"

/// The environment note (25d): where a model with tools is and what day it
/// is.
///
/// A model cannot know the date, and one given tools also has to know the
/// folder its paths start from and the shell its commands run in. A model
/// asked the date said it could not know (2026-09-24). The note is built
/// where the tools are registered and rendered each turn, then handed to
/// the loop as transient context: it is in every request and never in a
/// saved transcript.
namespace apogee::tools {

struct Environment {
    platform::OperatingSystem operating_system = platform::host_os();
    platform::Architecture architecture = platform::host_architecture();
    std::filesystem::path working_directory;
    /// The shell `run_command` runs through; empty when that toolset is off.
    std::string shell;
    /// Where the file tools reach; empty when that toolset is off.
    std::filesystem::path fs_root;
};

/// The note for `environment` on `date`.
///
/// **The date, never the time of day.** The note heads every request, so a
/// line that changed each turn would make a local model re-read the whole
/// conversation every turn (25c's checkpoints and the plain KV cache both
/// match from the first token). The date changes once a day; a model that
/// needs the time can run `date`.
[[nodiscard]] std::string render_environment_note(const Environment& environment,
                                                  const platform::LocalDate& date);

}  // namespace apogee::tools
