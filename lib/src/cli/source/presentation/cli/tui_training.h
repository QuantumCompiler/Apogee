#pragma once

#include <filesystem>
#include <memory>

#include "cli/command.h"
#include "tui/list_view.h"
#include "tui/progress.h"

/// The shell's training views (37f), the progress seam's next consumers:
/// Train and Datasets. Their reads are the commands' documents, drawn in
/// process; every run and every act is the command itself, run as a captured
/// child of the shell's own binary (`cli/tui_child`) and narrated through the
/// widget in its own words -- so a run's manifest, a promoted version and a
/// rolled-back config are the command's by construction.
///
/// - Train: one row per promoted version, as `train versions --output-format
///   json` states them, under `train status`'s lines. Enter evaluates the
///   version's run (`train eval`); `r`, `p` and `P` open the input row on
///   `run `, `pipeline run --pipeline ` and `promote ` -- the command's own
///   words, its arguments typed -- and run `apogee train <line>` after an
///   ask; `c` runs the cycle once, asked; `R` rolls the row's backend back,
///   asked.
/// - Datasets: the datasets as `datasets list --output-format json` states
///   them; Enter `datasets info`'s card, `k` `datasets kits`'s; `p` and `s`
///   open the input row on `prepare ` and `synth ` and run `apogee datasets
///   <line>` after an ask; `x` deletes the dataset, asked.
///
/// Ctrl-C stops a run through its own channel: the child interrupted as
/// Ctrl-C at a terminal would, so the command records its cancellation.
namespace apogee::commands {

[[nodiscard]] tui::ListOptions train_view_options(const RootContext& context,
                                                  std::shared_ptr<tui::Progress> progress,
                                                  std::filesystem::path binary);
[[nodiscard]] tui::ListOptions datasets_view_options(const RootContext& context,
                                                     std::shared_ptr<tui::Progress> progress,
                                                     std::filesystem::path binary);

}  // namespace apogee::commands
