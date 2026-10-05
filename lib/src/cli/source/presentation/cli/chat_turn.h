#pragma once

#include <filesystem>
#include <functional>
#include <string>

#include "agent/tool.h"
#include "agentloop/loop.h"
#include "agentloop/member_call.h"
#include "agentloop/question.h"
#include "agentloop/reporter.h"
#include "agentloop/tool_selection.h"
#include "cli/permissions.h"
#include "contracts/cancellation.h"
#include "harness/harness.h"
#include "logger/session.h"

/// One chat turn, for every surface that drives one.
///
/// Extracted from `chat.cpp` when machine mode landed, and out of it into
/// this file when the task runner did (27h): the terminal REPL, a
/// JSONL-driven child and a task's rounds differ entirely in how they come
/// by their input -- a line editor with slash commands, one JSON object per
/// line, a message the runner composes -- and not at all in what a turn
/// *is*: measure the context, compact or warn, append, retrieve, run the
/// loop, persist. A task is an ordinary chat session, so its turns are these
/// turns -- the suite's resolution, consult and validation with tools,
/// recall, compaction, the per-turn save -- with nothing special-cased.
///
/// Duplicating that for any one surface would be the parity failure the
/// Reporter seam exists to prevent, one level up: context monitoring or the
/// per-turn save would reach one surface and not the other, and nobody would
/// notice until a conversation was lost.
namespace apogee::commands {

class ChatAttachments;
class ChatRecall;

/// How a chat turn decides what to retrieve from.
///
/// The FLAG is fixed for the session; the config's `auto_rag` is read again
/// on every turn, from the file, so an edit mid-conversation takes effect on
/// the next question like every other config value. That is why this holds a
/// path rather than a loaded value.
struct RagSettings {
    bool flag_given = false;
    std::string flag_value;
    int limit = 4;
    std::filesystem::path config_path;
};

/// A backend's word on its saved state (26j), as a progress note: shown
/// under `--verbose`, like the prompt cache's own line.
[[nodiscard]] harness::StatusSink progress_sink(agentloop::Reporter& reporter);

/// What a turn did -- for a caller that drives turns rather than reading
/// them, the task runner (27h). The REPL and machine mode read nothing back.
struct ChatTurnResult {
    /// The loop ran to an answer: the session gained a turn, saved.
    bool completed = false;
    /// The cancellation token ended it.
    bool cancelled = false;
    /// A provider's failure, as `notice` said it.
    std::string error;
    /// The loop's own result, when it completed.
    agentloop::RunResult run;
};

/// One turn of `session` on `input`.
///
/// `notice` is the one genuinely surface-specific part. The terminal prints
/// warnings to its status line; machine mode sends them to **stderr**,
/// because stdout carries only protocol events and a context warning is a
/// diagnostic rather than a Reporter event. Inventing an event type for it
/// would grow a second vocabulary out of the first.
///
/// A cancelled or failed turn is said through `notice` and the session saved
/// as it stands, as before; a question `ask` throws on goes on up, unsaved.
/// `cancellation` ends the turn through the loop's own cancellation; the
/// default never does.
ChatTurnResult run_chat_turn(const harness::Harness& harness, logger::Session& session,
                             const std::string& input, const agent::ToolRegistry* tools,
                             agentloop::ToolSelection* selection,
                             agentloop::MemberCalls* member_calls, const agentloop::AskFn& ask,
                             const ToolGate& gate, agentloop::Reporter& reporter,
                             const std::function<void(const std::string&)>& notice,
                             const RagSettings& rag, const std::string& review_note,
                             ChatAttachments* attached, ChatRecall* recall,
                             const harness::CancellationToken& cancellation = {});

}  // namespace apogee::commands
