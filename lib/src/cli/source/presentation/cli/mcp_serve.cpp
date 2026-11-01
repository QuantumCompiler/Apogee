#include "cli/mcp_serve.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <filesystem>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

#include "agent/tool.h"
#include "cli/check.h"
#include "cli/helpers.h"
#include "cli/models.h"
#include "cli/suite_residency.h"
#include "cli/train.h"
#include "contracts/config.h"
#include "contracts/paths.h"
#include "machine/json_reporter.h"
#include "operations/training_reads.h"
#include "tools/toolsets.h"
#include "version/version.h"

namespace apogee::commands {
namespace {

constexpr std::array<std::string_view, 5> kStatusReads{
    kModelsListRead, kModelsStatusRead, kTrainStatusRead, kTrainVersionsRead, kCheckRead};

/// The name a host sees in `initialize`, unchanged since the server was
/// hidden: a host keys nothing on it, but nothing gains by moving it.
constexpr std::string_view kServerName = "apogee-tools";

/// An optional string argument: absent or null is empty, anything but a
/// string refused by name.
[[nodiscard]] std::string string_argument(const nlohmann::json& args, std::string_view key) {
    const auto found = args.find(std::string{key});
    if (found == args.end() || found->is_null()) {
        return {};
    }
    if (!found->is_string()) {
        throw std::invalid_argument(std::string{key} + " must be a string");
    }
    return found->get<std::string>();
}

/// An optional boolean argument, false when absent. A host's model may send
/// it as a string as readily as a boolean, so the usual spellings are read,
/// case aside; anything else is refused rather than guessed.
[[nodiscard]] bool flag_argument(const nlohmann::json& args, std::string_view key) {
    const auto found = args.find(std::string{key});
    if (found == args.end() || found->is_null()) {
        return false;
    }
    if (found->is_boolean()) {
        return found->get<bool>();
    }
    if (found->is_string()) {
        std::string text = found->get<std::string>();
        std::ranges::transform(text, text.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (text == "true" || text == "yes" || text == "1") {
            return true;
        }
        if (text == "false" || text == "no" || text == "0") {
            return false;
        }
    }
    throw std::invalid_argument(std::string{key} + " must be true or false");
}

[[nodiscard]] mcp::ToolCallResult answered(const nlohmann::json& document) {
    return mcp::ToolCallResult{document_text(document), false};
}

[[nodiscard]] mcp::ToolCallResult refused(std::string reason) {
    return mcp::ToolCallResult{"Error: " + std::move(reason), true};
}

/// The config the command reads, at the path it resolves -- a config that
/// will not load throws with the command's own reason.
[[nodiscard]] harness::Config config_of(const RootContext& context,
                                        std::filesystem::path& config_path) {
    config_path = harness::resolve_config_path(context.config_path);
    return harness::load_config(config_path);
}

[[nodiscard]] nlohmann::json object_schema(nlohmann::json properties = nlohmann::json::object()) {
    return nlohmann::json{{"type", "object"}, {"properties", std::move(properties)}};
}

}  // namespace

std::span<const std::string_view> status_read_names() noexcept {
    return kStatusReads;
}

std::vector<mcp::ServedRead> status_reads(const RootContext& context) {
    std::vector<mcp::ServedRead> reads;

    reads.push_back(mcp::ServedRead{
        .name = std::string{kModelsListRead},
        .description =
            "Apogee's models: every configured backend and every model in its store, each with "
            "what it is and where it stands -- the document `apogee models list --output-format "
            "json` prints, byte for byte. Reads each stored model's header, so a large store "
            "takes a moment.",
        .input_schema = object_schema(
            {{"all",
              {{"type", "boolean"},
               {"description",
                "Also list the SafeTensors sets a conversion has already consumed, as models "
                "list --all does"}}}}),
        .answer =
            [&context](const nlohmann::json& args) {
                const bool all = flag_argument(args, "all");
                std::filesystem::path config_path;
                const harness::Config config = config_of(context, config_path);
                return answered(render_model_document(read_model_rows(config, config_path), all));
            },
    });

    reads.push_back(mcp::ServedRead{
        .name = std::string{kModelsStatusRead},
        .description =
            "Which backend each of Apogee's roles resolves to -- chat, the helpers, embedding -- "
            "and whether it is configured; under a suite, the suite's footprint too. The "
            "document `apogee models status --output-format json` prints, byte for byte.",
        .input_schema = object_schema(
            {{"suite",
              {{"type", "string"},
               {"description",
                "Resolve under this suite instead of models.default_suite, or off for none, as "
                "models status --suite does"}}}}),
        .answer =
            [&context](const nlohmann::json& args) {
                const std::string suite = string_argument(args, "suite");
                std::filesystem::path config_path;
                harness::Config config = config_of(context, config_path);
                if (!suite.empty()) {
                    if (const std::string refusal = select_suite(config, suite); !refusal.empty()) {
                        return refused("suite: " + refusal);
                    }
                }
                return answered(role_status_document(config, {}, machine_budget));
            },
    });

    reads.push_back(mcp::ServedRead{
        .name = std::string{kTrainStatusRead},
        .description =
            "Apogee's training at a glance: the cycle, the active pipeline, the newest runs and "
            "each fine-tuned backend's active version. The document `apogee train status "
            "--output-format json` prints, byte for byte.",
        .input_schema = object_schema(),
        .answer =
            [](const nlohmann::json&) { return answered(operations::training_status_document()); },
    });

    reads.push_back(mcp::ServedRead{
        .name = std::string{kTrainVersionsRead},
        .description =
            "The versions training has promoted, per backend, each with its run, its eval and "
            "which one is active -- every ledger, or one backend's. The document `apogee train "
            "versions --output-format json` prints, byte for byte.",
        .input_schema =
            object_schema({{"backend",
                            {{"type", "string"},
                             {"description", "One backend's ledger; every ledger when omitted"}}}}),
        .answer =
            [](const nlohmann::json& args) {
                std::string refusal;
                const std::optional<nlohmann::json> document =
                    train_versions_document(string_argument(args, "backend"), refusal);
                return document.has_value() ? answered(*document) : refused(refusal);
            },
    });

    reads.push_back(mcp::ServedRead{
        .name = std::string{kCheckRead},
        .description =
            "Apogee's install check: every row `apogee check` reports -- its section, status, "
            "detail and remedy -- and the counts. The document `apogee check --output-format "
            "json` prints, byte for byte. Nothing is repaired: `apogee check --fix` is the "
            "person's to run.",
        .input_schema = object_schema(),
        .answer =
            [&context](const nlohmann::json&) {
                return answered(
                    render_report_document(read_check_report(check_inputs(context)), std::nullopt));
            },
    });

    return reads;
}

int serve_mcp(const RootContext& context, std::istream& in, std::ostream& out) {
    // The native toolsets only, never the MCP registry: a config that
    // listed this very command as a server would otherwise spawn itself
    // without end.
    harness::Config config;
    try {
        config = harness::load_config(harness::resolve_config_path(context.config_path));
    } catch (const std::exception&) {
        // No config is fine: the defaults sandbox to the launch folder, and
        // each status read says the config's own reason when asked.
    }
    agent::ToolRegistry registry;
    tools::ToolsetOptions options;
    options.config = &config;
    options.fs_root = harness::expand_env(config.tools.fs_root);
    options.disabled = config.tools.disabled;
    tools::register_native_toolsets(registry, options);
    const mcp::ServedSet served = mcp::with_reads(
        mcp::read_only_tools(registry), mcp::dispatch_through(registry), status_reads(context));
    return mcp::serve_stdio(in, out, kServerName, std::string{version::semantic()}, served.tools,
                            served.dispatch);
}

}  // namespace apogee::commands
