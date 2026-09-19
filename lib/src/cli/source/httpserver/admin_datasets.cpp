#include "httpserver/admin_datasets.h"

#include <nlohmann/json.hpp>

#include <exception>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

#include "commands/datasets.h"
#include "harness/config.h"
#include "harness/layout.h"
#include "httpserver/handler.h"
#include "training/datasets.h"
#include "training/kit.h"
#include "training/synth.h"

namespace apogee::httpserver {
namespace {

constexpr std::string_view kSynthJobKind = "datasets-synth";
constexpr std::string_view kConfigError = "config_error";
constexpr int kNotImplemented = 501;

[[nodiscard]] bool plain_name(std::string_view name) {
    return training::valid_dataset_name(name);
}

[[nodiscard]] std::optional<harness::Config> load_now(const AdminConfigContext& context,
                                                      HttpResponse& failure) {
    try {
        return harness::load_config(context.config_path);
    } catch (const harness::ConfigError& e) {
        failure = error_response(500, std::string{"the config cannot be read: "} + e.what(),
                                 kConfigError);
        return std::nullopt;
    }
}

[[nodiscard]] std::optional<nlohmann::json> parse_body(const HttpRequest& request,
                                                       HttpResponse& failure) {
    if (request.body.empty()) {
        return nlohmann::json::object();
    }
    const nlohmann::json body = nlohmann::json::parse(request.body, nullptr, false);
    if (body.is_discarded() || !body.is_object()) {
        failure = error_response(400, "the request body must be a JSON object");
        return std::nullopt;
    }
    return body;
}

/// A string field, or the empty string; `failure` on a wrong type.
[[nodiscard]] bool string_field(const nlohmann::json& body, const char* key, std::string& out,
                                HttpResponse& failure) {
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) {
        return true;
    }
    if (!it->is_string()) {
        failure = error_response(400, std::string{key} + " must be a string");
        return false;
    }
    out = it->get<std::string>();
    return true;
}

[[nodiscard]] bool bool_field(const nlohmann::json& body, const char* key, bool& out,
                              HttpResponse& failure) {
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) {
        return true;
    }
    if (!it->is_boolean()) {
        failure = error_response(400, std::string{key} + " must be a boolean");
        return false;
    }
    out = it->get<bool>();
    return true;
}

[[nodiscard]] bool int_field(const nlohmann::json& body, const char* key, int& out,
                             HttpResponse& failure) {
    const auto it = body.find(key);
    if (it == body.end() || it->is_null()) {
        return true;
    }
    if (!it->is_number_integer() || it->get<int>() < 0) {
        failure = error_response(400, std::string{key} + " must be a non-negative integer");
        return false;
    }
    out = it->get<int>();
    return true;
}

[[nodiscard]] training::DatasetStore store() {
    return training::DatasetStore{harness::training_datasets_dir()};
}

}  // namespace

HttpResponse admin_list_datasets(const AdminConfigContext& /*context*/) {
    nlohmann::json data = nlohmann::json::array();
    for (const training::DatasetInfo& info : store().list()) {
        data.push_back(commands::dataset_json(info));
    }
    return json_response(200, nlohmann::json{{"object", "list"}, {"data", data}});
}

HttpResponse admin_create_dataset(const AdminConfigContext& /*context*/,
                                  const HttpRequest& request) {
    HttpResponse failure;
    const std::optional<nlohmann::json> body = parse_body(request, failure);
    if (!body.has_value()) {
        return failure;
    }
    commands::DatasetCreateRequest create;
    std::string from = "template";
    if (!string_field(*body, "name", create.name, failure) ||
        !string_field(*body, "from", from, failure) ||
        !string_field(*body, "backend", create.filter.backend, failure) ||
        !string_field(*body, "since", create.filter.since, failure) ||
        !string_field(*body, "until", create.filter.until, failure) ||
        !bool_field(*body, "force", create.force, failure)) {
        return failure;
    }
    if (create.name.empty()) {
        return error_response(400, "name is required");
    }
    if (!plain_name(create.name)) {
        return error_response(400, "'" + create.name + "' is not a dataset name");
    }
    const std::optional<training::CreateSource> source = training::create_source_from_string(from);
    if (!source.has_value()) {
        return error_response(400, "from must be template, sessions or empty");
    }
    create.source = *source;
    if (const auto lines = body->find("lines"); lines != body->end() && !lines->is_null()) {
        if (!lines->is_array()) {
            return error_response(400, "lines must be an array of JSON objects");
        }
        for (const nlohmann::json& line : *lines) {
            if (!line.is_object()) {
                return error_response(400, "lines must be an array of JSON objects");
            }
            create.lines.push_back(line.dump());
        }
    }
    commands::DatasetCreateResult result;
    try {
        result = commands::create_dataset(store(), create);
    } catch (const std::runtime_error& e) {
        const std::string message = e.what();
        return error_response(message.find("already exists") != std::string::npos ? 409 : 400,
                              message);
    }
    nlohmann::json out{
        {"name", create.name}, {"path", result.path.string()}, {"examples", result.examples}};
    if (*source == training::CreateSource::Sessions) {
        out["sessions"] = result.sessions;
        out["skipped"] = result.skipped;
    }
    return json_response(201, out);
}

HttpResponse admin_get_dataset(const AdminConfigContext& /*context*/, std::string_view name) {
    if (!plain_name(name)) {
        return error_response(400, "'" + std::string{name} + "' is not a dataset name");
    }
    const std::optional<training::DatasetInfo> info = store().info(name);
    if (!info.has_value()) {
        return error_response(404, "no dataset named '" + std::string{name} + "'");
    }
    return json_response(200, commands::dataset_json(*info));
}

