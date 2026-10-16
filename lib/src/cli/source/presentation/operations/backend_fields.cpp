#include "operations/backend_fields.h"

#include <exception>

#include "contracts/config.h"
#include "contracts/config_edit.h"

namespace apogee::commands {

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
    try {
        harness::edit_config_file(path, [&](std::string_view content) {
            std::string text{content};
            for (const BackendFieldEdit& edit : edits) {
                text = harness::set_backend_field(text, name, edit.field, edit.value);
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
               (cleared.empty() ? "" : " cleared " + cleared);
    return out;
}

}  // namespace apogee::commands
