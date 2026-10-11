#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "cli/command.h"
#include "tui/list_view.h"

/// The full-screen shell's workbench (32d): Models, Chats, Suites and Config,
/// each a `tui::ListView` given a read this file makes through the cores the
/// commands run and actions that call those cores -- never a second
/// implementation of either.
///
/// - Models: `models list`'s rows (`read_model_rows`) -- its backends and
///   the models each runs from its roster (35); Enter `/model` in the
///   session, i `models info`, d `config set-default`.
/// - Chats: `chats list`'s rows (`session_row_view`); Enter opens one in the
///   session, i `chats info`, x `chats delete`.
/// - Suites: the config's suites; Enter `/suite` in the session, d `config
///   set-default-suite`.
/// - Config: the backends as the admin plane serves them (`backend_view`: a
///   key is only ever `api_key_set`); d `config set-default`, x `config
///   delete-backend`.
///
/// A key that changes a file calls the very function the command calls, so
/// the file it leaves is the command's, byte for byte; one that removes
/// something asks first.
namespace apogee::commands {

/// What the workbench asks of the session view beside it.
struct WorkbenchHooks {
    /// Opens a saved chat in the session; returns what was done or why not.
    std::function<std::string(const std::string& chat_id)> open_chat;
    /// Moves the session to a suite; returns what was done or why not.
    std::function<std::string(const std::string& suite)> use_suite;
    /// Moves the session onto a backend or a model one runs (35), as `/model`
    /// takes it; returns what was done or why not.
    std::function<std::string(const std::string& model)> use_model;
    /// Shows the session view.
    std::function<void()> show_session;
    /// Plays a symphony through the session's own `/play` (37d), its input
    /// as typed; returns what was done or why not, the session shown when the
    /// line reached it.
    std::function<std::string(const std::string& symphony, const std::string& input)> play_symphony;
};

/// Each view's options, over the config `context` names -- the reads and
/// actions, for the shell and for the tests that hold them to the commands.
[[nodiscard]] tui::ListOptions models_view_options(const RootContext& context,
                                                   const WorkbenchHooks& hooks);
[[nodiscard]] tui::ListOptions chats_view_options(const WorkbenchHooks& hooks);
[[nodiscard]] tui::ListOptions suites_view_options(const RootContext& context,
                                                   const WorkbenchHooks& hooks);
[[nodiscard]] tui::ListOptions config_view_options(const RootContext& context);

/// The four views, built, in the order the shell numbers them.
[[nodiscard]] std::vector<std::unique_ptr<tui::ListView>> make_workbench(
    tui::Pump& pump, tui::Theme theme, const RootContext& context, const WorkbenchHooks& hooks);

}  // namespace apogee::commands
