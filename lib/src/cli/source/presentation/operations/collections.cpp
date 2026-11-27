#include "operations/collections.h"

#include <algorithm>
#include <exception>
#include <optional>
#include <system_error>

#include "contracts/collection_name.h"
#include "contracts/config_yaml.h"
#include "contracts/layout.h"
#include "contracts/paths.h"

namespace apogee::commands {

std::filesystem::path collection_path(std::string_view name) {
    return harness::embeddings_dir() / (std::string{name} + ".db");
}

std::vector<std::string> collection_names() {
    std::vector<std::string> names;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(harness::embeddings_dir(), code)) {
        if (code) {
            break;
        }
        if (entry.is_regular_file(code) && entry.path().extension() == ".db") {
            names.push_back(entry.path().stem().string());
        }
    }
    std::ranges::sort(names);
    return names;
}

bool collection_is_new(std::string_view name, const harness::Config* config) {
    std::error_code code;
    if (std::filesystem::exists(collection_path(name), code)) {
        return false;
    }
    if (config == nullptr) {
        return true;
    }
    const std::vector<std::string> referenced = harness::collection_references(*config);
    return std::ranges::find(referenced, name) == referenced.end();
}

namespace {

/// The create rule for a name already known to be new, folded against the
/// config's entries and the stores on disk.
[[nodiscard]] std::string create_error(std::string_view name, const harness::Config* config) {
    std::vector<std::string> existing = collection_names();
    if (config != nullptr) {
        for (const std::string& entry : config->embedding_names()) {
            existing.push_back(entry);
        }
    }
    return harness::collection_create_error(name, existing);
}

/// Said after a refusal: the plan runs before anything is written.
constexpr std::string_view kNothingWritten = "; nothing was written";

/// The edits `plan` would run, applied to `text` and kept nowhere; empty when
/// they would land, else why not.
[[nodiscard]] std::string dry_run(std::string text, const CollectionWrite& write,
                                  const CollectionWritePlan& plan) {
    try {
        if (plan.register_entry) {
            text = harness::append_embedding(text, write.name, write.entry, false);
        }
        if (plan.enable_graph) {
            text = harness::set_embedding_graph_enabled(text, write.name, true);
        }
        (void)harness::parse_edited_config(text);
    } catch (const std::exception& e) {
        return e.what();
    }
    return {};
}

/// Which edits a write runs after it, from the entry `config` holds for its
/// name: today's conditions.
void plan_edits(CollectionWritePlan& plan, const CollectionWrite& write,
                const harness::Config& config) {
    const harness::EmbeddingConfig* entry = config.find_embedding(write.name);
    plan.register_entry = entry == nullptr;
    plan.enable_graph = write.enable_graph && (entry == nullptr || !entry->graph.enabled);
}

}  // namespace

std::string new_collection_error(std::string_view name, const harness::Config* config) {
    if (std::string refused = harness::collection_read_error(name); !refused.empty()) {
        return refused;
    }
    if (!collection_is_new(name, config)) {
        return {};
    }
    return create_error(name, config);
}

std::string collection_reference_error(std::string_view name,
                                       const std::filesystem::path& config_path) {
    std::optional<harness::Config> config;
    try {
        config = harness::load_config(config_path);
    } catch (const std::exception&) {
        // Refers to nothing: the edit that follows says why it cannot load.
    }
    return new_collection_error(name, config.has_value() ? &*config : nullptr);
}

