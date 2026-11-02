#pragma once

#include <nlohmann/json.hpp>

#include <chrono>
#include <condition_variable>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <ostream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

#include "acp/host_servers.h"
#include "acp/jsonrpc.h"
#include "acp/translate.h"
#include "platform/child_process.h"

/// One ACP session and its child (56a).
///
/// **A session is a captured child of the binary's own `execute`**, in
/// machine mode, started in the session's folder -- the way the macOS app
/// drives Apogee, moved inside the binary. So a prompt is one turn of
/// execute's loop, opened through execute's own path (ADR 0011): the
/// project's context read, the folder attached, the coder persona. ACP never
/// gets an opening of its own, and a fatal error in a local model takes down
/// one session, not the host's agent.
///
/// A reader thread serves the child's stdout, translating each event; a
/// second drains its stderr into an 8 KiB tail -- folded into a failure,
/// and under `--verbose` copied raw to `acp`'s own stderr. The child is ended
/// by closing its stdin, as any driver ends machine mode.
///
/// **The host's MCP servers (56b)** reach the child in a handover file
/// (`acp/host_servers.h`) named by a hidden `execute` option: the child
/// connects them beside the config's for its life alone, and says what it
/// could not in one notice. The file is removed once read -- by the child,
/// and by the session once the child has opened or ended, should it not.
///
/// **A session is a saved chat (56c).** A new one is an ordinary Apogee
/// chat, written by its child with its first answered turn (M28) and named
/// by its `chat_id`, which is the session's id from `session/new`'s answer
/// on; the host's name and the folder ride hidden `execute` options, saved
/// as the chat's `origin` and `cwd`. A loaded or resumed one is the child
/// started with `--resume <id>` -- execute's own resume, its notes and its
/// fallbacks (ADR 0011) -- and a load replays the saved turns before it
/// answers.
namespace apogee::acp {

/// A saved chat as a load or a resume reads it (56c).
struct SavedChat {
    /// The chat's own id, whatever the store was asked by: a session opens
    /// a chat by this id alone.
    std::string id{};
    /// Its entries, in order.
    std::vector<SavedEntry> entries{};
};

/// The one session store, as the composition root reads it for `acp` (56c):
/// the store `chats list` and `chats show` read, never one of ACP's own.
struct SavedChats {
    /// Every saved chat, newest first.
    std::function<std::vector<ListedChat>()> list{};
    /// The chat `id` names -- read as `--resume` reads it, so a name or a
    /// title answers too, and the chat's own id says which it was; nullopt,
    /// with `error` saying so (`no chat named '<id>'`), when no chat answers
    /// to it.
    std::function<std::optional<SavedChat>(const std::string& id, std::string& error)> read{};
};

/// How a session opens (56c).
enum class OpenAs : std::uint8_t {
    /// `session/new`: a new chat, the host's.
    New,
    /// `session/load`: a saved chat resumed, its turns replayed before the
    /// answer.
    Load,
    /// `session/resume`: a saved chat resumed, nothing replayed.
    Resume,
};

/// What every session of one `apogee acp` is launched with, composed by
/// `cli/acp_cmd`.
struct AgentSetup {
    /// The binary's own `execute` in machine mode, with the flags the `acp`
    /// line carries and its root; each session sets the folder.
    platform::ChildCommand child{};
    /// Every configured backend that chats, the default first.
    std::vector<ModelChoice> models{};
    /// The model a session starts on: `-m`, else the roles chain's default.
    /// The child's `session` event says what it runs, and wins.
    std::string model{};
    /// The permission mode a session starts in: `--permission-mode`, else
    /// manual.
    std::string mode{"manual"};
    /// `--allow-bypass` (or `--permission-mode bypass`): bypass is offered.
    bool bypass_allowed = false;
    /// `--verbose`: each child's raw stderr is copied to `acp`'s own.
    bool verbose = false;
    /// The home folder each child inherits: where a call's `~/...` target is
    /// located. Empty, such a target is drawn with no location.
    std::filesystem::path home{};
    /// Where a session's handover of the host's MCP servers is made (56b);
    /// empty, the system's temporary folder.
    std::filesystem::path handover_root{};
    /// The saved chats `session/list` and `session/load` read (56c); unset,
    /// there are none.
    SavedChats chats{};
};

/// What the host said of itself at `initialize`.
struct HostCaps {
    /// `clientCapabilities.session.notices`: it shows ACP's `notice` update.
    bool notices = false;
    /// `clientCapabilities.elicitation.form`: it takes a form.
    bool elicitation_form = false;
    /// `clientInfo`, as given.
    std::string name{};
    std::string version{};
};

/// `acp`'s own stderr, written whole lines at a time from any thread.
class Diagnostics {
public:
    explicit Diagnostics(std::ostream& err) : err_{&err} {}

    /// `apogee acp: <text>` on its own line.
    void line(std::string_view text);
    /// A child's stderr bytes, as they came (`--verbose`).
    void raw(std::string_view bytes);

private:
    std::mutex mutex_;
    std::ostream* err_;
};

class Session {
public:
    Session(std::string id, std::filesystem::path cwd, const AgentSetup& setup,
            const HostCaps& host, Peer& peer, Diagnostics& diagnostics);
    /// Ends the child and waits for it, if nothing did.
    ~Session();

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;
    Session(Session&&) = delete;
    Session& operator=(Session&&) = delete;

