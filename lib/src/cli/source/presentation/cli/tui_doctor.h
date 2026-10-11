#pragma once

#include <memory>
#include <vector>

#include "cli/command.h"
#include "cli/system_cmd.h"
#include "tui/list_view.h"

/// The shell's doctor views (37b): Check, Providers and System, each a
/// `tui::ListView` given the read its command makes and the core its command
/// calls -- never a second implementation of either (ADR 0010).
///
/// - Check: `check`'s report rows (`read_check_report`), the verdict above
///   them; Enter the row's detail and remedy, `f` the fix pass (`fix_install`)
///   after an ask, its lines shown whole.
/// - Providers: what the last explicit scan found (`last_provider_scan`) --
///   no probe of the view's own -- as `providers scan` words it
///   (`provider_rows`); `s` scans (the command's own scan, on the key), `r`
///   registers the selected provider (`register_provider`) after the offer's
///   question.
/// - System: `apogee system`'s table as a page, read each time it is shown,
///   off the shell's thread (the read waits out a 500 ms CPU window); the
///   bar keeps the ticking.
namespace apogee::commands {

[[nodiscard]] tui::ListOptions check_view_options(const RootContext& context);
[[nodiscard]] tui::ListOptions providers_view_options(const RootContext& context);
[[nodiscard]] tui::ListOptions system_view_options(SystemSeams seams = {});

/// The three, in their order on the shell.
[[nodiscard]] std::vector<std::unique_ptr<tui::ListView>> make_doctor_views(
    tui::Pump& pump, tui::Theme theme, const RootContext& context);

}  // namespace apogee::commands