CollectionWritePlan plan_collection_write(const CollectionWrite& write) {
    CollectionWritePlan plan;
    if (std::string refused = harness::collection_read_error(write.name); !refused.empty()) {
        plan.outcome = CollectionWritePlan::Outcome::Refuse;
        plan.words = std::move(refused);
        return plan;
    }
    plan.is_new = collection_is_new(write.name, write.config);
    if (plan.is_new) {
        if (std::string refused = create_error(write.name, write.config); !refused.empty()) {
            plan.outcome = CollectionWritePlan::Outcome::Refuse;
            plan.words = std::move(refused) + std::string{kNothingWritten};
            return plan;
        }
    }
    if (write.config == nullptr || write.config_path.empty()) {
        // Nothing to edit: the caller says the config is missing, as before.
        return plan;
    }
    // The edits are decided from the file as it is now, read unlocked and
    // kept nowhere (57f): a session's config may be older than the file -- a
    // chat's first capture registers the collection its next one writes to.
    plan_edits(plan, write, *write.config);
    std::string refused;
    try {
        const std::string text =
            harness::read_config_file(harness::config_edit_target(write.config_path));
        plan_edits(plan, write, harness::parse_config(text, write.config_path.string()));
        if (!plan.register_entry && !plan.enable_graph) {
            return plan;
        }
        refused = dry_run(text, write, plan);
    } catch (const std::exception& e) {
        refused = e.what();
    }
    if (refused.empty()) {
        return plan;
    }
    const std::string quoted = harness::quoted_collection_name(write.name);
    if (plan.is_new) {
        plan.outcome = CollectionWritePlan::Outcome::Refuse;
        plan.words = "collection " + quoted + " could not be registered in " +
                     write.config_path.string() + " -- " + refused + std::string{kNothingWritten};
        plan.register_entry = false;
        plan.enable_graph = false;
        return plan;
    }
    // Not new: the store is written and said, in one line. When the name is
    // what the config cannot hold, the create rule says why (its words keep
    // the name on one line) and the way out is a plain name; another refusal
    // names its own fix (`config migrate`, a hand edit).
    const bool name_is_why = !harness::yaml_plain_key(write.name);
    if (name_is_why) {
        refused = harness::collection_create_error(write.name, {});
    }
    const std::string store = harness::quoted_collection_name(collection_path(write.name).string());
    const std::string kept = write.records ? "settings" : "chunking";
    plan.outcome = CollectionWritePlan::Outcome::Note;
    if (plan.register_entry) {
        plan.words = quoted + " cannot be registered in the config -- " + refused +
                     "; without an entry it keeps no " + kept +
                     " of its own and retrieval won't expand through its graph";
        if (name_is_why) {
            plan.words +=
                write.records
                    ? " -- to keep them, move " + store +
                          " to a plain name and capture with --db <that name>, or set "
                          "knowledge.db to it by hand"
                    : " -- to keep them, move " + store + " to a plain name and re-ingest";
        }
    } else {
        plan.words = "graph.enabled cannot be set on " + quoted + " in the config -- " + refused +
                     "; retrieval won't expand through its graph";
        if (name_is_why) {
            plan.words += " -- to have it, move " + store + " to a plain name, " +
                          (write.records ? "capture into it" : "re-ingest") +
                          " and rebuild the graph";
        }
    }
    plan.register_entry = false;
    plan.enable_graph = false;
    return plan;
}

CollectionRegistration register_collection(const CollectionWrite& write,
                                           const CollectionWritePlan& plan,
                                           std::chrono::milliseconds lock_wait) {
    CollectionRegistration done;
    const std::string config_path = write.config_path.string();
    if (plan.register_entry) {
        try {
            harness::edit_config_file(
                write.config_path,
                [&](std::string_view content) {
                    return harness::append_embedding(content, write.name, write.entry, false);
                },
                lock_wait);
            done.registered = true;
        } catch (const std::exception& e) {
            done.error = "could not register " + harness::quoted_collection_name(write.name) +
                         " in " + config_path + " -- " + e.what() + "; its store, " +
                         harness::quoted_collection_name(collection_path(write.name).string()) +
                         ", is kept without an entry, so it keeps no chunking of its own until "
                         "a later write registers it";
            return done;
        }
    }
    if (plan.enable_graph) {
        try {
            harness::edit_config_file(
                write.config_path,
                [&](std::string_view content) {
                    return harness::set_embedding_graph_enabled(content, write.name, true);
                },
                lock_wait);
            done.enabled = true;
        } catch (const std::exception& e) {
            done.error = "could not set graph.enabled on " +
                         harness::quoted_collection_name(write.name) + " in " + config_path +
                         " -- " + e.what() + "; the graph is built in " +
                         harness::quoted_collection_name(collection_path(write.name).string()) +
                         ", and retrieval expands through it once that is set";
        }
    }
    return done;
}

}  // namespace apogee::commands
