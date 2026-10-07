#pragma once

#include <array>
#include <string>
#include <string_view>
#include <vector>

namespace CLI {
class App;
}

namespace apogee::commands {

/// The type name that registers an option's value as a configured backend's
/// name: `->type_name(kBackendValue)` on a flag or a positional, on any
/// subcommand at any depth.
///
/// **This is the completion registration, and the only one.** Shell completion
/// reads it out of the live parser (`specs_from_app`), so a backend-valued
/// option completes to the user's backends the moment it is declared -- and
/// `--help` shows BACKEND where it showed TEXT, so the same word documents the
/// argument. It replaced a list keyed by spelling, which offered backends for
/// `config add-backend --model` (a vendor model id) and nothing for `--judge`.
inline constexpr const char* kBackendValue = "BACKEND";

/// The type name that registers an option's value as a filesystem path --
/// wherever a path is one accepted form, "a kit name or path" included -- so
/// completion offers the shell's file names there and nowhere else. An
/// untagged value is free text, and TAB says what it wants instead of listing
/// files: before, `--model <TAB>` offered the working directory's contents.
inline constexpr const char* kPathValue = "PATH";

/// Type names that register an option's value as the name of something that
/// exists -- in the config, on disk, or in the repository -- so completion
/// offers what is there (`complete_sources.h` says where each list comes
/// from) and `--help` names the thing: `--rag COLLECTION`, not `--rag TEXT`.
/// The same one-registration rule as `kBackendValue`.
///
/// A kind that also accepts a path (`KIT`, `DATASET`, `SNAPSHOT` ...) offers
/// its names while any match what is typed, and the shell's file names once
/// none do -- so `./` or `~/` still completes a path.
inline constexpr const char* kCollectionValue = "COLLECTION";
/// Comma-separated collections: each word after the last comma completes.
inline constexpr const char* kCollectionListValue = "COLLECTION,...";
/// A named graph (a `graphs:` entry) or a collection -- what `graph` takes.
inline constexpr const char* kGraphValue = "GRAPH";
/// A `graphs:` entry only.
inline constexpr const char* kNamedGraphValue = "NAMED_GRAPH";
/// A `graphs:` entry with source trees -- what `graph update` refreshes.
inline constexpr const char* kSourcedGraphValue = "SOURCED_GRAPH";
inline constexpr const char* kAgentValue = "AGENT";
/// A saved conversation, by id.
inline constexpr const char* kChatValue = "CHAT";
/// A configured MCP server.
inline constexpr const char* kServerValue = "SERVER";
/// A prepared dataset, or a `.jsonl` path.
inline constexpr const char* kDatasetValue = "DATASET";
/// A training kit, bundled or the user's, or a path.
inline constexpr const char* kKitValue = "KIT";
/// An eval suite, a prepared `<name>.eval`, a kit, or a path.
inline constexpr const char* kSuiteValue = "SUITE";
/// A `suites:` entry -- a named bundle of models (27d). Not `SUITE`, which
/// is the training track's eval suite.
inline constexpr const char* kModelSuiteValue = "MODEL_SUITE";
/// A `suites:` entry, or `off` for none -- what `select_suite` takes, where a
/// command may run without a suite. `execute --suite` is `MODEL_SUITE`: it
/// never runs without one (27s).
inline constexpr const char* kModelSuiteOrOffValue = "MODEL_SUITE_OR_OFF";
/// A training run id.
inline constexpr const char* kRunValue = "RUN";
/// A pipeline run id.
inline constexpr const char* kPipelineRunValue = "PIPELINE_RUN";
/// A task's id (27h).
inline constexpr const char* kTaskValue = "TASK";
/// A task `task halt` stops, and one `task cancel` does -- each by the verb's
/// own test (`task_stoppable`).
inline constexpr const char* kHaltableTaskValue = "HALTABLE_TASK";
inline constexpr const char* kCancellableTaskValue = "CANCELLABLE_TASK";
/// A task `task resume` continues from the working folder: unfinished, and
/// started there (`task_resume_refusal`).
inline constexpr const char* kResumableTaskValue = "RESUMABLE_TASK";
/// A symphony (27q): a config entry, a shipped starter, a spec file under
/// `symphonies/` -- or a spec file's path.
inline constexpr const char* kSymphonyValue = "SYMPHONY";
/// A symphony by name, never a path -- what `symphonies edit` takes.
inline constexpr const char* kSymphonyNameValue = "SYMPHONY_NAME";
/// A `symphonies:` config entry -- what `symphonies delete` removes.
inline constexpr const char* kSymphonyEntryValue = "SYMPHONY_ENTRY";
/// A `training.pipelines` entry, or a spec path.
inline constexpr const char* kPipelineValue = "PIPELINE";
/// A `training.regimes` entry, or a spec path.
inline constexpr const char* kRegimeValue = "REGIME";
/// A model in the store, or one set of its weights (`<model>/<format>/<id>`).
inline constexpr const char* kModelValue = "MODEL";
/// A model with SafeTensors weights, one set of them, or a snapshot directory.
inline constexpr const char* kSnapshotValue = "SNAPSHOT";
/// A model with a GGUF, one GGUF of it, or a `.gguf` path.
inline constexpr const char* kGgufValue = "GGUF";
/// A configured backend, or one set of stored weights (`<model>/<format>/<id>`)
/// -- what `models info` shows.
inline constexpr const char* kBackendOrWeightsValue = "BACKEND_OR_WEIGHTS";
/// A model in the store, one set of its weights, or a backend whose model file
/// is a stored GGUF -- what `models delete` takes (M7).
inline constexpr const char* kModelOrBackendValue = "MODEL_OR_BACKEND";
/// A name for a new backend: free text, offering the stems of the stored GGUFs
/// no backend points at -- each a name that fills itself in (M7).
inline constexpr const char* kNewBackendValue = "NEW_BACKEND";
/// A SafeTensors set's id -- of the model named by the command's first
/// positional.
inline constexpr const char* kSnapshotIdValue = "SNAPSHOT_ID";
/// A GGUF's id -- of the model named by the command's first positional.
inline constexpr const char* kGgufIdValue = "GGUF_ID";
/// A precision `models convert` writes: a GGUF's -- or, with `--mlx` on the
/// line before it, an MLX model's. Validated against both by the parser; the
/// line narrows the offer to the one the command will take.
inline constexpr const char* kConvertPrecisionValue = "PRECISION";
/// A model to pull: the models in the local Ollama store (a Hugging Face
/// `owner/repo` is typed; there is no listing to offer).
inline constexpr const char* kPullRefValue = "MODEL_REF";
/// A knowledge record id -- in the collection `--db` names, else the default.
inline constexpr const char* kRecordValue = "RECORD";
/// A git branch, remote branch or tag in the working directory's repository.
inline constexpr const char* kGitRefValue = "GIT_REF";
inline constexpr const char* kGitRemoteValue = "REMOTE";
/// A dotted config key `config get` reads.
inline constexpr const char* kConfigKeyValue = "KEY";
/// A tool a `permissions:` entry can name.
inline constexpr const char* kToolValue = "TOOL";
/// A host in `tools.allowed_hosts`.
inline constexpr const char* kAllowedHostValue = "ALLOWED_HOST";

/// Every name kind above, for the protocol to recognise and a test to hold
/// each to a source.
inline constexpr std::array<std::string_view, 40> kNameValues{kCollectionValue,
                                                              kCollectionListValue,
                                                              kGraphValue,
                                                              kNamedGraphValue,
                                                              kSourcedGraphValue,
                                                              kAgentValue,
                                                              kChatValue,
                                                              kServerValue,
                                                              kDatasetValue,
                                                              kKitValue,
                                                              kSuiteValue,
                                                              kModelSuiteValue,
                                                              kModelSuiteOrOffValue,
                                                              kRunValue,
                                                              kPipelineRunValue,
                                                              kTaskValue,
                                                              kHaltableTaskValue,
                                                              kCancellableTaskValue,
                                                              kResumableTaskValue,
                                                              kPipelineValue,
                                                              kRegimeValue,
                                                              kModelValue,
                                                              kSnapshotValue,
                                                              kGgufValue,
                                                              kBackendOrWeightsValue,
                                                              kModelOrBackendValue,
                                                              kNewBackendValue,
                                                              kSnapshotIdValue,
                                                              kGgufIdValue,
                                                              kConvertPrecisionValue,
                                                              kPullRefValue,
                                                              kRecordValue,
                                                              kGitRefValue,
                                                              kGitRemoteValue,
                                                              kConfigKeyValue,
                                                              kToolValue,
                                                              kAllowedHostValue,
                                                              kSymphonyValue,
                                                              kSymphonyNameValue,
                                                              kSymphonyEntryValue};

/// `{a,b}`: a set of words, as a type name spells it.
template <typename Words>
[[nodiscard]] std::string word_set(const Words& words) {
    std::string out = "{";
    bool first = true;
    for (const auto& word : words) {
        out += first ? "" : ",";
        out += std::string_view{word};
        first = false;
    }
    return out + "}";
}

/// The type name for free text with a known set of usual words: the parser
/// still takes any word -- the command validates it, with its own message,
/// and some accept "" to mean none -- and completion offers these. `--help`
/// shows `TEXT:{a,b}`, as it shows a `CLI::IsMember` set. Pass the list the
/// command validates against, never a copy of it.
template <typename Words>
[[nodiscard]] std::string words_value(const Words& words) {
    return "TEXT:" + word_set(words);
}

/// `words_value` for a comma-separated list of the words in one value
/// (`--consultable utility,extraction`): the `,...` after the type marks a
/// list, as `COLLECTION,...` does, so the word after the last comma
/// completes.
template <typename Words>
[[nodiscard]] std::string word_list_value(const Words& words) {
    return "TEXT,...:" + word_set(words);
}

/// The type name for `KEY=VALUE,...` (`--toolset chat=fs,git`): the keys,
/// each offered with its `=`, then -- once one is typed -- the values, a
/// comma-separated list after it. `--help` shows
/// `TEXT:{chat=,utility=}:{fs,git}`.
template <typename Keys, typename Values>
[[nodiscard]] std::string keyed_words_value(const Keys& keys, const Values& values) {
    std::vector<std::string> prefixed;
    for (const auto& key : keys) {
        prefixed.push_back(std::string{std::string_view{key}} + "=");
    }
    return words_value(prefixed) + ":" + word_set(values);
}

/// Root-level state every subcommand can read.
///
/// Populated by CLI11 while it parses, which means a command must read these
/// fields inside its callback and never at bind time -- at bind time they are
/// still empty.
struct RootContext {
    /// Value of the persistent `--config` flag; empty when the user did not
    /// pass one, in which case the config engine's default resolution applies.
    std::string config_path;

