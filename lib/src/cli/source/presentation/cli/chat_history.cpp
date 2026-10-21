#include "cli/chat_history.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <iostream>
#include <memory>
#include <sstream>

#include "agentloop/content.h"
#include "backends/factory.h"
#include "cli/chat_attachments.h"
#include "cli/chat_recall.h"
#include "cli/helpers.h"
#include "cli/task_cmd.h"
#include "contracts/config.h"
#include "contracts/errors.h"
#include "contracts/paths.h"
#include "machine/json_reporter.h"
#include "operations/backend_names.h"
#include "platform/platform.h"

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

nlohmann::json session_row_view(const logger::Session& session) {
    // The backend and the roster model it pinned (52a), as the session file
    // records them -- the model empty for a chat on its backend's own.
    return nlohmann::json{{"id", session.chat_id},      {"updated", session.updated_at},
                          {"turns", session.turns},     {"name", session.display_name()},
                          {"backend", session.backend}, {"model", session.model}};
}

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
    // "Do not answer them": a small utility model (26b), shown "what is X?",
    // answered it -- and the answer, invented, became the title (found live,
    // 2026-09-28, Qwen3-VL-8B).
    return "Name the conversation below with a title of at most six words. Its questions are "
           "listed only so you can see the topic: do not answer them. Reply with the title only "
           "-- no quotes, no punctuation at the end, no explanation.";
}