    /// The session's id: the one it was opened under, and for a new one,
    /// from its child's `session` event on, the chat's `chat_id` (56c).
    [[nodiscard]] std::string id();

    /// Starts the child, handing it `servers` -- the host's MCP servers
    /// (56b), when it named any. The request -- `request_id`, a
    /// `session/new`, or as `as` says a `session/load` or `session/resume`
    /// of the chat this session's id names (56c) -- is answered when the
    /// child says it is open (its `session` event), after it has connected
    /// them and, for a load, after `replay` is sent; or with an error when it
    /// cannot start or ends first.
    void open(const nlohmann::json& request_id, const HostServers& servers = {},
              OpenAs as = OpenAs::New, std::vector<SavedEntry> replay = {});

    /// `session/prompt`: one `user` line, the turn answered when its
    /// `result` or `error` arrives.
    void prompt(const nlohmann::json& request_id, const nlohmann::json& params);

    /// `session/cancel`: a `cancel` line to the turn in flight; it ends
    /// `cancelled`, never in an error.
    void cancel();

    /// Whether `request_id` is the prompt in flight (`$/cancel_request`).
    [[nodiscard]] bool prompting(const nlohmann::json& request_id);

    /// `session/set_config_option`: the model or the mode, as a `set` line --
    /// the menu answered at once, the child told between turns: a setting
    /// chosen while a turn runs (a permission sheet open among it) is written
    /// as the turn ends, so it applies from the next.
    void set_config_option(const nlohmann::json& request_id, const nlohmann::json& params);

    /// `session/set_mode`: the permission mode, as a `set` line, told the
    /// child as `set_config_option` tells it.
    void set_mode(const nlohmann::json& request_id, const nlohmann::json& params);

    /// Ends the session -- a newer one replaced it, or the host left: a turn
    /// in flight is cancelled and the child's stdin closed.
    void end();

    /// Waits until the child has ended, `budget` at most before it is
    /// terminated.
    void wait(std::chrono::milliseconds budget);

    /// Its child has ended and the host holds no question of it: nothing
    /// will reach it again, so it can be let go.
    [[nodiscard]] bool finished();

private:
    void read_events();
    void drain_stderr();
    void handle_event(std::string_view line);
    void translate_event(std::string_view line);
    void notice(std::unique_lock<std::mutex>& lock, const std::string& text);
    void question(std::unique_lock<std::mutex>& lock, const nlohmann::json& event);
    /// The host answered a question asked in `turn`: one `answer` line each,
    /// if that turn still waits for them.
    void answered(std::int64_t turn, const std::vector<std::string>& texts);
    /// The menu moves to `value` for `key` (`model`, `permission_mode`) and
    /// the child is told -- at once between turns; while a turn runs, as it
    /// ends (`write_held`). Under `mutex_`.
    void choose(std::string_view key, const std::string& value);
    /// Writes the settings held while the turn ran. Under `mutex_`.
    void write_held();
    void child_ended();
    void send_update(nlohmann::json update);
    /// Writes one line to the child; false once it is gone. Under `mutex_`.
    bool write_child(const std::string& line);
    [[nodiscard]] nlohmann::json options() const;
    [[nodiscard]] std::string last_stderr_line();

    /// Under `mutex_` once the child runs: its reader names a new session
    /// by its chat as it opens.
    std::string id_;
    std::filesystem::path cwd_;
    const AgentSetup& setup_;
    const HostCaps& host_;
    Peer& peer_;
    Diagnostics& diagnostics_;

    std::unique_ptr<platform::ChildProcess> child_;
    /// The handover of the host's MCP servers, until it is gone.
    std::filesystem::path handover_;
    std::thread reader_;
    std::thread stderr_reader_;

    std::mutex mutex_;
    std::condition_variable done_;
    Translator translator_;
    std::vector<ModelChoice> models_;
    std::vector<ModeChoice> modes_;
    std::string model_;
    std::string fallback_model_;
    std::string mode_;
    std::string fallback_mode_;
    std::optional<nlohmann::json> open_id_;
    /// How it opened, and for a load the saved turns its answer follows.
    OpenAs opened_as_ = OpenAs::New;
    std::vector<SavedEntry> replay_;
    std::optional<nlohmann::json> prompt_id_;
    /// A setting chosen while a turn runs, held until it ends: written then,
    /// it would wait in the child's queue, where a question asked later in
    /// the turn reads every line that is not its answer -- and drops it. The
    /// latest value of each.
    std::optional<std::string> held_model_;
    std::optional<std::string> held_mode_;
    /// Questions put to the host and not yet answered: each holds this
    /// session in the peer.
    std::size_t asking_ = 0;
    std::int64_t turn_ = 0;
    bool cancel_sent_ = false;
    bool started_ = false;
    bool ending_ = false;
    bool ended_ = false;
    std::string ended_reason_;
    std::vector<std::string> pending_notices_;

    std::mutex tail_mutex_;
    std::string tail_;
};

}  // namespace apogee::acp
