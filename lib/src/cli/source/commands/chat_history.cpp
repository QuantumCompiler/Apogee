#include "commands/chat_history.h"

#include <CLI/CLI.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>

#include "harness/errors.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee chats: " << message << "\n";
    throw CLI::RuntimeError(1);
}

/// `text` cut to at most `limit` bytes, never through a codepoint: a sequence
/// the cut would split goes whole, lead byte included.
[[nodiscard]] std::string clip(std::string_view text, std::size_t limit) {
    if (text.size() <= limit) {
        return std::string{text};
    }
    std::size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) {
        --end;  // back to the start of the codepoint the cut lands in
    }
    return std::string{text.substr(0, end)};
}

/// `text` without any of `front` at its start or of `back` at its end.
[[nodiscard]] std::string_view strip(std::string_view text, std::string_view front,
                                     std::string_view back) {
    while (!text.empty() && front.find(text.front()) != std::string_view::npos) {
        text.remove_prefix(1);
    }
    while (!text.empty() && back.find(text.back()) != std::string_view::npos) {
        text.remove_suffix(1);
    }
    return text;
}

}  // namespace

std::string format_session_row(const logger::Session& session) {
    std::ostringstream out;
    out << session.chat_id << "  " << session.updated_at << "  " << session.turns << " turns  "
        << session.display_name();
    return out.str();
}

std::string format_session_info(const logger::Session& session) {
    std::ostringstream out;
    out << "id:        " << session.chat_id << "\n";
    if (!session.custom_name.empty()) {
        out << "name:      " << session.custom_name << "\n";
    }
    if (!session.title.empty()) {
        out << "title:     " << session.title << "\n";
    }
    out << "backend:   " << (session.backend.empty() ? "(default)" : session.backend) << "\n"
        << "started:   " << session.started_at << "\n"
        << "updated:   " << session.updated_at << "\n"
        << "turns:     " << session.turns << "\n"
        << "messages:  " << session.messages.size() << "\n";
    if (session.compactions > 0) {
        out << "compacted: " << session.compactions << " time(s)\n";
    }
    if (session.params.temperature.has_value()) {
        out << "temp:      " << *session.params.temperature << "\n";
    }
    if (session.params.max_tokens.has_value()) {
        out << "max_tok:   " << *session.params.max_tokens << "\n";
    }
    if (session.schema_version == 0) {
        out << "schema:    legacy (pre-versioning)\n";
    }
    return out.str();
}

std::string title_prompt() {
    return "Give this conversation a title of at most six words. Reply with the title only "
           "-- no quotes, no punctuation at the end, no explanation.";
}

harness::ChatRequest title_request(const logger::Session& session) {
    // What the user asked, not the whole transcript: the questions say what a
    // conversation is about, and the answers are most of its length. The
    // whole transcript made a title cost a full re-read of the conversation.
    constexpr std::size_t kPerMessage = 300;
    constexpr std::size_t kTotal = 1500;
    std::string asked;
    for (const harness::ChatMessage& message : session.messages) {
        if (message.role != harness::Role::User) {
            continue;
        }
        const std::string text =
            clip(strip(message.content.plain_text(), " \t\r\n", " \t\r\n"), kPerMessage);
        if (text.empty()) {
            continue;
        }
        if (asked.size() + text.size() > kTotal) {
            break;
        }
        asked += "- " + text + "\n";
    }

    harness::ChatRequest request;
    request.model = session.backend;
    request.messages.push_back(
        harness::ChatMessage::user(title_prompt() + "\n\nWhat the user asked:\n" + asked));
    // Six words and room to spare; a model that ignores the instruction is cut
    // short rather than left to write an essay nobody reads.
    constexpr std::int64_t kTitleTokens = 32;
    request.max_tokens = kTitleTokens;
    request.transient.side_request = true;
    // A reasoning model otherwise thinks for hundreds of tokens before six
    // words -- the most expensive part of the request, by far.
    request.transient.skip_reasoning = true;
    return request;
}

