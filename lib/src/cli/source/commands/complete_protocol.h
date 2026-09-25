#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <string>
#include <string_view>
#include <vector>

#include "commands/command.h"
#include "harness/config.h"

/// The hidden `apogee __complete` verb — the shell-completion protocol.
///
/// **Why a verb rather than static completion files.** The interesting things
/// to complete are not fixed: backend names come from the user's config, and a
/// completion file generated at build time cannot know them. Ommi's completions
/// worked this way (cobra's protocol) for exactly that reason, and the small
/// per-shell stubs Apogee installs do nothing but call back into the binary —
/// so `apogee complete -m <TAB>` offers the backends this user actually has,
/// and keeps working after they add one.
///
/// **Output contract.** One candidate per line on stdout, nothing else. Errors
/// are silent and produce no candidates: a completion handler that prints a
/// diagnostic corrupts the user's command line, and a broken config should make
/// tab-completion unhelpful, never disruptive.
namespace apogee::commands {

/// What is being completed, derived from the words before the cursor.
struct CompletionRequest {
    /// Words after the program name, up to but excluding the one being typed.
    std::vector<std::string> words;

    /// The partial word under the cursor. May be empty.
    std::string current;
};

/// What an argument's value is, as far as completion is concerned.
///
/// Every argument has one, read from what the command declared -- never from a
/// list here. Untagged means free text, because most arguments are: of the
/// ~230 options, about thirty take a path.
enum class ValueKind : std::uint8_t {
    /// Free text, a number, a name: nothing to offer, so the shell is told what
    /// the word is instead. Offering the working directory's files for
    /// `--model` -- what happened before -- reads as a suggestion.
    Text,
    /// A filesystem path is an accepted form -- declared `kPathValue`, or
    /// validated as a file or directory. The shell's own file completion.
    Path,
    /// One of a fixed set the parser validates (`CLI::IsMember`), read from
    /// that validator: `config add-backend --type`.
    Choice,
    /// A configured backend's name -- declared `kBackendValue`.
    Backend,
    /// The name of something that exists -- a collection, a chat, a model,
    /// a run -- declared with one of `kNameValues`, listed by a
    /// `CompletionSources`.
    Names,
};

/// What one flag's value, or one positional, takes.
struct ValueSpec {
    ValueKind kind = ValueKind::Text;
    /// The accepted values, for `ValueKind::Choice`.
    std::vector<std::string> choices;
    /// Which names, for `ValueKind::Names`: the declared type name.
    std::string source;
    /// The declared type name as written -- `TEXT`, `INT`, `COLLECTION` --
    /// so a test can tell free text from a number.
    std::string type;
    /// What the word is -- `--model TEXT: Model name` -- for a shell that can
    /// show it when there is nothing to offer.
    std::string hint;
};

/// What the parser knows about one command: its verbs, its flags, and what
/// each of its arguments takes -- at every depth.
///
/// Gathered from the live `CLI::App` tree rather than restated here. A hand-kept
/// list is the same drift the layout contract exists to prevent, one level
/// down, and it happened: this protocol once read only the top level and kept
/// `config`'s verbs by hand, so `models <TAB>` offered nothing and the `config`
/// list was four verbs behind. Nobody files a bug about tab-completion being
/// slightly less helpful.
struct CommandSpec {
    std::string name;
    /// Every accepted spelling, `-m` and `--model` alike, dashes included.
    std::vector<std::string> flags;
    /// The spellings that consume the next word, and what that word is. A flag
    /// not here takes no value, so the next word is never mistaken for one.
    std::map<std::string, ValueSpec> values;
    /// The positionals, in order.
    std::vector<ValueSpec> positionals;
    /// Whether the last positional takes any number of words.
    bool last_positional_repeats = false;
    /// Other names the parser accepts (`kn` for `knowledge`). Recognised on the
    /// line, never offered: offering both would double every list.
    std::vector<std::string> aliases;
    /// Visible subcommands. Hidden ones (`group("")`) are left out at every
    /// depth -- they are protocols, not features.
    std::vector<CommandSpec> subcommands;
};

/// What to offer for one word, and what the shell should do when that is
/// nothing.
struct Completion {
    std::vector<std::string> candidates;
    /// The word is a path: the shell's own file completion is the answer.
    bool files = false;
    /// What the word is, when nothing can be offered for it.
    std::string hint;
};

/// What the words before the cursor already say, for a list that depends on
/// them: `models convert <model> --from <TAB>` offers that model's ids.
struct CompletionContext {
    const harness::Config* config = nullptr;
    /// The positionals given so far to the command in play, in order.
    std::vector<std::string> positionals;
    /// The value each flag was given, by the spelling used -- the last one
    /// when a flag repeats.
    std::map<std::string, std::string> flags;
};

/// The names of one kind that exist now.
struct NameList {
    std::vector<std::string> names;
    /// The kind also accepts a path: when no name matches what is typed, the
    /// shell's file names are the answer.
    bool paths = false;
    /// Said instead when there is none to offer, e.g. "none yet -- 'apogee
    /// embed ingest' makes one".
    std::string none;
};

/// Lists a name kind (`kNameValues`). Injected, so the protocol stays pure and
/// the test for it needs no disk; `default_completion_sources` reads the real
/// config and data directory. A source that throws offers nothing.
using CompletionSources =
    std::function<NameList(std::string_view kind, const CompletionContext& context)>;

/// The environment variable a stub sets to receive the directive line (see
/// `render_completion`). A stub that does not set it gets bare candidates, so
/// a stub and a binary from different releases still work together -- and
/// they do get out of step: the stubs and the binary are installed separately.
inline constexpr const char* kCompletionProtocolVar = "APOGEE_COMPLETION_PROTOCOL";

/// What to offer for `request`, given `config` and the command tree under
/// `root`.
///
/// Pure: takes the config and the tree rather than reading either, so the
/// whole protocol is testable without a config file or a parser.
///
/// Names come from `sources`; without one, a name kind says what it wants, as
/// free text does.
[[nodiscard]] Completion complete_words(const CompletionRequest& request,
                                        const harness::Config& config, const CommandSpec& root,
                                        const CompletionSources& sources = {});

/// `complete_words(...).candidates`.
[[nodiscard]] std::vector<std::string> completion_candidates(const CompletionRequest& request,
                                                             const harness::Config& config,
                                                             const CommandSpec& root,
                                                             const CompletionSources& sources = {});

/// The protocol's output. Bare: one candidate per line. With directives, the
/// first line says what the rest mean -- `:values` (the candidates follow),
/// `:files` (complete a path), or `:hint <text>` (nothing to offer; show the
/// text) -- so a shell never falls back to file names for a word that is not
/// a path.
[[nodiscard]] std::string render_completion(const Completion& completion, bool directives);

/// Reads the whole command tree out of a parser; the root has an empty name.
[[nodiscard]] CommandSpec specs_from_app(const CLI::App& app);

/// Filters `candidates` to those starting with `prefix`.
[[nodiscard]] std::vector<std::string> filter_prefix(const std::vector<std::string>& candidates,
                                                     std::string_view prefix);

class CompleteProtocolCommand final : public Command {
public:
    [[nodiscard]] std::string_view name() const noexcept override;
    [[nodiscard]] std::string_view summary() const noexcept override;
    void bind(CLI::App& root, const RootContext& context) override;
};

}  // namespace apogee::commands
