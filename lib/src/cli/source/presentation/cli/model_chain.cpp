#include "cli/model_chain.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <iostream>
#include <utility>

#include "cli/helpers.h"
#include "cli/interrupt.h"
#include "cli/job_stream.h"
#include "cli/notes.h"

namespace apogee::commands {

namespace {

/// A note's words as the summary says them, with one prefix (55d): a note
/// the stage worded as its standalone verb does -- `note: `, `warning: ` --
/// as it is, any other given `note: `. Never two, never none; the writer puts
/// `apogee models: ` before it (CLI-M60).
[[nodiscard]] std::string said_as_note(const std::string& note) {
    if (note.starts_with("note: ") || note.starts_with("warning: ")) {
        return note;
    }
    return "note: " + note;
}

}  // namespace

void ChainLog::note(const std::string& note) {
    if (std::ranges::find(notes, note) == notes.end()) {
        notes.push_back(note);
    }
}

ChainLog run_chain(const std::vector<ChainStage>& stages, ChainLog log, std::ostream& out) {
    const std::size_t count = stages.size();
    // A job's stream says each stage as an event, where the terminal has its
    // `[i/n]` line (52b): the one producer, framed for a machine.
    JobStream* const job = JobStream::current();
    // One scope over every stage and the gaps between them (57i): each
    // stage's own joins it, so a Ctrl-C after one stage ends is the next
    // one's stop, never lost to a token armed afresh.
    const InterruptScope interrupt;
    for (std::size_t index = 0; index < count; ++index) {
        const ChainStage& stage = stages[index];
        const std::string where =
            "[" + std::to_string(index + 1) + "/" + std::to_string(count) + "] " + stage.label;
        if (job != nullptr) {
            job->events().emit_stage(index + 1, count, stage.label, "started");
        } else {
            out << (index == 0 ? "" : "\n") << where << "\n";
            out.flush();
        }
        const std::string resume = log.resume;
        try {
            if (InterruptScope::token().stop_requested()) {
                // Stopped between two stages: nothing of this one began.
                std::cerr << "apogee models: cancelled -- nothing was written\n";
                if (job != nullptr) {
                    job->failed("cancelled -- nothing was written");
                }
                throw CLI::RuntimeError(kCancelled);
            }
            stage.run(log);
        } catch (...) {
            if (job != nullptr) {
                job->events().emit_stage(index + 1, count, stage.label, "failed");
            }
            // The stage has said why. This says what is left, and the one
            // command that picks it up -- then the failure goes on, carrying
            // the stage's own exit code.
            out << "\nstopped at " << where << " -- what the stages before it made is kept.\n";
            if (!log.resume.empty()) {
                out << "resume with:\n  " << log.resume << "\n";
            }
            out.flush();
            throw;
        }
        if (job != nullptr) {
            job->events().emit_stage(index + 1, count, stage.label, "done");
        }
        if (log.resume != resume) {
            // Said now rather than only on a failure: a run killed outright
            // gets no chance to say it later.
            out << "\nfrom here on, if this stops, resume with:\n  " << log.resume << "\n";
        }
    }

    if (!log.ready.empty()) {
        out << "\nready:\n";
        for (const std::string& line : log.ready) {
            out << "  " << line << "\n";
        }
    }
    // Where the summary said them, after what is ready and before the next
    // step: each on stderr in the house voice, or a notice on an open job
    // stream (CLI-M60). The result -- `ready:`, `next` -- stays on `out`.
    out.flush();
    for (const std::string& note : log.notes) {
        say_note("models", said_as_note(note));
    }
    if (!log.next.empty()) {
        out << "\n" << log.next << "\n";
    }
    out.flush();
    return log;
}

}  // namespace apogee::commands
