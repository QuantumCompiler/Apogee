#include "cli/notes.h"

#include <iostream>

#include "cli/job_stream.h"

namespace apogee::commands {

std::string house_voice(std::string_view verb) {
    return "apogee " + std::string{verb} + ": ";
}

void say_note(std::string_view verb, std::string_view text) {
    if (JobStream* const job = JobStream::current(); job != nullptr) {
        // The stream's notice, the words alone; stderr carries none (CLI-M60).
        job->events().on_notice(text);
        return;
    }
    // One write of the whole line: the tie to std::cout flushes stdout first,
    // so a note follows the result it is about on a merged pipe.
    std::cerr << house_voice(verb) + std::string{text} + "\n";
}

}  // namespace apogee::commands
