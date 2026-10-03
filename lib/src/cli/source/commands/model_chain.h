#pragma once

#include <functional>
#include <ostream>
#include <string>
#include <vector>

/// One command from a pull to a runnable backend (M3): `models pull <ref>
/// --safetensors --register[-with <levels>]` and `models convert <model>
/// --register[-with <levels>]` run pull, convert, quantize and registration
/// in turn, each stage the shipped verb's own core.
///
/// This file is only the orchestration: stages in order, each a closure that
/// does its work and reports in its own words. A stage that fails has said
/// why -- the verbs' own failure path -- and throws; the chain then says
/// where it stopped and the command that resumes it, and lets the failure go
/// on, so the exit code is the stage's. What earlier stages made stays in the
/// store: every stage commits before the next begins.
namespace apogee::commands {

/// What the stages tell the chain as they go.
struct ChainLog {
    /// The command that resumes the chain from where it now is -- the command
    /// as typed, to begin with. A stage after which the rest can be resumed
    /// more cheaply (the pull: everything after it runs offline) changes it,
    /// and the chain says so then, so a run killed outright has already shown
    /// it.
    std::string resume;
    /// The summary's lines: what is ready to use.
    std::vector<std::string> ready;
    /// What to type next, when there is one thing.
    std::string next;
    /// What a stage would warn about on its own -- a base model, a projector
    /// that could not be made -- said once, in the summary, however many
    /// stages noticed it.
    std::vector<std::string> notes;

    /// Adds `note` unless it is already there.
    void note(const std::string& note);
};

/// One stage: a label for its `[i/n]` line, and the work.
struct ChainStage {
    std::string label;
    std::function<void(ChainLog&)> run;
};

/// Runs `stages` in order, narrating on `out`, starting from `log`. Returns
/// the log once every stage has run, its summary printed. When a stage
/// throws, says where the chain stopped and how to resume, and rethrows.
ChainLog run_chain(const std::vector<ChainStage>& stages, ChainLog log, std::ostream& out);

}  // namespace apogee::commands
