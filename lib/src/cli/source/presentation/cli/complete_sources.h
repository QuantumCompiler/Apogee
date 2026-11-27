#pragma once

#include <string>
#include <string_view>
#include <vector>

#include "cli/complete_protocol.h"

/// Where each name kind a completion can offer comes from (`kNameValues` in
/// `command.h`): the config the protocol already loaded, the data directory,
/// the model store, the local Ollama store, and the working directory's git
/// repository.
///
/// **Read-only, and quiet.** Completion runs on every <TAB>; a source never
/// creates a file (the knowledge store would create its collection when
/// opened, so a missing one is not opened), never prints, and lets any
/// failure through as an exception the protocol turns into "nothing to
/// offer".
namespace apogee::commands {

/// One kind's names, from the real data directory. Throws
/// `std::invalid_argument` for a kind it has no source for.
[[nodiscard]] NameList list_names(std::string_view kind, const CompletionContext& context);

/// `list_names`, as the protocol takes it.
[[nodiscard]] CompletionSources default_completion_sources();

/// The tools that write which `--allow`, `--deny` and a task's grant take on
/// this build under this config (`OFFERED_TOOL`): those of
/// `tools::destructive_tool_names()` a session's registry holds, registered
/// as a session registers them -- so no shell or git tool where no program
/// can be started (57k, `child_processes` false) and none of a toolset
/// `tools.disabled` switches off (`disabled`).
[[nodiscard]] std::vector<std::string> offered_tool_names(bool child_processes,
                                                          const std::vector<std::string>& disabled);

}  // namespace apogee::commands
