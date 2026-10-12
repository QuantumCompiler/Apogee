#include "cli/knowledge.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <cstdio>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>

#include "agentloop/embed_func.h"
#include "agentloop/rerank.h"
#include "agentloop/retriever.h"
#include "agentloop/validate.h"
#include "backends/factory.h"
#include "cli/embed.h"
#include "cli/helpers.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/errors.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "harness/roles.h"
#include "knowledge/clerk.h"
#include "knowledge/export.h"
#include "knowledge/query.h"
#include "knowledge/record.h"
#include "knowledge/store.h"
#include "logger/session.h"
#include "operations/knowledge_core.h"

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

void print_fields(std::ostream& out, const knowledge::Record& record) {
    out << "Intent:      " << record.intent << "\n";
    out << "Decision:    " << or_dash(record.decision) << "\n";
    out << "Status:      " << record.status << "\n";
    out << "Discipline:  " << or_dash(record.discipline) << "\n";
    out << "Link:        " << or_dash(record.downstream_link) << "\n";
    out << "Source:      " << or_dash(record.provenance.source) << "\n";
    out << "Attribution: " << or_dash(record.provenance.attribution) << "\n";
    if (!record.supersedes.empty()) {
        out << "Supersedes:  " << record.supersedes << "\n";
    }
    if (!record.raw_ref.empty()) {
        out << "Raw:         " << record.raw_ref << "\n";
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
    if (result.validation.has_value()) {
        out["validation"] = agentloop::validation_json(*result.validation);
    }
    if (!draft) {
        out["registered"] = result.registered;
    }
    return out;
}

/// What the suite's verifier made of the record (27g), for the human report
/// -- on stderr, beside the notes, so stdout stays the record.
void print_validation(const CaptureResult& result) {
    if (!result.validation.has_value()) {
        return;
    }
    for (const std::string& line : agentloop::extraction_lines(*result.validation)) {
        std::cerr << "apogee knowledge: " << line << "\n";
    }
}

/// The config, or the defaults with a warning when it cannot be read. A
/// read of the collection never depends on the config loading -- it names
/// the default collection and the pins, and the lexical floor needs neither.
[[nodiscard]] harness::Config load_config_lenient(const RootContext& context,
                                                  std::filesystem::path& config_path) {
    try {
        config_path = harness::resolve_config_path(context.config_path);
        return harness::load_config(config_path);
    } catch (const harness::ConfigError& e) {
        std::cerr << "apogee knowledge: config not loaded -- " << e.what() << "\n";
        return harness::Config{};
    }
}

/// The collection a subcommand works on: `--db`, else the config's, else
/// `knowledge` -- a plain name, never a path.
[[nodiscard]] std::string resolve_db(const harness::Config& config, const std::string& db_flag) {
    if (!db_flag.empty()) {
        require_plain_name(db_flag);
    }
    return knowledge_collection(config, db_flag);
}

/// Opens a collection that must already exist: a read never creates an
/// empty database out of a typo.
[[nodiscard]] knowledge::Store open_collection(const std::string& db) {
    const std::filesystem::path path = collection_path(db);
    std::error_code code;
    if (!std::filesystem::exists(path, code)) {
        throw KnowledgeRefusal{"no knowledge collection named '" + db +
                                   "' -- capture a record first with 'apogee knowledge capture'",
                               false};
    }
    return knowledge::Store{path, harness::knowledge_raw_dir()};
}

[[nodiscard]] knowledge::Store open_existing(const std::string& db) {
    try {
        return open_collection(db);
    } catch (const KnowledgeRefusal& e) {
        fail_user(e.what());
    }
}

/// Runs a carved knowledge act for a command, its refusal the command's own
/// failure with its own exit code.
template <typename Act>
void or_fail(Act act) {
    try {
        act();
    } catch (const KnowledgeRefusal& e) {
        if (e.backend()) {
            fail_backend(e.what());
        }
        fail_user(e.what());
    }
}

/// Every configured provider, built so the embedder and the judge can be
/// asked of the real objects. Only constructed when a decision might need
/// one.
struct Providers {
    harness::Harness harness;

    Providers(const harness::Config& config, const std::filesystem::path& config_path)
        : harness{config} {
        backends::BuildOptions options;
        options.config_path = config_path;
        (void)backends::build_providers(harness, options);
    }
};

[[nodiscard]] std::string four_places(double value) {
    std::ostringstream out;
    out << std::fixed << std::setprecision(4) << value;
    return out.str();
}

/// One record as a listing shows it: the id and its markers, the intent,
/// the link.
void print_summary(std::ostream& out, const knowledge::Record& record, std::string_view indent) {
    out << indent << record.id << "  [" << record.status << " · " << or_dash(record.discipline)
        << "]\n";
    out << indent << "  Intent:   " << record.intent << "\n";
    if (!record.decision.empty()) {
        out << indent << "  Decision: " << record.decision << "\n";
    }
    out << indent << "  Link:     " << or_dash(record.downstream_link) << "\n";
}

[[nodiscard]] std::vector<knowledge::Record> filtered(std::vector<knowledge::Record> records,
                                                      std::string_view status,
                                                      std::string_view discipline) {
    std::vector<knowledge::Record> out;
    for (knowledge::Record& record : records) {
        if (!status.empty() && record.status != status) {
            continue;
        }
        if (!discipline.empty() && record.discipline != discipline) {
            continue;
        }
        out.push_back(std::move(record));
    }
    return out;
}

struct QueryFlags {
    std::string text;
    std::string status{knowledge::kStatusShipped};
    std::string discipline;
    int top_k = 5;
    std::string retriever;
    std::string rerank;
    std::string db;
    bool json = false;
    bool graph = false;
};

struct ListFlags {
    std::string status;
    std::string discipline;
    std::string db;
    bool json = false;
};

struct InfoFlags {
    std::string id;
    std::string db;
    bool raw = false;
    bool json = false;
};

struct EditFlags {
    std::string id;
    std::string value;
    std::string db;
};

struct ExportFlags {
    std::string format{"json"};
    std::string status;
    std::string discipline;
    std::string out;
    std::string db;
    bool anonymize = false;
};

struct ReindexFlags {
    std::string id;
    std::string db;
    std::string model;
};

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
    capture->add_option("--input", flags->input, "Read the conversation from this file")
        ->type_name(kPathValue);
    capture
        ->add_option("--from-chat", flags->from_chat,
                     "Distil a saved chat session, by id or name (see 'apogee chats list')")
        ->type_name(kChatValue);
    capture
        ->add_option("--status", flags->status,
                     "Override the clerk: shipped, rejected, or superseded")
        ->type_name(words_value(knowledge::valid_statuses()))
        ->check([](const std::string& value) {
            return knowledge::is_valid_status(knowledge::normalize_status(value))
                       ? std::string{}
                       : "'" + value + "' is not a status (shipped, rejected, superseded)";
        });
    capture
        ->add_option("--discipline", flags->discipline,
                     "Override the clerk: program, product, project, ux, or eng")
        ->type_name(words_value(knowledge::valid_disciplines()));
    capture->add_option("--source", flags->source,
                        "The source surface (chat, meeting, manual, ...); overrides the clerk");
    capture
        ->add_option("--link", flags->link,
                     "The artifact this decision produced (a ticket, a commit, a file)")
        ->type_name(kPathValue);
    capture
        ->add_option("--supersedes", flags->supersedes,
                     "The id of the record this one replaces; that record is marked superseded")
        ->type_name(kRecordValue);
    capture
        ->add_option("--retriever", flags->retriever,
                     "How the record is indexed: lexical (text only), vector, or auto")
        ->type_name(words_value(agentloop::ingest_retriever_names()))
        ->check([](const std::string& value) {
            return agentloop::valid_retriever(value)
                       ? std::string{}
                       : agentloop::retriever_values_message("", value);
        });
    capture
        ->add_option("-m,--model", flags->model,
                     "Backend that runs the clerk (default: the extraction role, then the "
                     "default backend)")
        ->type_name(kBackendValue);
    capture
        ->add_option("--db", flags->db,
                     "Collection to store into (default: knowledge.db, then 'knowledge')")
        ->type_name(kCollectionValue);
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
            print_fields(std::cout, result.record);
            print_validation(result);
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
        print_fields(std::cout, result.record);
        print_validation(result);
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

    // ---- query --------------------------------------------------------------
    auto q = std::make_shared<QueryFlags>();
    CLI::App* query = cmd->add_subcommand("query", "Find the records that best answer a question");
    query->add_option("TEXT", q->text, "What to ask")->required();
    query
        ->add_option("--status", q->status,
                     "Keep only this branch (default shipped; \"\" for every branch)")
        ->type_name(words_value(knowledge::valid_statuses()))
        ->check([](const std::string& value) {
            return value.empty() || knowledge::is_valid_status(value)
                       ? std::string{}
                       : "'" + value + "' is not a status (shipped, rejected, superseded, or \"\")";
        });
    query->add_option("--discipline", q->discipline, "Keep only this discipline")
        ->type_name(words_value(knowledge::valid_disciplines()));
    query->add_option("-n,--top-k", q->top_k, "How many records to return (default 5)");
    query->add_option("--retriever", q->retriever, "lexical, vector, hybrid, or auto")
        ->type_name(words_value(agentloop::retriever_names()))
        ->check([](const std::string& value) {
            return agentloop::valid_retriever(value)
                       ? std::string{}
                       : agentloop::retriever_values_message("", value);
        });
    query
        ->add_option("--rerank", q->rerank,
                     "Backend that reorders the matches with one generation call, on (the utility "
                     "model), or off")
        ->type_name(kBackendValue);
    query->add_option("--db", q->db, "The collection (default: knowledge.db, then 'knowledge')")
        ->type_name(kCollectionValue);
    query->add_flag("--json", q->json, "Print the result as JSON");
    query->add_flag("--graph", q->graph,
                    "Also walk the knowledge graph from the matched records: the entities their "
                    "reasoning concerns, other decisions about the same things, supersedes chains");
    query->callback([&context, q]() {
        or_fail([&]() {
            const KnowledgeQuery request{.question = q->text,
                                         .status = q->status,
                                         .discipline = q->discipline,
                                         .top_k = q->top_k,
                                         .retriever = q->retriever,
                                         .rerank = q->rerank,
                                         .db = q->db};
            const KnowledgeAnswer answer = run_knowledge_query(context, request);
            const knowledge::QueryResult& result = answer.result;
            // The walk from the matched records, through the same core the
            // HTTP twin calls; a note only when no graph covers the collection.
            std::optional<knowledge::RecordGraph> graph;
            if (q->graph) {
                std::filesystem::path config_path;
                const harness::Config config = load_config_lenient(context, config_path);
                const knowledge::Store store = open_collection(answer.db);
                graph = knowledge::graph_for_records(store, config, answer.db, result, q->text);
            }
            if (q->json) {
                nlohmann::json records = nlohmann::json::array();
                for (const knowledge::ScoredRecord& scored : result.records) {
                    records.push_back({{"record", scored.record}, {"score", scored.score}});
                }
                nlohmann::json out{
                    {"records", std::move(records)},
                    {"retriever", std::string{agentloop::to_string(result.retriever)}},
                    {"reranked", result.reranked}};
                if (!result.notes.empty()) {
                    out["notes"] = result.notes;
                }
                if (graph.has_value()) {
                    nlohmann::json section{{"context", graph->context},
                                           {"entities", graph->entities}};
                    if (!graph->note.empty()) {
                        section["note"] = graph->note;
                    }
                    out["graph"] = std::move(section);
                }
                std::cout << out.dump(2) << "\n";
                return;
            }
            print_knowledge_answer(std::cout, request, answer);
            if (result.excluded || result.records.empty()) {
                return;
            }
            if (graph.has_value()) {
                if (!graph->note.empty()) {
                    std::cout << "\n" << graph->note << "\n";
                } else if (graph->context.empty()) {
                    std::cout << "\nNothing related in the knowledge graph.\n";
                } else {
                    std::cout << "\nRelated (knowledge graph, " << graph->entities
                              << " entities):\n";
                    std::size_t start = 0;
                    while (start <= graph->context.size()) {
                        const std::size_t newline = graph->context.find('\n', start);
                        std::cout << "  "
                                  << graph->context.substr(start, newline == std::string::npos
                                                                      ? std::string::npos
                                                                      : newline - start)
                                  << "\n";
                        if (newline == std::string::npos) {
                            break;
                        }
                        start = newline + 1;
                    }
                }
            }
        });
    });

    // ---- list ---------------------------------------------------------------
    auto l = std::make_shared<ListFlags>();
    CLI::App* list = cmd->add_subcommand("list", "List the records, newest first");
    list->add_option("--status", l->status, "Keep only this branch")
        ->type_name(words_value(knowledge::valid_statuses()));
    list->add_option("--discipline", l->discipline, "Keep only this discipline")
        ->type_name(words_value(knowledge::valid_disciplines()));
    list->add_option("--db", l->db, "The collection (default: knowledge.db, then 'knowledge')")
        ->type_name(kCollectionValue);
    list->add_flag("--json", l->json, "Print the records as JSON");
    auto list_format = std::make_shared<ReadFormat>(ReadFormat::Text);
    add_read_format(list, list_format);
    list->callback([&context, l, list_format]() {
        if (!l->db.empty()) {
            require_plain_name(l->db);
        }
        or_fail([&]() {
            const KnowledgeRecords read =
                read_knowledge_records(context, l->db, l->status, l->discipline);
            if (l->json) {
                std::cout << nlohmann::json(read.records).dump(2) << "\n";
                return;
            }
            if (*list_format == ReadFormat::Json) {
                write_document(std::cout, knowledge_list_document(read));
                return;
            }
            std::cout << knowledge_list_text(read);
        });
    });

    // ---- info ---------------------------------------------------------------
    auto i = std::make_shared<InfoFlags>();
    CLI::App* info = cmd->add_subcommand("info", "Show one record in full");
    info->add_option("ID", i->id, "The record id")->type_name(kRecordValue)->required();
    info->add_flag("--raw", i->raw, "Also print the archived raw conversation");
    info->add_option("--db", i->db, "The collection (default: knowledge.db, then 'knowledge')")
        ->type_name(kCollectionValue);
    info->add_flag("--json", i->json, "Print the record as JSON");
    auto info_format = std::make_shared<ReadFormat>(ReadFormat::Text);
    add_read_format(info, info_format);
    info->callback([&context, i, info_format]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_lenient(context, config_path);
        const std::string db = resolve_db(config, i->db);
        const knowledge::Store store = open_existing(db);
        const std::optional<knowledge::Record> record = store.get(i->id);
        if (!record.has_value()) {
            fail_user("no record '" + i->id + "' in '" + db + "'");
        }
        if (i->json) {
            std::cout << nlohmann::json(*record).dump(2) << "\n";
            return;
        }
        if (*info_format == ReadFormat::Json) {
            write_document(std::cout, nlohmann::json(*record));
            return;
        }
        std::cout << knowledge_record_text(*record);
        if (i->raw) {
            const std::optional<std::string> raw = store.read_raw(*record);
            if (raw.has_value()) {
                std::cout << "\n-- Raw conversation --\n" << *raw << "\n";
            } else {
                std::cout << "\nRaw conversation: (unavailable)\n";
            }
        }
    });

    // ---- link / status / delete ---------------------------------------------
    auto lk = std::make_shared<EditFlags>();
    CLI::App* link = cmd->add_subcommand(
        "link", "Set the record's downstream link -- the artifact the decision produced");
    link->add_option("ID", lk->id, "The record id")->type_name(kRecordValue)->required();
    link->add_option("REF", lk->value, "A ticket, a commit, a PR, a file")
        ->type_name(kPathValue)
        ->required();
    link->add_option("--db", lk->db, "The collection (default: knowledge.db, then 'knowledge')")
        ->type_name(kCollectionValue);
    link->callback([&context, lk]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_lenient(context, config_path);
        const std::string db = resolve_db(config, lk->db);
        knowledge::Store store = open_existing(db);
        const std::optional<knowledge::Record> record = store.set_link(lk->id, lk->value);
        if (!record.has_value()) {
            fail_user("no record '" + lk->id + "' in '" + db + "'");
        }
        std::cout << "Linked " << record->id << " -> " << record->downstream_link << "\n";
    });

    auto st = std::make_shared<EditFlags>();
    CLI::App* status = cmd->add_subcommand("status", "Change the record's branch marker");
    status->add_option("ID", st->id, "The record id")->type_name(kRecordValue)->required();
    status->add_option("STATUS", st->value, "shipped, rejected, or superseded")
        ->type_name(words_value(knowledge::valid_statuses()))
        ->required()
        ->check([](const std::string& value) {
            return knowledge::is_valid_status(knowledge::normalize_status(value))
                       ? std::string{}
                       : "'" + value + "' is not a status (shipped, rejected, superseded)";
        });
    status->add_option("--db", st->db, "The collection (default: knowledge.db, then 'knowledge')")
        ->type_name(kCollectionValue);
    status->callback([&context, st]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_lenient(context, config_path);
        const std::string db = resolve_db(config, st->db);
        knowledge::Store store = open_existing(db);
        const std::optional<knowledge::Record> record =
            store.set_status(st->id, knowledge::normalize_status(st->value));
        if (!record.has_value()) {
            fail_user("no record '" + st->id + "' in '" + db + "'");
        }
        std::cout << record->id << " is now " << record->status << "\n";
    });

    auto del = std::make_shared<EditFlags>();
    CLI::App* remove = cmd->add_subcommand("delete", "Remove a record and its raw archive");
    remove->add_option("ID", del->id, "The record id")->type_name(kRecordValue)->required();
    remove->add_option("--db", del->db, "The collection (default: knowledge.db, then 'knowledge')")
        ->type_name(kCollectionValue);
    remove->callback([&context, del]() {
        if (!del->db.empty()) {
            require_plain_name(del->db);
        }
        or_fail([&]() { std::cout << delete_knowledge_record(context, del->db, del->id); });
    });

    // ---- export -------------------------------------------------------------
    // What `--format` offers; its check also takes `md` for markdown.
    static constexpr std::array<std::string_view, 2> kExportFormats{"json", "markdown"};
    auto ex = std::make_shared<ExportFlags>();
    CLI::App* export_cmd = cmd->add_subcommand(
        "export", "Export the records as JSON or a Markdown report, optionally anonymized");
    export_cmd->add_option("--format", ex->format, "json (default) or markdown")
        ->type_name(words_value(kExportFormats))
        ->check([](const std::string& value) {
            return value == "json" || value == "markdown" || value == "md"
                       ? std::string{}
                       : "'" + value + "' is not a format (json, markdown)";
        });
    export_cmd->add_flag("--anonymize", ex->anonymize,
                         "Strip attribution and the local raw_ref; keep the provenance chain");
    export_cmd->add_option("--status", ex->status, "Keep only this branch")
        ->type_name(words_value(knowledge::valid_statuses()));
    export_cmd->add_option("--discipline", ex->discipline, "Keep only this discipline")
        ->type_name(words_value(knowledge::valid_disciplines()));
    export_cmd->add_option("-o,--out", ex->out, "Write to this file instead of stdout")
        ->type_name(kPathValue);
    export_cmd
        ->add_option("--db", ex->db, "The collection (default: knowledge.db, then 'knowledge')")
        ->type_name(kCollectionValue);
    export_cmd->callback([&context, ex]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_lenient(context, config_path);
        const std::string db = resolve_db(config, ex->db);
        const knowledge::Store store = open_existing(db);
        const std::vector<knowledge::Record> records = knowledge::export_records(
            filtered(store.list(), ex->status, ex->discipline), ex->anonymize);
        const std::string rendered = ex->format == "json" ? nlohmann::json(records).dump(2)
                                                          : knowledge::render_markdown(records);
        if (ex->out.empty()) {
            std::cout << rendered << "\n";
            return;
        }
        try {
            harness::write_file_atomically(harness::expand_env_and_home(ex->out), rendered + "\n");
        } catch (const std::exception& e) {
            fail_user(std::string{"could not write "} + ex->out + ": " + e.what());
        }
        std::cout << "Exported " << records.size() << " record(s) to " << ex->out << "\n";
    });

    // ---- reindex ------------------------------------------------------------
    auto rx = std::make_shared<ReindexFlags>();
    CLI::App* reindex = cmd->add_subcommand(
        "reindex", "Re-embed the records' vectors after an embedding-model change");
    reindex->add_option("ID", rx->id, "One record to reindex (default: every record)")
        ->type_name(kRecordValue);
    reindex
        ->add_option("-m,--model", rx->model,
                     "Embedding backend to reindex with (default: the collection's)")
        ->type_name(kBackendValue);
    reindex->add_option("--db", rx->db, "The collection (default: knowledge.db, then 'knowledge')")
        ->type_name(kCollectionValue);
    reindex->callback([&context, rx]() {
        std::filesystem::path config_path;
        const harness::Config config = load_config_lenient(context, config_path);
        const std::string db = resolve_db(config, rx->db);
        knowledge::Store store = open_existing(db);
        if (!rx->model.empty() && config.find_backend(rx->model) == nullptr) {
            fail_user("no backend named '" + rx->model + "'");
        }

        // Vector-only, and honest about the cases where that is a no-op --
        // before any embedding backend is built.
        const std::vector<knowledge::Record> records = store.list();
        if (records.empty()) {
            std::cout << "Collection \"" << db << "\" holds no records -- nothing to reindex.\n";
            return;
        }
        if (!store.has_vectors()) {
            if (rx->id.empty()) {
                std::cout << "Collection \"" << db << "\" holds " << records.size()
                          << " record(s), none embedded -- nothing to reindex.\n";
            } else {
                std::cout << "Record '" << rx->id << "' is in collection \"" << db
                          << "\", which holds no embedded records -- nothing to reindex.\n";
            }
            std::cout << "Its text index is maintained automatically, so 'apogee knowledge query' "
                         "works as is.\nTo switch this collection to vector search, re-capture its "
                         "records with --retriever vector.\n";
            return;
        }
        if (rx->id.empty()) {
            // A mixed collection: reindexing embeds every record, including
            // the ones deliberately captured lexically. Said before it is done.
            if (const std::size_t lexical = store.vectorless_count(); lexical > 0) {
                std::cout << "Note: " << lexical << " of " << records.size() << " record(s) in \""
                          << db << "\" have no vectors (captured lexically).\n"
                          << "Reindexing embeds every record, including those -- pass an id to "
                             "reindex just one.\n";
            }
        }

        const Providers providers{config, config_path};
        const harness::EmbeddingConfig* entry = config.find_embedding(db);
        const std::string backend =
            rx->model.empty() ? (entry != nullptr ? entry->backend : std::string{}) : rx->model;
        std::string reason;
        const std::optional<agentloop::Embedder> embedder =
            agentloop::resolve_embedder(providers.harness, config, backend, reason);
        if (!embedder.has_value()) {
            fail_user("reindex needs an embedding backend -- " +
                      (reason.empty() ? std::string{"none is configured"} : reason));
        }
        std::vector<std::string> ids;
        if (!rx->id.empty()) {
            ids.push_back(rx->id);
        }
        const knowledge::Store::ReindexOutcome outcome = store.reindex(
            [&embedder](std::string_view text) {
                const std::vector<std::vector<float>> vectors =
                    embedder->embed({std::string{text}}, {});
                return vectors.empty() ? std::vector<float>{} : vectors.front();
            },
            ids);
        if (!outcome.error.empty()) {
            if (outcome.error.starts_with("no record")) {
                fail_user(outcome.error);
            }
            fail_backend("reindex failed after " + std::to_string(outcome.reindexed) +
                         " record(s): " + outcome.error);
        }
        // The space the vectors now live in.
        const embedstore::Store::Stats stats = store.chunks().stats();
        if (stats.dimension > 0) {
            store.chunks().set_embedding_model(embedder->model, stats.dimension);
        }
        std::cout << "Reindexed " << outcome.reindexed << " record(s) in \"" << db << "\" ["
                  << embedder->model << "]\n";
    });
}

