#include "commands/models.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <filesystem>
#include <iostream>
#include <map>
#include <sstream>

#include "commands/json_reporter.h"
#include "commands/models_pull.h"
#include "harness/layout.h"
#include "harness/paths.h"
#include "harness/roles.h"
#include "models/sidecar.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee models: " << message << "\n";
    throw CLI::RuntimeError(1);
}

/// Whether this backend type names a local file Apogee can inspect.
[[nodiscard]] bool is_local(harness::BackendType type) noexcept {
    return type == harness::BackendType::LlamaCpp;
}

/// The roles pointing at `key`, as "chat, embedding".
[[nodiscard]] std::string roles_for(const harness::Config& config, const std::string& key) {
    std::string out;
    const auto note = [&out](std::string_view label) {
        if (!out.empty()) {
            out += ", ";
        }
        out += label;
    };
    // Asked through the resolver rather than compared against the raw config
    // values, so this column cannot disagree with what a run would actually do
    // -- which is the entire point of there being one resolver.
    for (const auto [role, label] : {std::pair{harness::ModelRole::Chat, "chat"},
                                     std::pair{harness::ModelRole::Embedding, "embedding"},
                                     std::pair{harness::ModelRole::Extraction, "extraction"}}) {
        if (harness::resolve_backend_key(config, harness::RoleRequest{.role = role}) == key) {
            note(label);
        }
    }
    return out;
}

/// Column widths for an aligned table.
[[nodiscard]] std::size_t width_of(const std::vector<ModelRow>& rows, std::string_view header,
                                   const std::function<const std::string&(const ModelRow&)>& get) {
    std::size_t width = header.size();
    for (const ModelRow& row : rows) {
        width = std::max(width, get(row).size());
    }
    return width;
}

void pad(std::ostringstream& out, const std::string& value, std::size_t width, bool last) {
    out << value;
    if (!last) {
        out << std::string(width - value.size() + 2, ' ');
    }
}

/// What a model's sidecar says was checked when it was acquired.
[[nodiscard]] std::string describe_record(const std::filesystem::path& model) {
    const std::optional<models::Sidecar> sidecar = models::load_sidecar(model);
    // "no record" rather than "unverified": a model placed by hand is
    // legitimate, it simply has nothing to be rechecked against.
    return sidecar.has_value() ? sidecar->verification.summary() : "no record";
}

}  // namespace

std::vector<ModelRow> build_model_rows(const harness::Config& config,
                                       const std::filesystem::path& models_dir) {
    std::vector<ModelRow> rows;
    rows.reserve(config.backends.size());

    for (const auto& [key, backend] : config.backends) {
        ModelRow row;
        row.backend = key;
        row.type = std::string{harness::to_string(backend.type)};
        row.roles = roles_for(config, key);
        // Until the model-profiles item lands nothing resolves a profile, and
        // saying so is the honest answer rather than a placeholder: the
        // permissive-unknown rule means an unprofiled model is handled, not
        // broken.
        row.profile = "unprofiled";

        if (!is_local(backend.type)) {
            row.model = backend.model;
            row.provenance = "-";
            row.architecture = "-";
            row.state = "-";
            row.verified = "-";
            rows.push_back(std::move(row));
            continue;
        }

        const std::string expanded = harness::expand_env(backend.model_path);
        row.provenance = "local";
        row.architecture = "-";
        if (expanded.empty()) {
            row.model = backend.model;
            row.state = "missing";
            row.note = "no model_path set";
            rows.push_back(std::move(row));
            continue;
        }

        const std::filesystem::path path{expanded};
        row.model = path.filename().string();
        row.verified = describe_record(path);

        const models::GgufInfo info = models::inspect_gguf(path);
        if (!info.parsed) {
            row.state = std::filesystem::exists(path) ? "unreadable" : "missing";
            row.note = info.parse_error;
        } else {
            row.state = "ok";
            row.architecture = info.architecture.empty() ? "(absent)" : info.architecture;
            if (info.is_projector()) {
                row.note = "a multimodal projector (" + std::to_string(info.tensors) +
                           " vision tensors) -- point a backend's mmproj_path at this, not "
                           "model_path";
            } else if (info.has_vision_tensors()) {
                row.note = "combined text+vision blob (" +
                           std::to_string(info.tensors - info.text_tensors) + " vision tensors)";
            }
        }
        rows.push_back(std::move(row));
    }

    // The second half: models on disk that no backend points at -- everything
    // `models pull` has ever fetched, until the user wires it up.
    if (!models_dir.empty()) {
        std::error_code code;
        for (const auto& entry : std::filesystem::directory_iterator(models_dir, code)) {
            if (code) {
                break;
            }
            if (!entry.is_regular_file(code) || entry.path().extension() != ".gguf") {
                continue;
            }
            const bool already_listed = std::ranges::any_of(rows, [&](const ModelRow& row) {
                return row.model == entry.path().filename().string();
            });
            if (already_listed) {
                continue;
            }

            ModelRow row;
            // Not a backend: it is a file waiting to be pointed at.
            row.backend = "(not configured)";
            row.type = "-";
            row.model = entry.path().filename().string();
            row.provenance = "local";
            row.profile = "unprofiled";
            row.verified = describe_record(entry.path());

            const models::GgufInfo info = models::inspect_gguf(entry.path());
            row.state = info.parsed ? "ok" : "unreadable";
            row.architecture = info.parsed && !info.architecture.empty() ? info.architecture : "-";
            if (!info.parsed) {
                row.note = info.parse_error;
            } else if (info.is_projector()) {
                row.note = "a multimodal projector (" + std::to_string(info.tensors) +
                           " vision tensors) -- point a backend's mmproj_path at this, not "
                           "model_path";
            } else if (info.has_vision_tensors()) {
                row.note = "combined text+vision blob (" +
                           std::to_string(info.tensors - info.text_tensors) + " vision tensors)";
            }
            rows.push_back(std::move(row));
        }
    }

    std::ranges::sort(rows,
                      [](const ModelRow& a, const ModelRow& b) { return a.backend < b.backend; });
    return rows;
}

