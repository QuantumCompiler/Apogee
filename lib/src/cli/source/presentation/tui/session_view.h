#pragma once

#include <cstddef>
#include <cstdint>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "agentloop/reporter.h"
#include "contracts/cancellation.h"
#include "tui/pump.h"
#include "tui/theme.h"
#include "tui/view.h"
#include "views/line_reader.h"

/// The session view (32c): the conversation on the shell's stage, and the
/// view a bare `apogee` lands in.
///
/// **A Reporter adapter, not a loop.** The conversation is chat's own session
/// core (`cli/chat_session`'s `run_session`) running on its own thread; this
/// view is what it reads lines from and writes to. Its `reporter()` is the
/// fourth adapter of the agentloop's Reporter beside the terminal's, machine
/// mode's and the served stream's -- and, in-process, richer than the wire:
/// side calls (26n), which machine mode deliberately leaves out, are drawn
/// inside the turn's thinking block. The answer is rendered by `markdown/`'s
/// one renderer: this view paints its render operations, the way
/// `views/answer_view` paints them on a terminal, and parses no Markdown of
/// its own. Every question and permission prompt is put through `ask`, which
/// returns what the person typed; the core applies it through the same answer
/// paths the terminal does.
///
/// Threads: the methods marked *any thread* post to the pump and return; the
/// ones marked *the session's thread* block it until the person answers or
/// the view closes. What is drawn is only ever touched on the shell's thread.
namespace apogee::tui {

/// A saved conversation the picker offers.
struct PickerEntry {
    std::string id;
    /// What `chats list` names it: its custom name, else its title, else its id.
    std::string name;
    std::string updated;
    int turns = 0;
};

/// A door the picker offers between the new chat and the saved ones (37d:
/// an execute session under a suite): what it says, and what choosing it
/// does, on the shell's thread.
struct PickerDoor {
    std::string label;
    std::function<void()> open;
};

/// One way to answer a prompt: the key that picks it and what it says.
struct Choice {
    std::string key;
    std::string label;
    std::string description;
};

/// Something the session asks the person: a permission, a question, a yes or
/// no.
struct Prompt {
    /// What is asked, a line each -- as the core styled them for a terminal.
    std::vector<std::string> lines;
    /// The answers on offer, each picked by its key.
    std::vector<Choice> choices;
    /// Whether the person may type an answer of their own instead (a
    /// question's free text): typed and entered, it is the answer.
    bool free_text = false;
};

class SessionView {
public:
    explicit SessionView(Pump& pump, Theme theme);
    ~SessionView();

    SessionView(const SessionView&) = delete;
    SessionView& operator=(const SessionView&) = delete;
    SessionView(SessionView&&) = delete;
    SessionView& operator=(SessionView&&) = delete;

    /// The view to register with the shell: it takes typing.
    [[nodiscard]] View view();

    // --- any thread -------------------------------------------------------

    /// The picker: a new chat first, then `doors`, then `entries`, newest
    /// first. Enter on a saved chat or the new one calls `pick`, on the
    /// shell's thread, with its id -- empty for the new chat; on a door, the
    /// door's own `open`.
    void show_picker(std::vector<PickerEntry> entries, std::function<void(std::string)> pick,
                     std::vector<PickerDoor> doors = {});

    /// A conversation begins: the transcript empties, a line queued for one
    /// before it is dropped, and input is taken.
    void begin_session();
    /// What is typed completes through `suggest` -- chat's own completer,
    /// the one command table's rows and values.
    void set_completion(commands::Suggester suggest);
    /// The line above the input: the model, the suite, the chat.
    void set_header(std::string line);
    /// A line kept in the transcript, as the core styled it.
    void say(std::string line);
    /// The transient status, replaced by the next; cleared by `clear_status`.
    void set_status(std::string line);
    void clear_status();
    /// What the session's turns report to: the fourth Reporter adapter.
    [[nodiscard]] agentloop::Reporter& reporter();

    /// Enters `line` as if it were typed and Enter pressed -- what a
    /// workbench view sends the conversation (`/suite <name>`, `/exit`).
    /// False, and nothing entered, when the conversation is not waiting for a
    /// line. The shell's thread.
    [[nodiscard]] bool enter(std::string line);
    /// Queues `line` as the conversation's next line, taken by its next read
    /// whether or not it is reading yet, and shown as typed -- what a view
    /// hands a conversation it opens for it (37d: the play a Symphonies view
    /// asked for, a new session's first line). Any thread; called after
    /// `begin_session` from the same thread, it lands in that conversation.
    void queue_line(std::string line);
    /// Whether a conversation is open and waiting for its next line.
    [[nodiscard]] bool waiting_for_line() const;

    // --- the session's thread --------------------------------------------

    /// The next line the person enters, or nothing once the view closes.
    [[nodiscard]] std::optional<std::string> read_line();
    /// Puts `prompt` to the person and returns the answer: a choice's key, or
    /// the text typed. Throws `harness::CancelledError` when the turn it
    /// belongs to is stopped meanwhile, and `std::runtime_error` when the view
    /// closes with it unanswered.
    [[nodiscard]] std::string ask(const Prompt& prompt);
    /// A turn runs until `end_turn`: Ctrl-C cancels `token` -- the turn ends
    /// the way a driver's `cancel` ends one, and the session goes on.
    void begin_turn(harness::CancellationToken token);
    void end_turn();

    // --- any thread -------------------------------------------------------

    /// The shell is ending: a waiting read returns nothing, a waiting ask
    /// throws, and a running turn is cancelled.
    void close();
    [[nodiscard]] bool closed() const;

    /// The session's own keys, as the shell's Keys view lists them.
    [[nodiscard]] static std::vector<std::string> key_lines();

    struct State;

private:
    std::shared_ptr<State> state_;
};

}  // namespace apogee::tui