harness::ChatRequest title_request(const logger::Session& session, std::string_view backend) {
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
    request.model = backend.empty() ? session.backend : std::string{backend};
    request.messages.push_back(
        harness::ChatMessage::user(title_prompt() + "\n\nThe conversation's questions:\n" + asked));
    // Six words and room to spare; a model that ignores the instruction is cut
    // short rather than left to write an essay nobody reads.
    constexpr std::int64_t kTitleTokens = 32;
    request.max_tokens = kTitleTokens;
    request.transient.side_request = true;
    // A reasoning model otherwise thinks for hundreds of tokens before six
    // words -- the most expensive part of the request, by far.
    request.thinking.mode = harness::ThinkingMode::Off;
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
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    CLI::App* list = cmd->add_subcommand("list", "List saved conversations, newest first");
    auto format = std::make_shared<ReadFormat>(ReadFormat::Text);
    add_read_format(list, format);
    list->callback([format]() {
        const std::vector<logger::Session> sessions = logger::list_sessions();
        if (*format == ReadFormat::Json) {
            // The row's facts, one object each, newest first (28h).
            nlohmann::json data = nlohmann::json::array();
            for (const logger::Session& session : sessions) {
                data.push_back(session_row_view(session));
            }
            write_document(std::cout,
                           nlohmann::json{{"object", "list"}, {"data", std::move(data)}});
            return;
        }
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

    // `chats compact` (52d): the one compaction a session's /compact runs,
    // on a saved chat -- asked first at a terminal, --yes for scripts.
    auto compact_name = std::make_shared<std::string>();
    auto compact_yes = std::make_shared<bool>(false);
    CLI::App* compact = cmd->add_subcommand(
        "compact", "Compact a saved conversation: its history folded into a summary");
    compact->add_option("name", *compact_name, "Chat id or name")
        ->type_name(kChatValue)
        ->required();
    compact->add_flag("-y,--yes", *compact_yes, "Do not ask first (for scripts)");
    compact->callback([compact_name, compact_yes, &context]() {
        if (!*compact_yes) {
            if (!platform::is_terminal(platform::StandardStream::In) || stdin_is_piped()) {
                fail(
                    "compacting rewrites a saved chat's history as a summary -- add --yes to do "
                    "it with no terminal to ask at");
            }
            logger::Session session;
            try {
                session = logger::load(*compact_name, {}).session;
            } catch (const std::exception& e) {
                fail(e.what());
            }
            std::cerr << "Compact " << session.display_name() << " (" << session.chat_id
                      << ")? Its " << session.messages.size()
                      << " messages are folded into a summary. [y/N] " << std::flush;
            std::string answer;
            if (!std::getline(std::cin, answer) ||
                (answer != "y" && answer != "Y" && answer != "yes")) {
                std::cout << "left as it was\n";
                return;
            }
        }
        std::cout << compact_saved_chat(*compact_name,
                                        harness::resolve_config_path(context.config_path))
                  << "\n";
    });

    // `chats recall` (52e): what recall holds, and with --forget every
    // chat's summary gone -- never a transcript -- shown first, asked at a
    // terminal, --yes for scripts. CLI-only, as recall is (26l).
    auto recall_forget = std::make_shared<bool>(false);
    auto recall_yes = std::make_shared<bool>(false);
    CLI::App* recall = cmd->add_subcommand(
        "recall", "Show what recall holds of earlier chats, or forget it all with --forget");
    recall->add_flag("--forget", *recall_forget,
                     "Forget every chat's summary -- the transcripts stay");
    recall->add_flag("-y,--yes", *recall_yes, "With --forget: do not ask first (for scripts)");
    recall->callback([recall_forget, recall_yes, &context]() {
        const RecallHoldings held = recall_holdings();
        const std::string holds =
            std::to_string(held.summaries) + " chat summar" + (held.summaries == 1 ? "y" : "ies") +
            " in " + held.index.string() +
            (held.pending == 0 ? std::string{}
                               : ", " + std::to_string(held.pending) + " more due one");
        if (!*recall_forget) {
            if (*recall_yes) {
                fail("--yes answers --forget's question -- give --forget too");
            }
            std::cout << "recall holds " << holds << "\n";
            return;
        }
        if (held.summaries == 0 && held.pending == 0) {
            std::cout << "recall holds nothing -- nothing to forget\n";
            return;
        }
        if (!*recall_yes) {
            if (!platform::is_terminal(platform::StandardStream::In) || stdin_is_piped()) {
                fail("forgetting recall removes " + holds +
                     " -- add --yes to do it with no terminal to ask at");
            }
            std::cerr << "Forget " << holds << "? Every transcript stays. [y/N] " << std::flush;
            std::string answer;
            if (!std::getline(std::cin, answer) ||
                (answer != "y" && answer != "Y" && answer != "yes")) {
                std::cout << "left as it was\n";
                return;
            }
        }
        const RecallHoldings forgot = forget_all_recall();
        bool recalls = true;
        try {
            recalls = harness::load_config(harness::resolve_config_path(context.config_path))
                          .memory.recall;
        } catch (const std::exception&) {
            recalls = true;
        }
        std::cout << "forgot " << forgot.summaries << " chat summar"
                  << (forgot.summaries == 1 ? "y" : "ies") << " -- every transcript stays"
                  << (recalls ? "; chats finished from now on are summarised again (apogee config "
                                "set memory.recall false stops it)"
                              : "")
                  << "\n";
    });

    auto delete_name = std::make_shared<std::string>();
    CLI::App* remove = cmd->add_subcommand("delete", "Delete a conversation");
    remove->add_option("name", *delete_name, "Chat id or name")->type_name(kChatValue)->required();
    remove->callback([delete_name]() { std::cout << delete_chat(*delete_name) << "\n"; });
}

std::string compact_conversation(const harness::Harness& harness, logger::Session& session) {
    const std::string compactor =
        helper_backend(harness.config(), harness::ModelRole::Utility, session.backend);
    session.messages = agentloop::compact_history(harness, session.messages, compactor);
    ++session.compactions;
    return compactor;
}

std::string compact_saved_chat(const std::string& name, const std::filesystem::path& config_path) {
    try {
        logger::Session session = logger::load(name, {}).session;
        // A live task writes this chat every round (27h): not rewritten
        // under it.
        if (const std::string held = task_holds_chat(session); !held.empty()) {
            fail(held);
        }
        const harness::Config config = harness::load_config(config_path);
        harness::Harness harness{config};
        // The chat's own model, as it runs: its roster pin (33) too.
        if (!session.model.empty()) {
            (void)harness.pin_model(session.backend, session.model);
        }
        backends::BuildOptions options;
        options.config_path = config_path;
        const backends::BuildResult built = backends::build_providers(harness, options);
        const std::string compactor =
            helper_backend(config, harness::ModelRole::Utility, session.backend);
        if (config.find_backend(compactor) == nullptr) {
            fail("nothing to compact with: " + session.chat_id + " ran on '" + session.backend +
                 "', which the config no longer has -- set models.default_utility");
        }
        for (const backends::BackendStatus& status : built.statuses) {
            if (status.name == compactor && !status.constructed) {
                fail("backend '" + compactor + "' is configured but unavailable -- " +
                     status.reason);
            }
        }
        const std::size_t before = session.messages.size();
        (void)compact_conversation(harness, session);
        // Its attachments told, as a live chat's are: what rode the folded
        // messages is retrieved from now on.
        std::vector<std::string> said;
        {
            ChatAttachments attached{
                harness, session, ChatAttachments::index_for(session.chat_id),
                ChatAttachments::Hooks{
                    .say = [&said](const std::string& line, bool) { said.push_back(line); },
                    .progress = {},
                    .confirm_large = {},
                    .save = true,
                    .built_in_graph = harness::AttachmentGraphMethod::Code}};
            attached.after_compaction();
        }
        logger::save(session);
        std::string out = "compacted " + session.chat_id + ": " + std::to_string(before) +
                          " messages into " + std::to_string(session.messages.size()) +
                          (compactor == session.backend ? "" : ", by " + compactor);
        for (const std::string& line : said) {
            out += "\n" + line;
        }
        return out;
    } catch (const CLI::RuntimeError&) {
        throw;
    } catch (const std::exception& e) {
        fail(e.what());
    }
}

std::string delete_chat(const std::string& name) {
    try {
        const logger::Session session = logger::load(name, {}).session;
        // A live task writes this chat every round, and resumes into it
        // (27h): it is not deleted under the task.
        if (const std::string held = task_holds_chat(session); !held.empty()) {
            fail(held);
        }
        std::error_code ec;
        std::filesystem::remove(logger::session_path(session.chat_id), ec);
        if (ec) {
            fail("could not delete: " + ec.message());
        }
        // Its attachments' index goes with it (26d): an attachment's text is
        // as private as the chat it belonged to.
        ChatAttachments::remove_index(session.chat_id);
        // And its summary: a deleted chat is recalled no more (26l).
        forget_recall(session.chat_id);
        return "deleted " + session.chat_id;
    } catch (const CLI::RuntimeError&) {
        throw;
    } catch (const std::exception& e) {
        fail(e.what());
    }
}

}  // namespace apogee::commands