HttpResponse admin_delete_dataset(const AdminConfigContext& /*context*/, std::string_view name) {
    if (!plain_name(name)) {
        return error_response(400, "'" + std::string{name} + "' is not a dataset name");
    }
    const training::DatasetStore datasets = store();
    if (!datasets.exists(name)) {
        return error_response(404, "no dataset named '" + std::string{name} + "'");
    }
    if (const std::string error = datasets.remove(name); !error.empty()) {
        return error_response(500, error);
    }
    return json_response(200, nlohmann::json{{"deleted", std::string{name}}});
}

HttpResponse admin_synth_dataset(const AdminConfigContext& context, Handler& plane,
                                 JobRegistry& jobs, JobWorkers& workers,
                                 const HttpRequest& request) {
    HttpResponse failure;
    const std::optional<nlohmann::json> body = parse_body(request, failure);
    if (!body.has_value()) {
        return failure;
    }
    std::string name;
    std::string teacher;
    std::string kit_name;
    std::string topic;
    int count = 0;
    int max_tokens = 4096;
    int parallel = 4;
    bool force = false;
    double temperature = 0.0;
    if (!string_field(*body, "name", name, failure) ||
        !string_field(*body, "teacher", teacher, failure) ||
        !string_field(*body, "kit", kit_name, failure) ||
        !string_field(*body, "topic", topic, failure) ||
        !int_field(*body, "count", count, failure) ||
        !int_field(*body, "max_tokens", max_tokens, failure) ||
        !int_field(*body, "parallel", parallel, failure) ||
        !bool_field(*body, "force", force, failure)) {
        return failure;
    }
    if (const auto it = body->find("temperature"); it != body->end() && !it->is_null()) {
        if (!it->is_number()) {
            return error_response(400, "temperature must be a number");
        }
        temperature = it->get<double>();
    }
    if (name.empty() || teacher.empty() || kit_name.empty()) {
        return error_response(400, "name, teacher and kit are required");
    }
    if (!plain_name(name)) {
        return error_response(400, "'" + name + "' is not a dataset name");
    }
    if (plane.options().served.empty()) {
        return error_response(kNotImplemented,
                              "this server has no generation backend to run the teacher on",
                              kBackendUnavailable);
    }
    const std::optional<harness::Config> config = load_now(context, failure);
    if (!config.has_value()) {
        return failure;
    }
    const training::DatasetStore datasets = store();
    if (!force && datasets.exists(name)) {
        return error_response(409, "dataset already exists: " + datasets.path_for(name).string() +
                                       " (pass force to overwrite it)");
    }
    const std::optional<std::filesystem::path> kit_path =
        training::find_kit(harness::training_kits_dir(), kit_name);
    if (!kit_path.has_value()) {
        return error_response(400, "no kit named '" + kit_name + "'");
    }
    training::Kit kit;
    try {
        kit = training::load_kit(*kit_path);
    } catch (const std::exception& e) {
        return error_response(400, e.what());
    }
    if (const std::string error = training::validate_kit(kit); !error.empty()) {
        return error_response(400, "kit '" + kit.name + "': " + error);
    }
    const commands::TeacherResolution resolved = commands::resolve_teacher(*config, teacher);
    if (resolved.key.empty()) {
        return error_response(400, resolved.error);
    }
    std::string backend;
    try {
        backend = plane.resolve_served(resolved.key);
    } catch (const HttpError& e) {
        return to_response(e);
    }

    training::SynthOptions options;
    options.count = count;
    options.topic = topic;
    options.temperature = temperature;
    options.max_tokens = max_tokens;
    options.parallel = commands::effective_parallel(resolved, parallel);

    const JobRegistry::Started started =
        jobs.start(std::string{kSynthJobKind},
                   nlohmann::json{{"name", name}, {"teacher", backend}, {"kit", kit.name}});
    const std::string job_id = started.id;
    options.cancellation = started.cancellation;
    options.on_progress = [&jobs, job_id](int produced, int target) {
        jobs.progress(job_id, "synthesising examples",
                      nlohmann::json{{"produced", produced}, {"target", target}});
    };
    workers.run(started.cancellation, [&plane, &jobs, job_id, name, backend, kit, options, force,
                                       datasets] {
        try {
            const training::SynthResult result = training::synthesize(
                kit, commands::make_teacher(plane.harness(), backend), options);
            if (result.cancelled) {
                return;
            }
            if (!result.error.empty()) {
                jobs.fail(job_id, result.error);
                return;
            }
            std::filesystem::path path;
            if (const std::string error =
                    datasets.write(name, training::example_lines(result.examples), force, &path);
                !error.empty()) {
                jobs.fail(job_id, error);
                return;
            }
            jobs.finish(job_id, nlohmann::json{{"name", name},
                                               {"path", path.string()},
                                               {"examples", result.examples.size()},
                                               {"calls", result.calls},
                                               {"failed_batches", result.failed_batches},
                                               {"retries", result.retries}});
        } catch (const std::exception& e) {
            jobs.fail(job_id, e.what());
        }
    });
    return json_response(202, nlohmann::json{{"job_id", job_id}});
}

HttpResponse admin_list_kits(const AdminConfigContext& /*context*/) {
    nlohmann::json data = nlohmann::json::array();
    for (const training::KitSummary& kit : training::list_kits(harness::training_kits_dir())) {
        data.push_back(commands::kit_json(kit));
    }
    return json_response(200, nlohmann::json{{"object", "list"}, {"data", data}});
}

}  // namespace apogee::httpserver