std::string render_model_table(const std::vector<ModelRow>& rows) {
    if (rows.empty()) {
        return "no backends configured -- run 'apogee config init' to write a starter config\n";
    }

    using Get = std::function<const std::string&(const ModelRow&)>;
    const std::vector<std::pair<std::string, Get>> columns{
        {"BACKEND", [](const ModelRow& r) -> const std::string& { return r.backend; }},
        {"TYPE", [](const ModelRow& r) -> const std::string& { return r.type; }},
        {"MODEL", [](const ModelRow& r) -> const std::string& { return r.model; }},
        {"ROLES", [](const ModelRow& r) -> const std::string& { return r.roles; }},
        {"SOURCE", [](const ModelRow& r) -> const std::string& { return r.provenance; }},
        {"ARCH", [](const ModelRow& r) -> const std::string& { return r.architecture; }},
        {"PROFILE", [](const ModelRow& r) -> const std::string& { return r.profile; }},
        {"STATE", [](const ModelRow& r) -> const std::string& { return r.state; }},
        {"VERIFIED", [](const ModelRow& r) -> const std::string& { return r.verified; }},
    };

    std::vector<std::size_t> widths;
    widths.reserve(columns.size());
    for (const auto& [header, get] : columns) {
        widths.push_back(width_of(rows, header, get));
    }

    std::ostringstream out;
    for (std::size_t i = 0; i < columns.size(); ++i) {
        pad(out, columns[i].first, widths[i], i + 1 == columns.size());
    }
    out << "\n";

    for (const ModelRow& row : rows) {
        for (std::size_t i = 0; i < columns.size(); ++i) {
            pad(out, columns[i].second(row), widths[i], i + 1 == columns.size());
        }
        out << "\n";
        if (!row.note.empty()) {
            out << "    " << row.note << "\n";
        }
    }
    return out.str();
}

std::string render_model_jsonl(const std::vector<ModelRow>& rows) {
    std::ostringstream out;
    for (const ModelRow& row : rows) {
        nlohmann::json object;
        object["type"] = "model";
        object["backend"] = row.backend;
        object["backend_type"] = row.type;
        object["model"] = row.model;
        object["roles"] = row.roles;
        object["source"] = row.provenance;
        object["architecture"] = row.architecture;
        object["profile"] = row.profile;
        object["state"] = row.state;
        object["verified"] = row.verified;
        if (!row.note.empty()) {
            object["note"] = row.note;
        }
        out << object.dump() << "\n";
    }
    return out.str();
}

