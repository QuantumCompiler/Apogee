#include "commands/model_chain.h"

#include <algorithm>
#include <utility>

namespace apogee::commands {

void ChainLog::note(const std::string& note) {
    if (std::ranges::find(notes, note) == notes.end()) {
        notes.push_back(note);
    }
}

ChainLog run_chain(const std::vector<ChainStage>& stages, ChainLog log, std::ostream& out) {
    const std::size_t count = stages.size();
    for (std::size_t index = 0; index < count; ++index) {
        const ChainStage& stage = stages[index];
        const std::string where =
            "[" + std::to_string(index + 1) + "/" + std::to_string(count) + "] " + stage.label;
        out << (index == 0 ? "" : "\n") << where << "\n";
        out.flush();
        const std::string resume = log.resume;
        try {
            stage.run(log);
        } catch (...) {
            // The stage has said why. This says what is left, and the one
            // command that picks it up -- then the failure goes on, carrying
            // the stage's own exit code.
            out << "\nstopped at " << where << " -- what the stages before it made is kept.\n"
                << "resume with:\n  " << log.resume << "\n";
            out.flush();
            throw;
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
        out << "note: " << note << "\n";
    }
    if (!log.next.empty()) {
        out << "\n" << log.next << "\n";
    }
    out.flush();
    return log;
}

}  // namespace apogee::commands