KnowledgeRecords read_knowledge_records(const RootContext& context, const std::string& db,
                                        const std::string& status, const std::string& discipline) {
    std::filesystem::path config_path;
    const harness::Config config = load_config_lenient(context, config_path);
    KnowledgeRecords read;
    read.db = knowledge_collection(config, db);
    const knowledge::Store store = open_collection(read.db);
    read.records = filtered(store.list(), status, discipline);
    return read;
}

std::string knowledge_list_text(const KnowledgeRecords& read) {
    if (read.records.empty()) {
        return "No records in \"" + read.db + "\".\n";
    }
    std::ostringstream out;
    out << read.records.size() << " record(s) in \"" << read.db << "\":\n";
    for (const knowledge::Record& record : read.records) {
        out << "\n  " << record.id << "  [" << record.status << " · " << or_dash(record.discipline)
            << "]\n";
        out << "    " << preview_text(record.intent, 100) << "\n";
        out << "    -> " << or_dash(record.downstream_link) << "\n";
    }
    return out.str();
}

nlohmann::json knowledge_list_document(const KnowledgeRecords& read) {
    return nlohmann::json{{"object", "list"}, {"db", read.db}, {"data", read.records}};
}

std::string knowledge_record_text(const knowledge::Record& record) {
    std::ostringstream out;
    out << "ID:          " << record.id << "\n";
    print_fields(out, record);
    out << "Captured:    " << record.timestamp << "\n";
    return out.str();
}

