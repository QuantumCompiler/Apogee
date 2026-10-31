#include "cli/model_chain.h"

#include <algorithm>
#include <utility>

#include "cli/job_stream.h"

namespace apogee::commands {

namespace {

/// A note as the summary says it, with one prefix (55d): a note the stage
/// worded as its standalone verb does -- `note: `, `warning: ` -- as it is,
/// any other given `note: `. Never two, never none.
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
    for (const std::string& note : log.notes) {
        out << said_as_note(note) << "\n";
    }
    if (!log.next.empty()) {
        out << "\n" << log.next << "\n";
    }
    out.flush();
    return log;
}

}  // namespace apogee::commands
