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
/// A training run id.
inline constexpr const char* kRunValue = "RUN";
/// A pipeline run id.
inline constexpr const char* kPipelineRunValue = "PIPELINE_RUN";
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
/// A SafeTensors set's id -- of the model named by the command's first
/// positional.
inline constexpr const char* kSnapshotIdValue = "SNAPSHOT_ID";
/// A GGUF's id -- of the model named by the command's first positional.
inline constexpr const char* kGgufIdValue = "GGUF_ID";
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

/// Every name kind above, for the protocol to recognise and a test to hold
/// each to a source.
inline constexpr std::array<std::string_view, 25> kNameValues{
    kCollectionValue, kCollectionListValue, kGraphValue,      kNamedGraphValue, kAgentValue,
    kChatValue,       kServerValue,         kDatasetValue,    kKitValue,        kSuiteValue,
    kRunValue,        kPipelineRunValue,    kPipelineValue,   kRegimeValue,     kModelValue,
    kSnapshotValue,   kGgufValue,           kSnapshotIdValue, kGgufIdValue,     kPullRefValue,
    kRecordValue,     kGitRefValue,         kGitRemoteValue,  kConfigKeyValue,  kToolValue};

/// The type name for free text with a known set of usual words: the parser
/// still takes any word -- the command validates it, with its own message,
/// and some accept "" to mean none -- and completion offers these. `--help`
/// shows `TEXT:{a,b}`, as it shows a `CLI::IsMember` set. Pass the list the
/// command validates against, never a copy of it.
template <typename Words>
[[nodiscard]] std::string words_value(const Words& words) {
    std::string out = "TEXT:{";
    bool first = true;
    for (const auto& word : words) {
        out += first ? "" : ",";
        out += std::string_view{word};
        first = false;
    }
    return out + "}";
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
/// to establish, and it is why the CLI can grow to Ommi's ~25 commands without
/// main.cpp growing at all.
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
