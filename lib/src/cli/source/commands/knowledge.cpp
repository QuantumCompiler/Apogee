#include "commands/knowledge.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "agentloop/retriever.h"
#include "backends/factory.h"
#include "commands/helpers.h"
#include "commands/knowledge_core.h"
#include "harness/config.h"
#include "harness/errors.h"
#include "harness/paths.h"
#include "harness/roles.h"
#include "knowledge/clerk.h"
#include "knowledge/record.h"
#include "logger/session.h"

namespace apogee::commands {
namespace {

struct CaptureFlags {
    std::string positional;
    std::string input;
    std::string from_chat;
    std::string status;
    std::string discipline;
    std::string source;
    std::string link;
    std::string supersedes;
    std::string retriever;
    std::string model;
    std::string db;
    bool dry_run = false;
    bool json = false;
};

[[noreturn]] void fail_user(const std::string& message) {
    std::cerr << "apogee knowledge: " << message << "\n";
    throw CLI::RuntimeError(kUserError);
}

[[noreturn]] void fail_backend(const std::string& message) {
    std::cerr << "apogee knowledge: " << message << "\n";
    throw CLI::RuntimeError(kBackendError);
}

[[nodiscard]] std::string trim(std::string_view text) {
    std::size_t begin = 0;
    while (begin < text.size() && (text[begin] == ' ' || text[begin] == '\n' ||
                                   text[begin] == '\r' || text[begin] == '\t')) {
        ++begin;
    }
    std::size_t end = text.size();
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\n' || text[end - 1] == '\r' ||
                           text[end - 1] == '\t')) {
        --end;
    }
    return std::string{text.substr(begin, end - begin)};
}

[[nodiscard]] std::optional<std::string> read_text(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

/// Refuses a name that would escape the embeddings directory -- the rule
/// `embed` applies: a collection is NAMED, not pathed.
void require_plain_name(std::string_view name) {
    if (name.find("..") != std::string_view::npos || name.find('/') != std::string_view::npos ||
        name.find('\\') != std::string_view::npos) {
        fail_user("'" + std::string{name} + "' is not a plain collection name");
    }
}

/// The raw conversation: the argument, else `--input`, else `--from-chat`,
/// else piped stdin. `source` is set to `chat` for a saved session when the
/// user named none.
[[nodiscard]] std::string resolve_raw(const CaptureFlags& flags, std::string& source) {
    if (!flags.positional.empty()) {
        return trim(flags.positional);
    }
    if (!flags.input.empty()) {
        const std::optional<std::string> text = read_text(std::filesystem::path{flags.input});
        if (!text.has_value()) {
            fail_user("cannot read " + flags.input);
        }
        return trim(*text);
    }
    if (!flags.from_chat.empty()) {
        logger::LoadedSession loaded;
        try {
            loaded = logger::load(flags.from_chat, {});
        } catch (const std::exception& e) {
            fail_user(e.what());
        }
        if (source.empty()) {
            source = "chat";
        }
        return logger::transcript_text(loaded.session.messages);
    }
    if (stdin_is_piped()) {
        return trim(read_stdin());
    }
    return {};
}

[[nodiscard]] std::string or_dash(const std::string& value) {
    return value.empty() ? "-" : value;
}

void print_fields(const knowledge::Record& record) {
    std::cout << "Intent:      " << record.intent << "\n";
    std::cout << "Decision:    " << or_dash(record.decision) << "\n";
    std::cout << "Status:      " << record.status << "\n";
    std::cout << "Discipline:  " << or_dash(record.discipline) << "\n";
    std::cout << "Link:        " << or_dash(record.downstream_link) << "\n";
    std::cout << "Source:      " << or_dash(record.provenance.source) << "\n";
    std::cout << "Attribution: " << or_dash(record.provenance.attribution) << "\n";
    if (!record.supersedes.empty()) {
        std::cout << "Supersedes:  " << record.supersedes << "\n";
    }
    if (!record.raw_ref.empty()) {
        std::cout << "Raw:         " << record.raw_ref << "\n";
    }
}

[[nodiscard]] nlohmann::json envelope(const CaptureResult& result, bool draft) {
    nlohmann::json out{{"draft", draft},
                       {"record", result.record},
                       {"db", result.decision.db},
                       {"retriever", std::string{agentloop::to_string(result.decision.retriever)}}};
    if (!result.decision.warning.empty()) {
        out["warning"] = result.decision.warning;
    }
    if (!result.decision.note.empty()) {
        out["note"] = result.decision.note;
    }
    if (!result.notes.empty()) {
        out["notes"] = result.notes;
    }
    if (!draft) {
        out["registered"] = result.registered;
    }
    return out;
}

}  // namespace