std::string sanitize_title(std::string_view raw) {
    constexpr std::string_view kFront = " \t\r\"'*#`";
    constexpr std::string_view kBack = " \t\r\"'*`.";

    // The first line with anything on it. A model asked for a title will
    // sometimes explain its choice underneath -- and a reasoning model's answer
    // begins with the blank lines its closed think block leaves. Taking the
    // first line outright titled every such conversation "", and the title
    // was asked for again after every turn (found live, 2026-09-25).
    std::string_view line;
    for (std::size_t start = 0; start < raw.size() && line.empty();) {
        const std::size_t newline = raw.find('\n', start);
        const std::size_t end = newline == std::string_view::npos ? raw.size() : newline;
        line = strip(raw.substr(start, end - start), kFront, kBack);
        start = end + 1;
    }

    // Bounded so one runaway answer cannot make every listing row wrap.
    constexpr std::size_t kMaxTitle = 60;
    if (line.size() <= kMaxTitle) {
        return std::string{line};
    }
    std::string title = clip(line, kMaxTitle);
    while (!title.empty() && title.back() == ' ') {
        title.pop_back();
    }
    return title + "…";
}

std::string_view ChatsCommand::name() const noexcept {
    return "chats";
}

std::string_view ChatsCommand::summary() const noexcept {
    return "List, inspect, and rename saved conversations";
}

void ChatsCommand::bind(CLI::App& root, const RootContext& context) {
    (void)context;  // sessions live under APOGEE_HOME, not beside the config

    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    CLI::App* list = cmd->add_subcommand("list", "List saved conversations, newest first");
    list->callback([]() {
        const std::vector<logger::Session> sessions = logger::list_sessions();
        if (sessions.empty()) {
            std::cout << "no saved conversations yet\n";
            return;
        }
        for (const logger::Session& session : sessions) {
            std::cout << format_session_row(session) << "\n";
        }
    });

    auto info_name = std::make_shared<std::string>();
    CLI::App* info = cmd->add_subcommand("info", "Show one conversation's details");
    info->add_option("name", *info_name, "Chat id or name")->type_name(kChatValue)->required();
    info->callback([info_name]() {
        try {
            std::cout << format_session_info(logger::load(*info_name, {}).session);
        } catch (const std::exception& e) {
            fail(e.what());
        }
    });

    auto title_name = std::make_shared<std::string>();
    auto title_value = std::make_shared<std::string>();
    CLI::App* title = cmd->add_subcommand("title", "Rename a conversation");
    title->add_option("name", *title_name, "Chat id or name")->type_name(kChatValue)->required();
    title->add_option("title", *title_value, "The new name")->required();
    title->callback([title_name, title_value]() {
        try {
            logger::Session session = logger::load(*title_name, {}).session;
            // The id never changes: renaming sets custom_name. An id that moved
            // would break every reference to the chat that already exists.
            session.custom_name = *title_value;
            logger::save(session);
            std::cout << session.chat_id << " renamed to '" << session.custom_name << "'\n";
        } catch (const std::exception& e) {
            fail(e.what());
        }
    });

    auto delete_name = std::make_shared<std::string>();
    CLI::App* remove = cmd->add_subcommand("delete", "Delete a conversation");
    remove->add_option("name", *delete_name, "Chat id or name")->type_name(kChatValue)->required();
    remove->callback([delete_name]() {
        try {
            const logger::Session session = logger::load(*delete_name, {}).session;
            std::error_code ec;
            std::filesystem::remove(logger::session_path(session.chat_id), ec);
            if (ec) {
                fail("could not delete: " + ec.message());
            }
            std::cout << "deleted " << session.chat_id << "\n";
        } catch (const CLI::RuntimeError&) {
            throw;
        } catch (const std::exception& e) {
            fail(e.what());
        }
    });
}

}  // namespace apogee::commands