KnowledgeAnswer run_knowledge_query(const RootContext& context, const KnowledgeQuery& query) {
    std::filesystem::path config_path;
    const harness::Config config = load_config_lenient(context, config_path);
    KnowledgeAnswer answer;
    answer.db = knowledge_collection(config, query.db);
    const knowledge::Store store = open_collection(answer.db);
    if (!agentloop::valid_rerank(query.rerank, config)) {
        throw KnowledgeRefusal{"--rerank: no backend named '" + query.rerank + "'", false};
    }

    knowledge::QueryOptions options;
    options.question = query.question;
    options.status = query.status;
    options.discipline = query.discipline;
    options.top_k = query.top_k;
    options.retriever_flag = query.retriever == "auto" ? std::string{} : query.retriever;
    options.rerank_flag = query.rerank;

    // Providers only when the decision might need an embedder or a judge:
    // the lexical floor asks for neither, and building them for nothing
    // would spawn what a text search never needs.
    const harness::EmbeddingConfig* entry = config.find_embedding(answer.db);
    const std::string pin = entry != nullptr ? entry->retriever : std::string{};
    const std::string rerank_pin = entry != nullptr ? entry->rerank : std::string{};
    const bool judge =
        !agentloop::resolve_turn_rerank(query.rerank, rerank_pin, config).backend.empty();
    std::optional<Providers> providers;
    if ((options.retriever_flag != "lexical" && pin != "lexical") || judge) {
        providers.emplace(config, config_path);
    }
    const harness::Harness bare{config};
    answer.result = knowledge::query(store, providers.has_value() ? providers->harness : bare,
                                     config, answer.db, options);
    if (!answer.result.ok()) {
        throw KnowledgeRefusal{answer.result.error, answer.result.backend_error};
    }
    return answer;
}

