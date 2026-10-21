#include "operations/backend_fields.h"

#include <algorithm>
#include <exception>
#include <fstream>
#include <sstream>

#include "backends/adapters.h"
#include "contracts/config.h"
#include "contracts/config_edit.h"
#include "contracts/layout.h"

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

}  // namespace

BackendFieldsOutcome set_backend_fields(const std::filesystem::path& path, std::string_view name,
                                        const std::vector<BackendFieldEdit>& edits) {
    BackendFieldsOutcome out;
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
               (pinned_to.empty() ? "" : "; adapters pinned to " + pinned_to);
    return out;
}

}  // namespace apogee::commands
