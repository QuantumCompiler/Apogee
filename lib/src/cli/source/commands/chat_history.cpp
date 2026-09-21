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

std::string sanitize_title(std::string_view raw) {
    // First line only: a model asked for a title will sometimes explain its
    // choice underneath.
    const std::size_t newline = raw.find('\n');
    std::string_view line = newline == std::string_view::npos ? raw : raw.substr(0, newline);

    auto trim = [](std::string_view text) {
        while (!text.empty() && (text.front() == ' ' || text.front() == '\t' ||
                                 text.front() == '"' || text.front() == '\'')) {
            text.remove_prefix(1);
        }
        while (!text.empty() &&
               (text.back() == ' ' || text.back() == '\t' || text.back() == '"' ||
                text.back() == '\'' || text.back() == '.' || text.back() == '\r')) {
            text.remove_suffix(1);
        }
        return text;
    };

    std::string title{trim(line)};

    // Bounded so one runaway answer cannot make every listing row wrap.
    constexpr std::size_t kMaxTitle = 60;
    if (title.size() > kMaxTitle) {
        title.resize(kMaxTitle);
        while (!title.empty() && (static_cast<unsigned char>(title.back()) & 0xC0U) == 0x80U) {
            title.pop_back();  // never cut mid-codepoint
        }
        while (!title.empty() && title.back() == ' ') {
            title.pop_back();
        }
        title += "…";
    }
    return title;
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
    info->add_option("name", *info_name, "Chat id or name")->required();
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
    title->add_option("name", *title_name, "Chat id or name")->required();
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
    remove->add_option("name", *delete_name, "Chat id or name")->required();
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
