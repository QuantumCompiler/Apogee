#include "operations/backend_fields.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <sstream>

#include "backends/adapters.h"
#include "backends/mlx_local.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/layout.h"
#include "modelstore/mlx_info.h"
#include "modelstore/store.h"
#include "operations/renames.h"

namespace apogee::commands {
namespace {

/// The adapters `edits` give backend `name`, pinned to its base as the same
/// edit leaves it (53a): the edit tried on the file's text unpinned, so the
/// loader's own rules refuse a type that applies none, a second adapter on
/// an mlx entry or a scale out of bounds before any weights are read; then
/// the entry it leaves names the base, and `pin_adapters` resolves, checks
/// and pins every adapter. `said` names the base and its hash. The refusal,
/// or empty.
[[nodiscard]] std::string pin_edited_adapters(const std::filesystem::path& path,
                                              std::string_view name,
                                              const std::vector<BackendFieldEdit>& edits,
                                              std::vector<harness::AdapterConfig>& pinned,
                                              std::string& said) {
    try {
        const std::ifstream in{path, std::ios::binary};
        std::ostringstream content;
        content << in.rdbuf();
        std::string trial = content.str();
        for (const BackendFieldEdit& edit : edits) {
            trial = harness::set_backend_field(trial, name, edit.field, edit.value);
        }
        const harness::Config edited = harness::parse_config(trial, "<edited config>");
        const harness::BackendConfig* entry = edited.find_backend(name);
        if (entry == nullptr) {
            return "no backend named '" + std::string{name} + "'";
        }
        if (entry->model_path.empty()) {
            return "adapters apply over the entry's model_path, and it names none -- set it "
                   "with --model-path";
        }
        pinned = entry->adapters;
        const std::filesystem::path base{harness::expand_env_and_home(entry->model_path)};
        const backends::AdapterCheck check{.backend = std::string{name},
                                           .base = base,
                                           .directories = entry->type == harness::BackendType::Mlx,
                                           .hashing = {.memo = harness::weight_hash_cache_path()}};
        if (std::string refused = backends::pin_adapters(pinned, check); !refused.empty()) {
            return refused;
        }
        said = base.string() + " (sha256 " + pinned.front().base_sha256.substr(0, 12) + ")";
        return {};
    } catch (const std::exception& e) {
        return e.what();
    }
}

/// `text` as one word of a command a person copies: double-quoted when a
/// shell would split it, as `spell_adapters` quotes a set.
[[nodiscard]] std::string shell_word(const std::string& text) {
    return text.empty() || text.find_first_of(" '\"") != std::string::npos ? "\"" + text + "\""
                                                                           : text;
}

/// The adapters an entry keeps checked against the base `edits` move it
/// onto (55e) -- as the same edit re-pairs a drafter: the edits tried on the
/// file's text, kept nowhere, and every adapter the entry they leave applies
/// asked its file facts and its pin against that base's weights, which
/// `pin_adapters` reads once. Where every pin is that base's hash, the pins
/// hold as written and `said` names the base; otherwise the edit is refused
/// naming each adapter whose pin is not, with the two edits that carry the
/// move through: the same one re-pinning the set over the new weights, or
/// taking it off. The refusal, or empty.
[[nodiscard]] std::string repin_kept_adapters(const std::filesystem::path& path,
                                              std::string_view name,
                                              const std::vector<BackendFieldEdit>& edits,
                                              std::string& said) {
    try {
        const std::ifstream in{path, std::ios::binary};
        std::ostringstream content;
        content << in.rdbuf();
        std::string trial = content.str();
        std::string command = "apogee config set-backend " + std::string{name};
        for (const BackendFieldEdit& edit : edits) {
            trial = harness::set_backend_field(trial, name, edit.field, edit.value);
            std::string flag = edit.field;
            std::ranges::replace(flag, '_', '-');
            command += " --" + flag + " " + shell_word(edit.value);
        }
        const harness::Config edited = harness::parse_config(trial, "<edited config>");
        const harness::BackendConfig* entry = edited.find_backend(name);
        if (entry == nullptr || entry->adapters.empty()) {
            return {};
        }
        const std::string take_off = command + " --adapters \"\"";
        if (entry->model_path.empty()) {
            return "'" + std::string{name} +
                   "' applies adapters over its model_path, and this edit leaves it none -- "
                   "nothing was edited; take them off in the same edit: " +
                   take_off;
        }
        const std::filesystem::path base{harness::expand_env_and_home(entry->model_path)};
        std::vector<harness::AdapterConfig> repinned = entry->adapters;
        const backends::AdapterCheck check{.backend = std::string{name},
                                           .base = base,
                                           .directories = entry->type == harness::BackendType::Mlx,
                                           .hashing = {.memo = harness::weight_hash_cache_path()}};
        if (std::string refused = backends::pin_adapters(repinned, check); !refused.empty()) {
            return refused;
        }
        const std::string now = repinned.front().base_sha256.substr(0, 12);
        std::string stale;
        for (const harness::AdapterConfig& adapter : entry->adapters) {
            if (adapter.base_sha256 == repinned.front().base_sha256) {
                continue;
            }
            stale += (stale.empty() ? "" : ", ") + adapter.path +
                     (adapter.base_sha256.empty()
                          ? std::string{" (pinned to no base)"}
                          : " (sha256 " + adapter.base_sha256.substr(0, 12) + ")");
        }
        if (!stale.empty()) {
            return "'" + std::string{name} + "' applies adapters pinned to other weights than " +
                   base.string() + " holds (sha256 " + now + "): " + stale +
                   " -- an adapter applies only over the weights it was trained on, so nothing "
                   "was edited; if these are the weights they were made for, re-pin them in the "
                   "same edit: " +
                   command + " --adapters " + backends::spell_adapters(entry->adapters) +
                   ", or take them off: " + take_off;
        }
        said = base.string() + " (sha256 " + now + ")";
        return {};
    } catch (const std::exception& e) {
        return e.what();
    }
}

/// The drafter `value` names (53i): one stored set by its handle
/// (`<model>/mlx/<id>`), a model whose MLX builds hold exactly one drafter,
/// or a path -- made absolute, as the entry will read it from anywhere.
/// The directory, or the refusal in `refusal`.
[[nodiscard]] std::string resolve_drafter(const harness::Config& config, const std::string& value,
                                          std::string& refusal) {
    const models::StoreRoots roots = store_roots_of(config);
    const models::StoreTarget target = models::resolve_store_target(roots, value);
    if (target.error.empty() && !target.outside) {
        if (!target.id.empty() && target.format == models::kMlxFormat) {
            return target.path.string();
        }
        if (!target.format.empty() && target.format != models::kMlxFormat) {
            refusal = "'" + value + "' is " + target.format +
                      " weights, and a drafter is an MLX directory -- 'apogee models convert "
                      "<a draft head> --mlx' makes one";
            return {};
        }
        // A model: its one drafter.
        std::vector<std::string> drafters;
        for (const models::StoredMlx& stored : models::list_store_mlx(roots, target.model)) {
            if (models::read_mlx_drafter(stored.dir).has_value()) {
                drafters.push_back(
                    models::weights_handle(stored.model, models::kMlxFormat, stored.id));
            }
        }
        if (drafters.size() == 1) {
            return models::resolve_store_target(roots, drafters.front()).path.string();
        }
        refusal = drafters.empty() ? "'" + value +
                                         "' stores no MLX drafter -- 'apogee models convert "
                                         "<its draft head> --mlx' makes one"
                                   : "'" + value + "' stores " + std::to_string(drafters.size()) +
                                         " drafters -- name one:";
        for (const std::string& handle : drafters) {
            refusal += " " + handle;
        }
        return {};
    }
    std::error_code code;
    const std::filesystem::path path{harness::expand_env_and_home(value)};
    return std::filesystem::absolute(path, code).lexically_normal().string();
}

/// The drafter `edits` leave backend `name` with, paired with the model the
/// same edit leaves it over (53i): the drafter's value resolved
/// (`resolve_drafter`) and written back into its edit, the edit tried on the
/// file's text so the loader refuses a draft on a type that runs none, then
/// `backends::probe_mlx_draft` -- the ladder's own question -- asked of the
/// entry it leaves. `said` names the pair. The refusal, or empty.
[[nodiscard]] std::string pair_edited_draft(const std::filesystem::path& path,
                                            std::string_view name,
                                            std::vector<BackendFieldEdit>& edits,
                                            std::string& said) {
    try {
        const std::ifstream in{path, std::ios::binary};
        std::ostringstream content;
        content << in.rdbuf();
        const harness::Config config = harness::parse_config(content.str(), path.string());
        for (BackendFieldEdit& edit : edits) {
            if (edit.field == "draft" && !edit.value.empty()) {
                std::string refusal;
                edit.value = resolve_drafter(config, edit.value, refusal);
                if (!refusal.empty()) {
                    return refusal;
                }
            }
        }
        std::string trial = content.str();
        for (const BackendFieldEdit& edit : edits) {
            trial = harness::set_backend_field(trial, name, edit.field, edit.value);
        }
        const harness::Config edited = harness::parse_config(trial, "<edited config>");
        const harness::BackendConfig* entry = edited.find_backend(name);
        if (entry == nullptr || entry->draft.empty()) {
            return {};
        }
        if (entry->model_path.empty()) {
            return "a drafter runs beside the entry's model_path, and it names none -- set it "
                   "with --model-path";
        }
        const std::filesystem::path base{harness::expand_env_and_home(entry->model_path)};
        const backends::MlxDraft draft =
            backends::probe_mlx_draft(base, entry->draft, backends::MlxHost::current());
        if (!draft.pairs()) {
            return draft.message();
        }
        said = draft.dir.string() + " beside " + base.string();
        return {};
    } catch (const std::exception& e) {
        return e.what();
    }
}

/// Why the entry `edits` leave backend `name` with may not run what it names
/// (55d): the edits tried on the file's text, kept nowhere, and the entry
/// they leave asked `kind_refusal`. Empty when it may, or when the edit would
/// not load -- the edit's own re-parse says why then.
[[nodiscard]] std::string edited_kind_refusal(const std::filesystem::path& path,
                                              std::string_view name,
                                              const std::vector<BackendFieldEdit>& edits) {
    try {
        const std::ifstream in{path, std::ios::binary};
        std::ostringstream content;
        content << in.rdbuf();
        std::string trial = content.str();
        for (const BackendFieldEdit& edit : edits) {
            if (edit.field == "type" || edit.field == "model_path") {
                trial = harness::set_backend_field(trial, name, edit.field, edit.value);
            }
        }
        const harness::Config edited = harness::parse_config(trial, "<edited config>");
        const harness::BackendConfig* entry = edited.find_backend(name);
        return entry == nullptr ? std::string{} : kind_refusal(name, *entry);
    } catch (const std::exception&) {
        return {};
    }
}

}  // namespace

std::string kind_refusal(std::string_view name, const harness::BackendConfig& backend) {
    if (backend.type != harness::BackendType::Mlx || backend.model_path.empty()) {
        return {};
    }
    const backends::MlxReadiness readiness = backends::probe_mlx_kind(
        name, std::filesystem::path{harness::expand_env_and_home(backend.model_path)});
    return readiness.message();
}

BackendFieldsOutcome set_backend_fields(const std::filesystem::path& path, std::string_view name,
                                        const std::vector<BackendFieldEdit>& requested) {
    BackendFieldsOutcome out;
    // Each edit as it will be written: a drafter's resolved below (53i).
    std::vector<BackendFieldEdit> edits = requested;
    if (edits.empty()) {
        out.refusal = "name a field to set -- 'apogee config set-backend --help' lists them";
        return out;
    }
    harness::Config config;
    try {
        config = harness::load_config(path);
    } catch (const std::exception& e) {
        out.refusal = e.what();
        return out;
    }
    if (config.find_backend(name) == nullptr) {
        out.refusal = "no backend named '" + std::string{name} + "'";
        return out;
    }
    // A new type is checked as `add-backend` checks one; the rest of a
    // value's shape is the loader's, on the edit's own re-parse.
    for (const BackendFieldEdit& edit : edits) {
        if (edit.field == "type" && !edit.value.empty() &&
            !harness::backend_type_from_string(edit.value).has_value()) {
            std::string accepted;
            for (const std::string_view type : harness::backend_type_names()) {
                accepted += (accepted.empty() ? "" : ", ") + std::string{type};
            }
            out.refusal = "type: unknown value '" + edit.value + "' (accepted: " + accepted + ")";
            return out;
        }
    }
    // The kind decides the role (55d): an edit that would leave an mlx entry
    // over a set no mlx entry runs is refused by that set's kind.
    if (std::ranges::any_of(edits, [](const BackendFieldEdit& edit) {
            return edit.field == "type" || edit.field == "model_path";
        })) {
        if (std::string refused = edited_kind_refusal(path, name, edits); !refused.empty()) {
            out.refusal = std::move(refused);
            return out;
        }
    }
    // An adapter set is pinned to the base it is registered over (53a).
    std::vector<harness::AdapterConfig> pinned;
    std::string pinned_to;
    const auto adapters = std::ranges::find_if(edits, [](const BackendFieldEdit& edit) {
        return edit.field == "adapters" && !edit.value.empty();
    });
    const BackendFieldEdit* adapters_edit = adapters == edits.end() ? nullptr : &*adapters;
    if (adapters_edit != nullptr) {
        if (std::string refused = pin_edited_adapters(path, name, edits, pinned, pinned_to);
            !refused.empty()) {
            out.refusal = std::move(refused);
            return out;
        }
    }
    // The pins of the adapters the entry keeps, checked against the base an
    // edit moves it onto (55e), as a drafter is re-paired below: a model_path
    // or type moved under them, with no new set and no clearing in the edit.
    std::string kept_pinned_to;
    const harness::BackendConfig* current = config.find_backend(name);
    const bool sets_adapters = std::ranges::any_of(
        edits, [](const BackendFieldEdit& edit) { return edit.field == "adapters"; });
    const bool moves_base = std::ranges::any_of(edits, [](const BackendFieldEdit& edit) {
        return edit.field == "model_path" || edit.field == "type";
    });
    if (!current->adapters.empty() && moves_base && !sets_adapters) {
        if (std::string refused = repin_kept_adapters(path, name, edits, kept_pinned_to);
            !refused.empty()) {
            out.refusal = std::move(refused);
            return out;
        }
    }
    // A drafter is paired with the model it runs beside before it is written
    // (53i) -- and again when an edit moves that model, or its type, under a
    // drafter the entry keeps.
    std::string drafted;
    const bool drafts = std::ranges::any_of(edits, [](const BackendFieldEdit& edit) {
        return edit.field == "draft" && !edit.value.empty();
    });
    const bool moves = !current->draft.empty() && moves_base;
    if (drafts || moves) {
        if (std::string refused = pair_edited_draft(path, name, edits, drafted); !refused.empty()) {
            out.refusal = std::move(refused);
            return out;
        }
    }
    try {
        harness::edit_config_file(path, [&](std::string_view content) {
            std::string text{content};
            for (const BackendFieldEdit& edit : edits) {
                text = &edit == adapters_edit
                           ? harness::set_backend_adapters(text, name, pinned)
                           : harness::set_backend_field(text, name, edit.field, edit.value);
            }
            return text;
        });
    } catch (const std::exception& e) {
        out.refusal = e.what();
        return out;
    }
    std::string set;
    std::string cleared;
    for (const BackendFieldEdit& edit : edits) {
        std::string& list = edit.value.empty() ? cleared : set;
        list += (list.empty() ? "" : ", ") + edit.field;
    }
    out.said = "edited backend '" + std::string{name} + "' in " + path.string() + ":" +
               (set.empty() ? "" : " set " + set) + (set.empty() || cleared.empty() ? "" : ";") +
               (cleared.empty() ? "" : " cleared " + cleared) +
               (pinned_to.empty() ? "" : "; adapters pinned to " + pinned_to) +
               (kept_pinned_to.empty() ? "" : "; adapters still pinned to " + kept_pinned_to) +
               (drafted.empty() ? "" : "; drafter " + drafted);
    return out;
}

}  // namespace apogee::commands