std::string_view KnowledgeCommand::name() const noexcept {
    return "knowledge";
}

std::string_view KnowledgeCommand::summary() const noexcept {
    return "Capture the why behind decisions as searchable records";
}

void KnowledgeCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->alias("kn");
    cmd->require_subcommand(1);

    auto flags = std::make_shared<CaptureFlags>();
    CLI::App* capture =
        cmd->add_subcommand("capture", "Distil a conversation into one decision record");
    // Upper-case so it cannot collide with a long flag name: CLI11 matches a
    // positional against every long name.
    capture->add_option("TEXT", flags->positional,
                        "The raw conversation (or --input, --from-chat, or piped stdin)");
    capture->add_option("--input", flags->input, "Read the conversation from this file");
    capture->add_option("--from-chat", flags->from_chat,
                        "Distil a saved chat session, by id or name (see 'apogee chats list')");
    capture
        ->add_option("--status", flags->status,
                     "Override the clerk: shipped, rejected, or superseded")
        ->check([](const std::string& value) {
            return knowledge::is_valid_status(knowledge::normalize_status(value))
                       ? std::string{}
                       : "'" + value + "' is not a status (shipped, rejected, superseded)";
        });
    capture->add_option("--discipline", flags->discipline,
                        "Override the clerk: program, product, project, ux, or eng");
    capture->add_option("--source", flags->source,
                        "The source surface (chat, meeting, manual, ...); overrides the clerk");
    capture->add_option("--link", flags->link,
                        "The artifact this decision produced (a ticket, a commit, a file)");
    capture->add_option("--supersedes", flags->supersedes,
                        "The id of the record this one replaces; that record is marked superseded");
    capture
        ->add_option("--retriever", flags->retriever,
                     "How the record is indexed: lexical (text only), vector, or auto")
        ->check([](const std::string& value) {
            return agentloop::valid_retriever(value)
                       ? std::string{}
                       : agentloop::retriever_values_message("", value);
        });
    capture->add_option("-m,--model", flags->model,
                        "Backend that runs the clerk (default: the extraction role, then the "
                        "default backend)");
    capture->add_option("--db", flags->db,
                        "Collection to store into (default: knowledge.db, then 'knowledge')");
    capture->add_flag("--dry-run", flags->dry_run,
                      "Run the clerk and print the record and the store decision; write nothing");
    capture->add_flag("--json", flags->json, "Print the result as JSON");

    capture->callback([&context, flags]() {
        harness::Config config;
        std::filesystem::path config_path;
        try {
            config_path = harness::resolve_config_path(context.config_path);
            config = harness::load_config(config_path);
        } catch (const harness::ConfigError& e) {
            fail_user(e.what());
        }
        if (!flags->db.empty()) {
            require_plain_name(flags->db);
        }

        std::string source = flags->source;
        const std::string raw = resolve_raw(*flags, source);
        if (raw.empty()) {
            fail_user(
                "no conversation provided -- pass it as an argument, via --input <file> or "
                "--from-chat <id>, or piped on stdin");
        }

        // --- the clerk's backend, and the refusal by type -------------------
        // The extraction role: a capture is structured extraction, so a
        // cheaper extractor configured for that role runs it, and `-m` wins.
        if (!flags->model.empty() && !names_a_configured_backend(config, flags->model)) {
            std::string known;
            for (const std::string& name : config.backend_names()) {
                known += known.empty() ? "" : ", ";
                known += name;
            }
            fail_user("no backend named '" + flags->model + "'" +
                      (known.empty() ? "" : " (configured: " + known + ")"));
        }
        const std::string model = harness::resolve_backend_key(
            config,
            harness::RoleRequest{.role = harness::ModelRole::Extraction, .override = flags->model});
        if (model.empty()) {
            fail_user("no backend configured to run the clerk -- set models.default, or pass -m");
        }
        if (const harness::BackendConfig* entry =
                config.find_backend(configured_backend_key(config, model));
            entry != nullptr && harness::is_vendor_cli(entry->type)) {
            // Refused BEFORE anything is built, so no vendor CLI is spawned:
            // the same rule `analyze` and `serve` apply.
            fail_user("backend '" + model + "' is a vendor-CLI backend (type " +
                      std::string{harness::to_string(entry->type)} +
                      "); the capture clerk runs on API-billing and local backends only");
        }

        harness::Harness harness{config};
        backends::BuildOptions build_options;
        build_options.config_path = config_path;
        const backends::BuildResult built = backends::build_providers(harness, build_options);
        if (built.constructed_count() == 0) {
            std::string message = "no usable backend is configured";
            if (!built.skipped_summary().empty()) {
                message += " -- " + built.skipped_summary();
            } else {
                message += " (add one with 'apogee config add-backend')";
            }
            fail_user(message);
        }
        for (const backends::BackendStatus& status : built.statuses) {
            if (!status.constructed && status.name == model) {
                fail_user("backend '" + model + "' is configured but unavailable -- " +
                          status.reason);
            }
        }

        CaptureInputs inputs;
        inputs.raw = raw;
        // Passed as typed: `draft_record` normalises every override with the
        // clerk's own synonyms, so `--status live` lands as `shipped` there.
        inputs.overrides.status = flags->status;
        inputs.overrides.discipline = flags->discipline;
        inputs.overrides.source = source;
        inputs.overrides.link = flags->link;
        inputs.overrides.supersedes = flags->supersedes;
        inputs.db = flags->db;
        inputs.retriever_flag = flags->retriever == "auto" ? std::string{} : flags->retriever;
        const knowledge::ClerkFn clerk = knowledge::make_structured_clerk(harness, model);

        const auto deliver_failure = [](const CaptureResult& result) {
            if (result.backend_error) {
                fail_backend(result.error);
            }
            fail_user(result.error);
        };

        if (flags->dry_run) {
            // The clerk and every override, exactly as a real run -- and then
            // nothing: no chunk, no raw file, no config edit.
            const CaptureResult result = draft_capture(harness, config, inputs, clerk);
            if (!result.ok()) {
                deliver_failure(result);
            }
            if (flags->json) {
                std::cout << envelope(result, true).dump(2) << "\n";
                return;
            }
            std::cout << "Draft (not stored)\n";
            print_fields(result.record);
            std::cout << "\nWould store in \"" << result.decision.db
                      << "\" (retriever: " << agentloop::to_string(result.decision.retriever)
                      << ")\n";
            if (!result.decision.note.empty()) {
                std::cout << result.decision.note << "\n";
            }
            if (!result.decision.warning.empty()) {
                std::cout << "Warning: a real capture would fail -- " << result.decision.warning
                          << "\n";
            }
            std::cout << "Re-run without --dry-run to store it.\n";
            return;
        }

        const CaptureResult result = capture_and_store(harness, config, config_path, inputs, clerk);
        if (!result.ok()) {
            deliver_failure(result);
        }
        if (flags->json) {
            std::cout << envelope(result, false).dump(2) << "\n";
            for (const std::string& note : result.notes) {
                std::cerr << "apogee knowledge: " << note << "\n";
            }
            return;
        }
        std::cout << "Captured " << result.record.id << "\n";
        print_fields(result.record);
        std::cout << "\nStored in \"" << result.decision.db
                  << "\" (retriever: " << agentloop::to_string(result.decision.retriever) << ")";
        if (result.decision.retriever == agentloop::Retriever::Lexical) {
            std::cout << " -- searchable by text; no vector written";
        }
        std::cout << "\n";
        if (!result.decision.note.empty()) {
            std::cout << result.decision.note << "\n";
        }
        if (result.registered) {
            std::cout << "registered '" << result.decision.db << "' in " << config_path.string()
                      << "\n";
        }
        for (const std::string& note : result.notes) {
            std::cerr << "apogee knowledge: " << note << "\n";
        }
    });
}

}  // namespace apogee::commands
