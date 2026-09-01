#include "commands/line_reader.h"

#include <algorithm>
#include <iostream>
#include <replxx.hxx>
#include <utility>

#include "harness/paths.h"
#include "platform/platform.h"

namespace apogee::commands {

std::filesystem::path default_history_path() {
    return harness::apogee_home() / "chat_history";
}

// ---------------------------------------------------------------------------
// PlainLineReader
// ---------------------------------------------------------------------------

PlainLineReader::PlainLineReader(std::istream& in) : in_{in} {}

std::optional<std::string> PlainLineReader::read(std::string_view prompt) {
    // The prompt is deliberately NOT written. On a pipe there is nobody to
    // read it, and whatever IS reading the output would receive "You: "
    // interleaved with the answers.
    (void)prompt;

    std::string line;
    if (!std::getline(in_, line)) {
        return std::nullopt;
    }
    // getline strips '\n' but leaves '\r' from a CRLF source.
    if (!line.empty() && line.back() == '\r') {
        line.pop_back();
    }
    return line;
}

// ---------------------------------------------------------------------------
// EditingLineReader
// ---------------------------------------------------------------------------

struct EditingLineReader::Impl {
    replxx::Replxx editor;
    Options options;
    bool history_loaded = false;
};

EditingLineReader::EditingLineReader(Options options) : impl_{std::make_unique<Impl>()} {
    impl_->options = std::move(options);

    impl_->editor.set_max_history_size(static_cast<int>(impl_->options.history_limit));

    if (!impl_->options.history_path.empty()) {
        std::error_code ec;
        std::filesystem::create_directories(impl_->options.history_path.parent_path(), ec);
        // A missing or unreadable history file is not an error: a first run has
        // none, and a session must never fail to start over recall.
        impl_->editor.history_load(impl_->options.history_path.string());
        impl_->history_loaded = true;
    }

    if (!impl_->options.completions.empty()) {
        const std::vector<std::string> words = impl_->options.completions;
        impl_->editor.set_completion_callback([words](const std::string& input,
                                                      int& context_length) {
            replxx::Replxx::completions_t matches;
            // Complete only the last whitespace-separated token, so
            // "/model cla" completes the model rather than the whole line.
            const std::size_t start = input.find_last_of(" \t");
            const std::string prefix = start == std::string::npos ? input : input.substr(start + 1);
            context_length = static_cast<int>(prefix.size());

            for (const std::string& word : words) {
                if (word.rfind(prefix, 0) == 0) {
                    matches.emplace_back(word);
                }
            }
            return matches;
        });
    }
}

EditingLineReader::~EditingLineReader() {
    if (impl_ && impl_->history_loaded && !impl_->options.history_path.empty()) {
        // Best-effort: a full disk costs recall, never the conversation.
        impl_->editor.history_sync(impl_->options.history_path.string());
    }
}

std::optional<std::string> EditingLineReader::read(std::string_view prompt) {
    const char* line = impl_->editor.input(std::string{prompt});
    if (line == nullptr) {
        // EOF (Ctrl-D) or an interrupt (Ctrl-C). Both end the session, which
        // is what a user pressing either expects.
        return std::nullopt;
    }
    return std::string{line};
}

void EditingLineReader::remember(std::string_view line) {
    if (line.empty()) {
        return;
    }
    impl_->editor.history_add(std::string{line});
}

// ---------------------------------------------------------------------------

std::unique_ptr<LineReader> make_line_reader(EditingLineReader::Options options,
                                             std::istream& fallback_input) {
    // BOTH ends must be a terminal. replxx draws on stdout, so a terminal stdin
    // with a redirected stdout would write escape sequences into the redirect --
    // and that redirect is the answer the user asked for.
    const bool interactive = platform::is_terminal(platform::StandardStream::In) &&
                             platform::is_terminal(platform::StandardStream::Out);
    if (!interactive) {
        return std::make_unique<PlainLineReader>(fallback_input);
    }
    return std::make_unique<EditingLineReader>(std::move(options));
}

}  // namespace apogee::commands