    /// Every registered subcommand name, in registration order.
    ///
    /// Here so the completion protocol can offer the real command set rather
    /// than a list it maintains separately -- a second list would go stale the
    /// first time a command is added, and the symptom (tab-completion quietly
    /// missing a command) is one nobody files a bug about.
    std::vector<std::string> command_names;
};

/// One `apogee <name>` subcommand.
///
/// A command owns its own flags and its own behavior, and nothing outside it
/// knows what those are -- adding a command touches exactly two places: the
/// command's own files, and the one line in `default_registry()` that
/// constructs it. That is the self-registration property the skeleton exists
/// to establish, and it is why the CLI can grow to any number of commands
/// without main.cpp growing at all.
///
/// Signal failure from a callback by throwing `CLI::RuntimeError(code)`; the
/// root command turns it into that process exit code.
class Command {
public:
    Command() = default;
    virtual ~Command() = default;

    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    Command(Command&&) = delete;
    Command& operator=(Command&&) = delete;

    /// The subcommand word, e.g. "version". Must be unique in a registry.
    [[nodiscard]] virtual std::string_view name() const noexcept = 0;

    /// One-line description, shown in `apogee --help`.
    [[nodiscard]] virtual std::string_view summary() const noexcept = 0;

    /// Attaches this command's subcommand, flags, and callback to `root`.
    /// Called once, by CommandRegistry::bind_all. `context` outlives the app.
    virtual void bind(CLI::App& root, const RootContext& context) = 0;
};

}  // namespace apogee::commands