void print_knowledge_answer(std::ostream& out, const KnowledgeQuery& query,
                            const KnowledgeAnswer& answer) {
    const knowledge::QueryResult& result = answer.result;
    for (const std::string& note : result.notes) {
        out << note << "\n";
    }
    if (result.excluded) {
        return;
    }
    if (result.records.empty()) {
        out << "No matching records in \"" << answer.db << "\" ["
            << agentloop::to_string(result.retriever) << "].\n";
        if (!query.status.empty()) {
            out << "(filtering status=\"" << query.status
                << "\" -- pass --status \"\" to search all branches)\n";
        }
        return;
    }
    out << "Top " << result.records.size() << " result(s) for \"" << query.question << "\" in \""
        << answer.db << "\" [" << agentloop::to_string(result.retriever)
        << (result.reranked ? ", reranked" : "") << "]:\n";
    for (const knowledge::ScoredRecord& scored : result.records) {
        out << "\n[score " << four_places(scored.score) << "] ";
        print_summary(out, scored.record, "");
    }
}

std::string knowledge_query_text(const RootContext& context, const KnowledgeQuery& query) {
    std::ostringstream out;
    print_knowledge_answer(out, query, run_knowledge_query(context, query));
    return out.str();
}

std::string delete_knowledge_record(const RootContext& context, const std::string& db,
                                    const std::string& id) {
    std::filesystem::path config_path;
    const harness::Config config = load_config_lenient(context, config_path);
    const std::string resolved = knowledge_collection(config, db);
    knowledge::Store store = open_collection(resolved);
    if (!store.remove(id)) {
        throw KnowledgeRefusal{"no record '" + id + "' in '" + resolved + "'", false};
    }
    return "Deleted " + id + "\n";
}

}  // namespace apogee::commands
