#include "commands/terminal.h"

namespace apogee::commands {

void TerminalWriter::write(std::string_view text) {
    const std::lock_guard<std::mutex> guard{mutex_};
    out_.write(text.data(), static_cast<std::streamsize>(text.size()));
    out_.flush();
}

}  // namespace apogee::commands