std::string render_model_info(const harness::Config& config, std::string_view backend) {
    const auto entry = config.backends.find(std::string{backend});
    if (entry == config.backends.end()) {
        return {};
    }
    const harness::BackendConfig& value = entry->second;

    std::ostringstream out;
    out << "backend:      " << backend << "\n";
    out << "type:         " << harness::to_string(value.type) << "\n";
    if (!value.model.empty()) {
        out << "model:        " << value.model << "\n";
    }
    const std::string roles = roles_for(config, std::string{backend});
    out << "roles:        " << (roles.empty() ? "-" : roles) << "\n";

    if (!is_local(value.type)) {
        // A cloud backend has no file to inspect, and saying so beats printing
        // empty GGUF fields that read like a failed read.
        out << "source:       remote (no local file to inspect)\n";
        return out.str();
    }

    const std::string expanded = harness::expand_env(value.model_path);
    if (expanded.empty()) {
        out << "model_path:   (unset)\n";
        out << "header:       skipped -- no model_path to read\n";
        return out.str();
    }

    out << "model_path:   " << expanded << "\n";
    const models::GgufInfo info = models::inspect_gguf(std::filesystem::path{expanded});
    if (!info.parsed) {
        // The reason, always. An unreadable header rendering as blank fields is
        // the exact failure this surface exists to prevent.
        out << "header:       FAILED -- " << info.parse_error << "\n";
        out << "repair:       apogee models repair " << backend
            << "   (once model-acquisition lands; until then re-download the file)\n";
        return out.str();
    }

    out << "header:       ok (GGUF v" << info.version << ")\n";
    out << "architecture: " << (info.architecture.empty() ? "(absent)" : info.architecture) << "\n";
    // Stated separately from the architecture above, and stated at all rather
    // than omitted: a user comparing two models needs to know Apogee has no
    // characterized behaviour for either yet.
    out << "profile:      unprofiled (no profile registry yet -- model-profiles)\n";
    if (!info.name.empty()) {
        out << "name:         " << info.name << "\n";
    }
    out << "tensors:      " << info.tensors << " total, " << info.text_tensors << " text\n";
    out << "size:         " << (info.file_size / (1024LL * 1024)) << " MiB\n";
    if (info.is_projector()) {
        out << "note:         a multimodal projector -- belongs on mmproj_path, not model_path\n";
    } else if (info.has_vision_tensors()) {
        out << "note:         combined text+vision blob -- " << (info.tensors - info.text_tensors)
            << " vision/projector tensors\n";
    }
    return out.str();
}

std::string render_role_status(const harness::Config& config) {
    std::ostringstream out;
    for (const auto [role, label] : {std::pair{harness::ModelRole::Chat, "chat"},
                                     std::pair{harness::ModelRole::Embedding, "embedding"},
                                     std::pair{harness::ModelRole::Extraction, "extraction"}}) {
        const harness::Resolution resolved =
            harness::resolve_backend(config, harness::RoleRequest{.role = role});
        const std::string& key = resolved.key;

        out << label << ": ";
        if (key.empty()) {
            out << "(unset -- no models.default configured)\n";
            continue;
        }
        out << key;

        // Which rung answered, straight from the resolver. Working it out here
        // would mean re-reading the chain, and a second reading is a second
        // chain -- which cli.one_role_resolver would (correctly) reject.
        if (resolved.from == harness::ResolvedFrom::Default && role != harness::ModelRole::Chat) {
            out << "   (via models.default)";
        }

        // Resolving and validating are separate on purpose: the resolver
        // returns a key, and each surface decides what an unconfigured one
        // means. Here it is a note; on a run path it is fatal.
        if (!config.backends.contains(key)) {
            out << "   [not configured]";
        }
        out << "\n";
    }
    return out.str();
}

std::string_view ModelsCommand::name() const noexcept {
    return "models";
}

std::string_view ModelsCommand::summary() const noexcept {
    return "List configured models, inspect one, and show role assignments";
}

void ModelsCommand::bind(CLI::App& root, const RootContext& context) {
    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->require_subcommand(1);

    const auto load = [&context]() {
        try {
            return harness::load_config(harness::resolve_config_path(context.config_path));
        } catch (const std::exception& e) {
            fail(e.what());
        }
    };

    auto format = std::make_shared<std::string>();
    CLI::App* list = cmd->add_subcommand("list", "List configured backends and their models");
    list->add_option("--output-format", *format, "text (default) or stream-json")
        ->check(CLI::IsMember({"text", "stream-json"}));
    list->callback([load, format]() {
        const std::vector<ModelRow> rows = build_model_rows(load(), harness::models_dir());
        std::cout << (*format == "stream-json" ? render_model_jsonl(rows)
                                               : render_model_table(rows));
    });

    auto info_name = std::make_shared<std::string>();
    CLI::App* info = cmd->add_subcommand("info", "Show one backend's model in detail");
    info->add_option("backend", *info_name, "Backend key from the config")->required();
    info->callback([load, info_name]() {
        const harness::Config config = load();
        const std::string body = render_model_info(config, *info_name);
        if (body.empty()) {
            fail("no backend named '" + *info_name + "'");
        }
        std::cout << body;
    });

    CLI::App* status = cmd->add_subcommand("status", "Show which backend each role resolves to");
    status->callback([load]() { std::cout << render_role_status(load()); });

    // The mutating verbs live in their own translation unit, so "what can this
    // command destroy?" has a short answer.
    bind_model_mutations(*cmd, harness::models_dir());
}

}  // namespace apogee::commands
