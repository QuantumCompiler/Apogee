#include "cli/check.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <span>
#include <sstream>
#include <system_error>
#include <tuple>

#include "agent/fetch_url.h"
#include "agent/web_search.h"
#include "agentloop/recall.h"
#include "agentloop/rerank.h"
#include "agentloop/retriever.h"
#include "agentloop/structured.h"
#include "ansi/ansi.h"
#include "backends/adapters.h"
#include "backends/mlx_embed.h"
#include "backends/mlx_local.h"
#include "backends/prompt_cache.h"
#include "backends/provider_cache.h"
#include "backends/provider_probe.h"
#include "backends/provider_status.h"
#include "backends/provider_table.h"
#include "cli/embed.h"
#include "cli/helpers.h"
#include "cli/models_pull.h"
#include "cli/provider_offer.h"
#include "cli/train.h"
#include "contracts/assets.h"
#include "contracts/config_edit.h"
#include "contracts/config_migrate.h"
#include "contracts/host.h"
#include "contracts/jsonc.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "embedstore/ingest.h"
#include "graph/code_languages.h"
#include "harness/roles.h"
#include "httpserver/admin_auth.h"
#include "knowledge/store.h"
#include "machine/json_reporter.h"
#include "modelstore/gguf_inspect.h"
#include "modelstore/kv_cache.h"
#include "modelstore/mlx_info.h"
#include "modelstore/pipeline_info.h"
#include "modelstore/snapshot.h"
#include "modelstore/store.h"
#include "modelstore/voice_info.h"
#include "operations/acp_registration.h"
#include "operations/code_exec_server.h"
#include "operations/graph_sources.h"
#include "operations/mcp_presets.h"
#include "operations/suites.h"
#include "platform/child_process.h"
#include "platform/ffmpeg.h"
#include "platform/platform.h"
#include "platform/xcode.h"
#include "scaffold/agent.h"
#include "secrets/resolve.h"
#include "secrets/store.h"
#include "symphony/definition.h"
#include "tools/toolsets.h"
#include "training/cycle.h"
#include "training/kit.h"
#include "training/python_env.h"
#include "training/store.h"
#include "training/trainer.h"
#include "version/version.h"
#include "video/runtime.h"
#include "views/download_progress.h"

namespace apogee::commands {
namespace {

/// The environment variable a backend type conventionally reads its key from.
///
/// Deliberately mirrors what each provider's `from_config` says in its
/// missing-key error rather than inventing a second convention: a doctor that
/// names a different variable than the failure message does is worse than one
/// that stays quiet.

void add(CheckReport& report, Status status, std::string section, std::string name,
         std::string detail, std::string remedy = {}) {
    report.rows.push_back(
        {status, std::move(section), std::move(name), std::move(detail), std::move(remedy)});
}

/// Says what the doctor is doing now, when someone is listening (M1).
void say(const CheckInputs& inputs, const std::string& label, std::size_t done = 0,
         std::size_t total = 0) {
    if (inputs.progress) {
        inputs.progress(label, done, total);
    }
}

/// The MLX ladder's view of this install (27a): the data directory checked,
/// and the target -- this build's, unless a test names another.
[[nodiscard]] backends::MlxHost mlx_host(const CheckInputs& inputs) {
    backends::MlxHost host = backends::MlxHost::at(inputs.home);
    if (!inputs.host_target.empty()) {
        host.target = inputs.host_target;
    }
    return host;
}

void check_version(CheckReport& report, const CheckInputs& inputs) {
    add(report, Status::Ok, "Version", "apogee", std::string{version::semantic()});
    add(report, Status::Ok, "Version", "channel",
        std::string{harness::channel_name(inputs.root.channel)});
    add(report, Status::Ok, "Version", "root",
        inputs.home.string() + " -- " + harness::root_reason(inputs.root));

    if (inputs.executable.empty()) {
        return;
    }

#if defined(__APPLE__)
    // The browser-download case. v0.1.0 ships unsigned (decided 2026-09-01):
    // a curl-driven install sets no quarantine attribute, but downloading the
    // archive in a browser does, and Gatekeeper then refuses to run it with a
    // message that does not mention the attribute. Naming it here turns a
    // baffling failure into a one-line fix.
    const std::string command =
        "xattr -p com.apple.quarantine '" + inputs.executable.string() + "' >/dev/null 2>&1";
    if (std::system(command.c_str()) == 0) {  // NOLINT(cert-env33-c)
        add(report, Status::Warn, "Version", "quarantine",
            "this binary carries macOS's quarantine attribute (downloaded via a browser)",
            "xattr -d com.apple.quarantine '" + inputs.executable.string() + "'");
    } else {
        add(report, Status::Ok, "Version", "quarantine", "not quarantined");
    }
#endif
}

/// What a helper role reads.
enum class Medium : std::uint8_t { Image, Audio };

/// Why a llamacpp or cloud `backend` cannot read `medium`, or empty when it
/// can -- judged from the config and the projector's own header, without
/// loading a model.
[[nodiscard]] std::string medium_gap_text(const harness::BackendConfig& backend, Medium medium) {
    if (backend.type != harness::BackendType::LlamaCpp) {
        // A cloud API reads images; no cloud backend here is sent audio.
        return medium == Medium::Audio ? "only a local model's audio projector transcribes here"
                                       : std::string{};
    }
    const std::string what = medium == Medium::Audio ? "audio" : "an image";
    if (backend.mmproj_path.empty()) {
        return "it has no mmproj_path, so it cannot read " + what;
    }
    const models::GgufInfo projector =
        models::inspect_gguf(std::filesystem::path{harness::expand_env(backend.mmproj_path)});
    if (!projector.parsed) {
        return "its projector cannot be read: " + projector.parse_error;
    }
    const bool reads =
        medium == Medium::Audio ? projector.projector_audio : projector.projector_vision;
    if (!reads) {
        return std::string{"its projector has no "} +
               (medium == Medium::Audio ? "audio" : "vision") + " encoder";
    }
    return {};
}

/// Why a backend cannot read a medium, and the command that fixes it when
/// one does; both empty when it can.
struct MediumGap {
    std::string reason;
    std::string remedy;
};

/// Why `backend` cannot read `medium`, or empty when it can -- judged from
/// the config, the projector's own header, and for an `mlx` entry its model
/// directory and the environment (27c), without loading a model.
[[nodiscard]] MediumGap medium_gap(const harness::BackendConfig& backend, Medium medium,
                                   const backends::MlxHost& host) {
    if (backend.type == harness::BackendType::Mlx) {
        if (host.target != backends::kMlxTarget) {
            return {.reason = "the mlx backend runs on Apple silicon macOS only", .remedy = {}};
        }
        const backends::MlxModelInfo info = backends::inspect_mlx_model(
            std::filesystem::path{harness::expand_env_and_home(backend.model_path)});
        if (medium == Medium::Audio) {
            // Whether it hears (53g), by the rungs its provider answers
            // `accepts_audio` from (M23): an omni model mlx-vlm built does.
            const backends::MlxAudio audio = backends::probe_mlx_audio(info, host);
            return {.reason = audio.hears() ? std::string{}
                                            : std::string{backends::to_string(audio.refusal)} +
                                                  ": " + audio.reason,
                    .remedy = audio.remedy};
        }
        const backends::MlxVision vision = backends::probe_mlx_vision(info, host);
        return {.reason = vision.reason, .remedy = vision.remedy};
    }
    return {.reason = medium_gap_text(backend, medium), .remedy = {}};
}

/// Role pointers must name a backend that exists. A dangling one fails at
/// the point of use with a routing error that does not mention config. A
/// helper that reads a `medium` (26b) is also warned about when its backend
/// cannot read it, because the attachment that needs it fails much later,
/// somewhere else.
void check_role_pointer(CheckReport& report, const harness::Config& config,
                        const backends::MlxHost& host, std::string_view what,
                        const std::string& value, std::optional<Medium> medium = std::nullopt) {
    if (value.empty()) {
        return;
    }
    const harness::BackendConfig* backend = config.find_backend(value);
    if (backend == nullptr) {
        add(report, Status::Fail, "Config", std::string{what},
            "names a backend that is not configured: '" + value + "'",
            "apogee config set-default <one of your configured backends>");
        return;
    }
    if (medium.has_value()) {
        if (const MediumGap gap = medium_gap(*backend, *medium, host); !gap.reason.empty()) {
            std::string remedy = gap.remedy;
            if (remedy.empty()) {
                remedy = *medium == Medium::Audio
                             ? "point it at a llamacpp backend whose mmproj_path has an audio "
                               "encoder, or an mlx backend over an omni model mlx-vlm built"
                             : "point it at a backend with an mmproj_path, an mlx backend over a "
                               "vision model, or a cloud one";
            }
            add(report, Status::Warn, "Config", std::string{what}, value + " -- " + gap.reason,
                remedy);
            return;
        }
    }
    add(report, Status::Ok, "Config", std::string{what}, value);
}

/// One row per suite (27d): each member names a configured backend -- the
/// row that says so before a session does -- and a vision or transcription
/// member reads its medium, as a pointer at that role must. Since 27f a
/// consultable member's provider is asked whether it bills per call -- a
/// hand-edited one that does fails here, where the config verbs would have
/// refused it -- and the row names whom the root may consult. Since 27g the
/// verifier is held the same way, and the row names the seams it checks.
/// Since 27t a suite that orchestrates is held to the config verbs' own rule
/// -- every member a symphony it would offer reaches local and unmetered --
/// over `symphonies`, and the row says it orchestrates.
void check_suites(CheckReport& report, const harness::Config& config, const backends::MlxHost& host,
                  const MeteredProbe& metered, const symphony::Catalog& symphonies) {
    const harness::SuiteConfig* active = harness::active_suite(config);
    for (const auto& [name, suite] : config.suites) {
        const std::string label = "suite: " + name;
        if (suite.members.empty()) {
            add(report, Status::Warn, "Config", label,
                "names no members -- every role falls through to the global pointers",
                "apogee config set-suite " + name + " --<role> <backend>");
            continue;
        }
        std::string members;
        bool reported = false;
        for (const std::string_view role : harness::suite_role_names()) {
            const auto it = suite.members.find(role);
            if (it == suite.members.end()) {
                continue;
            }
            const harness::SuiteMember& member = it->second;
            const harness::BackendConfig* backend = config.find_backend(member.backend);
            if (backend == nullptr) {
                add(report, Status::Fail, "Config", label,
                    std::string{role} + " names a backend that is not configured: '" +
                        member.backend + "'",
                    "apogee config set-suite " + name + " --" + std::string{role} +
                        " <one of your configured backends>");
                reported = true;
                break;
            }
            std::optional<Medium> medium;
            if (role == "vision") {
                medium = Medium::Image;
            } else if (role == "transcription") {
                medium = Medium::Audio;
            }
            if (medium.has_value()) {
                if (const MediumGap gap = medium_gap(*backend, *medium, host);
                    !gap.reason.empty()) {
                    add(report, Status::Warn, "Config", label,
                        std::string{role} + " " + member.backend + " -- " + gap.reason, gap.remedy);
                    reported = true;
                    break;
                }
            }
            members += members.empty() ? "" : " · ";
            members += std::string{role} + " " + member.backend;
            if (member.context_size.has_value()) {
                members += " (window " + std::to_string(*member.context_size) + ")";
            }
        }
        for (const std::string& role : suite.consultable) {
            if (reported) {
                break;
            }
            const auto it = suite.members.find(role);
            if (it == suite.members.end()) {
                continue;  // the loader admits a consultable role with a member only
            }
            const MeteredAnswer answer = metered(config, it->second.backend);
            const std::string fix = "apogee config set-suite " + name +
                                    " --consultable <its local members>, or --" + role +
                                    " <a local backend>";
            if (!answer.unknown.empty()) {
                add(report, Status::Warn, "Config", label,
                    "consultable " + role + " -- whether '" + it->second.backend +
                        "' is billed per call cannot be told (" + answer.unknown +
                        "), so it is not offered to consult",
                    fix);
                reported = true;
            } else if (answer.metered) {
                add(report, Status::Fail, "Config", label,
                    "consultable " + role + " -- '" + it->second.backend +
                        "' is billed per call; a consult runs on the model's initiative, which "
                        "never spends",
                    fix);
                reported = true;
            }
        }
        // The verifier is a member call too (27g): held to the same rule.
        const harness::ValidatePolicy policy = harness::validate_policy(suite.validate);
        if (!reported && suite.validate.any()) {
            if (const auto it = suite.members.find(policy.verifier); it != suite.members.end()) {
                const MeteredAnswer answer = metered(config, it->second.backend);
                const std::string fix = "apogee config set-suite " + name +
                                        " --verifier <a local member>, or --" + policy.verifier +
                                        " <a local backend>";
                if (!answer.unknown.empty()) {
                    add(report, Status::Warn, "Config", label,
                        "verifier " + policy.verifier + " -- whether '" + it->second.backend +
                            "' is billed per call cannot be told (" + answer.unknown +
                            "), so nothing is checked by it",
                        fix);
                    reported = true;
                } else if (answer.metered) {
                    add(report, Status::Fail, "Config", label,
                        "verifier " + policy.verifier + " -- '" + it->second.backend +
                            "' is billed per call; a check runs on Apogee's initiative, which "
                            "never spends",
                        fix);
                    reported = true;
                }
            }
        }
        // A play the model starts is a member call too (27t): the rule the
        // config verbs hold `orchestrate: true` to, asked of a hand-edited
        // file or a symphony added since.
        if (!reported && suite.orchestrate) {
            if (const std::string refused =
                    validate_suite_orchestrate(config, name, suite, metered, &symphonies);
                !refused.empty()) {
                add(report, Status::Fail, "Config", label, refused,
                    "apogee config set-suite " + name + " --orchestrate off");
                reported = true;
            }
        }
        if (!reported) {
            std::string consult;
            for (const std::string& role : suite.consultable) {
                consult += (consult.empty() ? "  · consult: " : ", ") + role;
            }
            std::string validate;
            if (suite.validate.any()) {
                std::string seams;
                const auto seam = [&seams](bool on, std::string_view seam_name) {
                    if (on) {
                        seams += (seams.empty() ? "" : ", ") + std::string{seam_name};
                    }
                };
                seam(policy.tool_args, "tool_args");
                seam(policy.extraction, "extraction");
                seam(policy.answers_always, "answers always");
                validate = "  · validate: " + (seams.empty() ? std::string{"on request"} : seams) +
                           " (verifier " + policy.verifier + ")";
            }
            std::string detail = members + consult + validate;
            if (suite.orchestrate) {
                detail += "  · orchestrates";
            }
            if (active == &suite) {
                detail += "  -- the default suite";
            }
            add(report, Status::Ok, "Config", label, detail);
        }
    }
}

void check_config(CheckReport& report, const CheckInputs& inputs) {
    const std::string file = inputs.config_path.filename().string();
    if (inputs.config_missing) {
        // A fresh install has no config yet. That is a state with an obvious
        // fix, not a broken installation.
        add(report, Status::Warn, "Config", file, "not found at " + inputs.config_path.string(),
            "apogee config init");
        return;
    }
    if (!inputs.config_error.empty()) {
        add(report, Status::Fail, "Config", file, inputs.config_error);
        return;
    }

    if (inputs.config_legacy) {
        // The compat read (28i): healthy, and on its way out.
        add(report, Status::Ok, "Config", file,
            "parses cleanly -- the older YAML format, still read; migrate converts it to " +
                std::string{harness::kConfigFileName} + ", every comment kept",
            "apogee config migrate");
    } else if (const std::size_t missing = inputs.config_missing_options.size(); missing > 0) {
        // Behind the template is healthy: the absent options have their
        // defaults. Said, so a newer release's options can be found.
        add(report, Status::Ok, "Config", file,
            "parses cleanly -- " + std::to_string(missing) +
                (missing == 1 ? " option" : " options") +
                " from this release's starter config not in the file (their defaults apply)",
            "apogee config upgrade");
    } else {
        add(report, Status::Ok, "Config", file, "parses cleanly");
    }

    const harness::Config& config = inputs.config;
    if (config.backends.empty()) {
        // A fresh install's natural state, not a defect (M11): the config is
        // seeded before any key or model exists, like the models row's
        // "bundles none". The offer and the scan exist to move it along.
        add(report, Status::Skipped, "Config", "backends",
            "none configured yet -- a chat needs one; 'apogee providers scan --register' finds "
            "this machine's providers",
            "apogee config add-backend <name> --type <type>");
        return;
    }

    const secrets::CredentialStore store{secrets::credentials_path(inputs.config_path)};
    const secrets::EnvSnapshot env = secrets::EnvSnapshot::capture(inputs.env);
    // The local backends' headers are the slow part: counted, so the line
    // can say how far through them it is.
    const auto local =
        static_cast<std::size_t>(std::ranges::count_if(config.backends, [](const auto& entry) {
            return entry.second.type == harness::BackendType::LlamaCpp &&
                   !entry.second.model_path.empty();
        }));
    std::size_t read = 0;
    for (const auto& [name, backend] : config.backends) {
        const std::string label = "backend: " + name;
        const std::string_view type = harness::to_string(backend.type);

        // A key no backend reads does nothing, and said nothing until 55g: a
        // warning naming it, with the key it most likely meant. The entry
        // still loads -- a newer release's key read here is one of these.
        for (const std::string& key : backend.unknown_keys) {
            const std::string_view meant = harness::nearest_backend_key(key);
            add(report, Status::Warn, "Config", label,
                "unknown key '" + key + "' -- no backend reads it, so it does nothing" +
                    (meant.empty() ? std::string{}
                                   : "; did you mean '" + std::string{meant} + "'?"),
                meant.empty() ? "remove it from " + file +
                                    " ('apogee config set-backend --help' lists a backend's fields)"
                              : "rename it '" + std::string{meant} + "' in " + file);
        }

        if (backend.type == harness::BackendType::LlamaCpp) {
            if (backend.model_path.empty()) {
                // The entry exists: a door the binary accepts on it (M14).
                add(report, Status::Warn, "Config", label,
                    std::string{type} + " -- model_path not set",
                    "apogee config set-backend " + name + " --model-path <file.gguf>");
                continue;
            }
            const std::filesystem::path model = harness::expand_env(backend.model_path);
            say(inputs, "checking config: " + model.filename().string(), ++read, local);
            std::error_code code;
            if (!std::filesystem::exists(model, code)) {
                // A dangling model_path is a real failure -- the backend cannot
                // answer -- but check does NOT edit it out. It says what to run.
                // Its two real doors (M14): add-backend on an existing name
                // is refused, so the hint never names it.
                add(report, Status::Fail, "Config", label,
                    "model_path does not exist: " + model.string(),
                    "apogee config set-backend " + name +
                        " --model-path <an existing .gguf> to repoint it, or apogee config "
                        "delete-backend " +
                        name + " to remove it");
                continue;
            }
            // A full header read rather than the 4-byte magic check this used
            // to do. Magic alone passes a half-finished download -- and the row
            // then said "model loads", which was a claim four bytes cannot
            // support and which reads exactly like a pass.
            const models::GgufInfo info = models::inspect_gguf(model);
            if (!info.parsed) {
                add(report, Status::Fail, "Config", label, "unreadable GGUF -- " + info.parse_error,
                    "re-download the model to " + model.string());
                continue;
            }
            if (info.is_projector()) {
                // A projector configured as a model_path is a real mistake with
                // an exact fix, so it is a failure rather than a note.
                add(report, Status::Fail, "Config", label,
                    "this is a multimodal projector, not a model",
                    "move it to mmproj_path and set model_path to the model it projects for");
                continue;
            }
            if (info.has_vision_tensors()) {
                // Advisory, never fatal: under the open-model policy a warning
                // tells the user something, and refusing tells them nothing
                // they asked for.
                add(report, Status::Warn, "Config", label,
                    std::string{type} + " -- combined text+vision blob (" +
                        std::to_string(info.tensors - info.text_tensors) + " vision tensors)");
                continue;
            }
            add(report, Status::Ok, "Config", label,
                std::string{type} + " -- GGUF header ok" +
                    (info.architecture.empty() ? "" : " (" + info.architecture + ")") + "; " +
                    models::describe(models::local_window(info, backend)));
            continue;
        }

        if (backend.type == harness::BackendType::Mlx) {
            // The same ladder construction runs (27a), so the doctor and a
            // build cannot disagree about whether this entry can run.
            const backends::MlxReadiness readiness =
                backends::probe_mlx_backend(name, backend, mlx_host(inputs));
            if (readiness.ready()) {
                const backends::MlxModelInfo info =
                    backends::inspect_mlx_model(readiness.model_dir);
                // Its files read whole, as a GGUF's header is: a shard cut
                // short fails here, not at the first turn (27b).
                const models::MlxInfo files = models::read_mlx_info(readiness.model_dir);
                if (!files.complete) {
                    // A stored model is repaired by its handle; one placed
                    // by hand has nothing to be fetched from.
                    const std::optional<models::StoredMlx> stored = models::stored_mlx_at(
                        models::list_store_mlx(models::StoreRoots::at(inputs.home / "models")),
                        readiness.model_dir);
                    add(report, Status::Fail, "Config", label,
                        std::string{type} + " -- " + files.problem,
                        stored.has_value() ? "apogee models repair " +
                                                 models::weights_handle(
                                                     stored->model, models::kMlxFormat, stored->id)
                                           : std::string{});
                    continue;
                }
                if (backends::mlx_entry_embeds(backend)) {
                    // An embedding entry (53h): what it embeds, and how wide.
                    add(report, Status::Ok, "Config", label,
                        std::string{type} + " -- an embedding model directory (" +
                            (info.model_type.empty() ? std::string{"its type unstated"}
                                                     : info.model_type) +
                            "), " + files.quantization.describe() + "; vectors " +
                            models::describe(
                                models::read_mlx_embedding_width(readiness.model_dir)) +
                            "; it embeds through mlx-embeddings and answers no chat; its "
                            "runtime is present");
                    continue;
                }
                // Whether its pictures are read as they are (27c), never claimed
                // for a model that cannot.
                std::string sight;
                if (info.vision) {
                    sight = backends::probe_mlx_vision(info, mlx_host(inputs)).reads_images()
                                ? "; a vision model, reading images through mlx-vlm"
                                : "; a vision model, but mlx-vlm is not installed, so it "
                                  "does not read images as they are";
                }
                // And whether it hears (53g), said beside its sight for a
                // directory with an audio tower (M23): the provider's own
                // rungs, the one that refuses named with its way.
                std::string hearing;
                if (const backends::MlxAudio audio =
                        backends::probe_mlx_audio(info, mlx_host(inputs));
                    audio.tower) {
                    hearing = audio.hears()
                                  ? "; it hears recordings as they are, through mlx-vlm"
                                  : "; it has an audio tower, but does not hear recordings as "
                                    "they are -- " +
                                        audio.message();
                }
                add(report, Status::Ok, "Config", label,
                    std::string{type} + " -- " +
                        (info.model_type.empty() ? std::string{"a"} : info.model_type) +
                        " model directory" +
                        (info.chat_template ? "" : " with no chat template (a base model)") + ", " +
                        files.quantization.describe() + "; " +
                        models::describe(models::mlx_window(files, backend)) + sight + hearing +
                        "; its runtime is present");
                continue;
            }
            // A directory that is not there is a real failure, as a dangling
            // GGUF is; a runtime not set up yet is a warning with its fix.
            const bool broken = readiness.refusal == backends::MlxRefusal::ModelMissing ||
                                readiness.refusal == backends::MlxRefusal::NotAModelDirectory;
            add(report, broken ? Status::Fail : Status::Warn, "Config", label,
                std::string{type} + " -- " + readiness.reason, readiness.remedy);
            continue;
        }

        if (secrets::takes_api_key(backend.type)) {
            // The ONE chain, so the doctor reports exactly what a build would
            // use -- and only WHERE it came from. NEVER the key itself.
            const secrets::KeyResolution resolution =
                secrets::resolve_api_key(backend, &store, env);
            switch (resolution.source) {
                case secrets::KeySource::Config:
                    add(report, Status::Ok, "Config", label,
                        std::string{type} + " -- API key from config");
                    break;
                case secrets::KeySource::Store:
                    add(report, Status::Ok, "Config", label,
                        std::string{type} + " -- API key from the credential store");
                    break;
                case secrets::KeySource::Environment:
                    add(report, Status::Ok, "Config", label,
                        std::string{type} + " -- API key from " + resolution.variable);
                    break;
                case secrets::KeySource::None: {
                    // A missing key is a WARNING, not a failure: a keyless
                    // install is valid, and the whole point of the
                    // fresh-install criterion is that it passes.
                    const std::string variable =
                        std::string{secrets::conventional_variables(backend.type).front()};
                    add(report, Status::Warn, "Config", label,
                        std::string{type} + " -- no API key found",
                        "apogee auth add " + std::string{type} + "   (or export " + variable +
                            "=..., or set api_key: \"${" + variable + "}\" on this backend)");
                    break;
                }
            }
            continue;
        }

        // A vendor CLI's row is the Providers section's (28c): whether its
        // binary is there is the one fact that matters, and "ok" for an
        // entry that merely parses read like a pass.
        if (harness::is_vendor_cli(backend.type)) {
            continue;
        }
        add(report, Status::Ok, "Config", label, std::string{type});
    }

    const backends::MlxHost host = mlx_host(inputs);
    check_role_pointer(report, config, host, "default_backend", config.models.default_backend);
    check_role_pointer(report, config, host, "default_embedding", config.models.default_embedding);
    check_role_pointer(report, config, host, "default_extraction",
                       config.models.default_extraction);
    check_role_pointer(report, config, host, "default_vision", config.models.default_vision,
                       Medium::Image);
    check_role_pointer(report, config, host, "default_transcription",
                       config.models.default_transcription, Medium::Audio);
    check_role_pointer(report, config, host, "default_utility", config.models.default_utility);
    check_suites(report, config, host,
                 inputs.metered ? inputs.metered : provider_metered_probe(inputs.config_path),
                 symphony::catalog(config, symphony::directory_for(inputs.config_path)));

    // Collections: a typo in `retriever:` must never silently mean auto, and a
    // `rerank:` or `backend:` must name something that exists. The validator
    // is the same one the flag uses, so a value cannot pass here and fail
    // there.
    for (const auto& [name, collection] : config.embeddings) {
        const std::string label = "collection: " + name;
        if (!agentloop::valid_retriever(collection.retriever)) {
            add(report, Status::Fail, "Config", label,
                agentloop::retriever_values_message("retriever", collection.retriever),
                "set retriever to lexical, vector, hybrid, or auto");
            continue;
        }
        if (!agentloop::valid_rerank(collection.rerank, config)) {
            add(report, Status::Fail, "Config", label,
                "rerank names a backend that is not configured: '" + collection.rerank + "'",
                "set rerank to a configured backend, on, or off");
            continue;
        }
        if (!collection.backend.empty() && config.find_backend(collection.backend) == nullptr) {
            add(report, Status::Fail, "Config", label,
                "backend names a backend that is not configured: '" + collection.backend + "'",
                "set backend to a configured backend, or remove it to use "
                "models.default_embedding");
            continue;
        }
        add(report, Status::Ok, "Config", label,
            collection.retriever.empty() ? "auto" : collection.retriever);
    }
}

/// The Providers section (28c): each configured provider backend's tier in
/// plain words -- a vendor CLI whose binary is missing WARNS, with the
/// use-time error's own remedy, where the config's shape once said ok --
/// then the providers the last scan found that no backend reaches. Read
/// from the provider cache and the cheap checks alone: no binary is started
/// here, and a machine never scanned is said, not probed. Absent entirely
/// on a pure-local install with nothing detected.
void check_providers(CheckReport& report, const CheckInputs& inputs) {
    const backends::ProviderCache cache =
        backends::load_provider_cache(inputs.home / "cache" / "providers.json");
    const bool configured_any = std::ranges::any_of(inputs.config.backends, [](const auto& entry) {
        return backends::provider_for_type(entry.second.type) != nullptr;
    });
    std::vector<backends::ProviderStatus> unregistered;
    for (const backends::ProviderStatus& status : cached_statuses(cache)) {
        const bool reached = std::ranges::any_of(inputs.config.backends, [&](const auto& entry) {
            return entry.second.type == status.type;
        });
        if (!reached && offerable(status)) {
            unregistered.push_back(status);
        }
    }
    if (!configured_any && unregistered.empty()) {
        return;
    }

    const std::unique_ptr<backends::ExistenceView> host =
        inputs.provider_view == nullptr ? backends::host_existence_view() : nullptr;
    const backends::ExistenceView& view =
        inputs.provider_view != nullptr ? *inputs.provider_view : *host;
    const secrets::CredentialStore store{secrets::credentials_path(inputs.config_path)};
    const secrets::EnvSnapshot env = secrets::EnvSnapshot::capture(inputs.env);
    for (const auto& [name, backend] : inputs.config.backends) {
        const std::optional<backends::ProviderStatus> status =
            backends::backend_provider_status(backend, view, &store, env, cache);
        if (!status.has_value()) {
            continue;
        }
        const std::string label = "backend: " + name;
        const std::string type{harness::to_string(backend.type)};
        if (status->tier() == backends::ProviderTier::NotFound) {
            // An API key that resolves nowhere is the Config row's warning,
            // said once; a vendor CLI's missing binary is said here.
            if (harness::is_vendor_cli(backend.type)) {
                add(report, Status::Warn, "Providers", label,
                    type + " -- " + status->installed_evidence,
                    std::string{backends::provider_for_type(backend.type)->remedy});
            }
            continue;
        }
        add(report, Status::Ok, "Providers", label,
            type + " -- " + backends::describe_status(*status));
    }
    for (const backends::ProviderStatus& status : unregistered) {
        const backends::ProviderFacts* facts = backends::provider_for_type(status.type);
        const std::string type{harness::to_string(status.type)};
        add(report, Status::Ok, "Providers", "provider: " + status.id,
            std::string{backends::to_string(status.tier())} + ", not registered",
            facts->needs_model
                ? "apogee config add-backend " + status.id + " --type " + type + " --model <model>"
                : std::string{"apogee providers scan --register"});
    }
    if (cache.scanned()) {
        add(report, Status::Ok, "Providers", "scan", "last scanned " + cache.scanned_at);
    } else {
        add(report, Status::Ok, "Providers", "scan",
            "not scanned yet -- versions, status-command logins and providers no backend "
            "reaches are unknown",
            "apogee check --refresh-providers   (or apogee providers scan)");
    }
}

void check_filesystem(CheckReport& report, const CheckInputs& inputs) {
    std::error_code code;
    if (!std::filesystem::exists(inputs.home, code)) {
        add(report, Status::Fail, "Filesystem", "data directory",
            "does not exist: " + inputs.home.string(), "apogee check --fix");
        return;
    }
    add(report, Status::Ok, "Filesystem", "data directory", inputs.home.string());

    // Enumerated from the contract, never restated -- harness/layout.h.
    for (const harness::LayoutEntry& entry : harness::data_directories()) {
        const std::filesystem::path path = inputs.home / entry.relative_path;
        const std::string label = std::string{entry.relative_path} + "/";

        if (!std::filesystem::exists(path, code)) {
            add(report, Status::Fail, "Filesystem", label,
                "missing -- " + std::string{entry.purpose}, "apogee check --fix");
            continue;
        }
        if (!std::filesystem::is_directory(path, code)) {
            add(report, Status::Fail, "Filesystem", label, "exists but is not a directory");
            continue;
        }

        if (!entry.private_mode) {
            add(report, Status::Ok, "Filesystem", label, std::string{entry.purpose});
            continue;
        }

        if (!harness::supports_private_modes()) {
            // Reported, not silently passed: see the Status::Skipped comment.
            add(report, Status::Skipped, "Filesystem", label,
                "mode check not applicable on this platform (no POSIX file modes)");
            continue;
        }

        const std::filesystem::perms mode =
            std::filesystem::status(path, code).permissions() & std::filesystem::perms::mask;
        const bool leaks =
            (mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) !=
            std::filesystem::perms::none;
        if (leaks) {
            add(report, Status::Fail, "Filesystem", label,
                "is readable by other users, and may hold conversation transcripts",
                "chmod 700 '" + path.string() + "'   (or: apogee check --fix)");
        } else {
            add(report, Status::Ok, "Filesystem", label, std::string{entry.purpose} + " (private)");
        }
    }
}

/// The model store's roots as `check` sees them: `paths.hf_dir` from the
/// config when it sets one.
[[nodiscard]] models::StoreRoots check_roots(const CheckInputs& inputs) {
    models::StoreRoots roots = models::StoreRoots::at(inputs.home / "models");
    if (!inputs.config.paths.hf_dir.empty()) {
        roots.safetensors =
            std::filesystem::path{harness::expand_env_and_home(inputs.config.paths.hf_dir)};
    }
    return roots;
}

void check_models(CheckReport& report, const CheckInputs& inputs) {
    std::error_code code;
    if (!std::filesystem::exists(inputs.home / "models", code)) {
        return;  // already reported by the filesystem section
    }
    const models::StoreRoots roots = check_roots(inputs);

    int found = 0;
    const std::vector<models::StoredGguf> ggufs = models::list_store_ggufs(roots);
    const std::vector<models::StoredSnapshot> snapshots = models::list_store_snapshots(roots);
    const std::vector<models::StoredMlx> mlx = models::list_store_mlx(roots);
    const std::vector<models::StoredPipeline> pipelines = models::list_store_pipelines(roots);
    const std::vector<models::StoredVoice> voices = models::list_store_voices(roots);
    const std::size_t total =
        ggufs.size() + snapshots.size() + mlx.size() + pipelines.size() + voices.size();
    std::size_t read = 0;
    for (const models::StoredGguf& stored : ggufs) {
        ++found;
        const std::string name = stored.model + "/gguf/" + stored.id;
        say(inputs, "checking models: " + stored.file.filename().string(), ++read, total);
        // A full header read, not the 4-byte magic check this used to do. The
        // failure that actually happens is a half-finished download, and that
        // file has perfectly valid magic -- so magic alone reported "valid
        // GGUF header" for exactly the file that cannot be loaded.
        const models::GgufInfo info = models::inspect_gguf(stored.file);
        if (!info.parsed) {
            add(report, Status::Fail, "Models", name, "unreadable GGUF -- " + info.parse_error,
                "apogee models repair " + name);
        } else {
            add(report, Status::Ok, "Models", name,
                info.architecture.empty() ? "valid GGUF header"
                                          : info.architecture + ", valid GGUF header");
        }
    }
    for (const models::StoredSnapshot& stored : snapshots) {
        ++found;
        const std::string name = stored.model + "/safetensors/" + stored.id;
        say(inputs, "checking models: " + name, ++read, total);
        if (models::config_is_download_record(stored.dir)) {
            add(report, Status::Warn, "Models", name,
                "damaged by an older pull: its config.json is a download record",
                "apogee models repair " + name);
        } else if (const std::optional<models::DraftHead> head =
                       models::snapshot_draft_head(stored.dir)) {
            add(report, Status::Ok, "Models", name,
                "SafeTensors weights, " +
                    models::draft_head_note(*head, models::draft_head_bases(roots, *head)));
        } else {
            add(report, Status::Ok, "Models", name, "SafeTensors weights");
        }
    }
    // An MLX model's files read whole -- the configuration the model's, a
    // tokenizer, every shard the index names and each within its file --
    // never "loads", which only a load can say (27b).
    for (const models::StoredMlx& stored : mlx) {
        ++found;
        const std::string name =
            models::weights_handle(stored.model, models::kMlxFormat, stored.id);
        say(inputs, "checking models: " + name, ++read, total);
        const models::MlxInfo info = models::read_mlx_info(stored.dir);
        if (!info.complete) {
            add(report, Status::Fail, "Models", name, "MLX model that " + info.problem,
                "apogee models repair " + name);
        } else {
            add(report, Status::Ok, "Models", name,
                (info.model_type.empty() ? std::string{} : info.model_type + ", ") + "MLX, " +
                    info.quantization.describe() + ", files whole");
        }
    }
    // A diffusion pipeline the same way: every component the index names
    // there with what its kind needs, every shard within its file (49a).
    for (const models::StoredPipeline& stored : pipelines) {
        ++found;
        const std::string name =
            models::weights_handle(stored.model, models::kPipelineFormat, stored.id);
        say(inputs, "checking models: " + name, ++read, total);
        const models::PipelineInfo info = models::read_pipeline_info(stored.dir);
        if (!info.complete) {
            add(report, Status::Fail, "Models", name, "pipeline that " + info.problem,
                "apogee models repair " + name);
        } else {
            add(report, Status::Ok, "Models", name,
                info.class_name + ", " + std::to_string(info.components.size()) +
                    " components, files whole");
        }
    }
    // A voice model the same way: the configuration, one checkpoint and
    // every voice whole (50a).
    for (const models::StoredVoice& stored : voices) {
        ++found;
        const std::string name =
            models::weights_handle(stored.model, models::kVoiceFormat, stored.id);
        say(inputs, "checking models: " + name, ++read, total);
        const models::VoiceInfo info = models::read_voice_info(stored.dir);
        if (!info.complete) {
            add(report, Status::Fail, "Models", name, "voice model that " + info.problem,
                "apogee models repair " + name);
        } else {
            add(report, Status::Ok, "Models", name,
                info.checkpoint + ", " + std::to_string(info.voices.size()) +
                    " voices, files whole");
        }
    }

    // What the flat layout left is found, never read in place: say so, and
    // name the one command that moves it -- `check` itself never edits the
    // config that points at it.
    const models::LegacyLayout legacy = models::find_legacy(roots);
    if (!legacy.empty()) {
        add(report, Status::Warn, "Models", "old layout",
            std::to_string(legacy.ggufs.size() + legacy.snapshots.size()) +
                " model(s) still in the flat layout, which nothing reads any more",
            "apogee models migrate");
    }

    // What an interrupted pull, convert or quantize left: its whole staging
    // directory, at full size -- two 52 GB copies of one model, the first
    // time (2026-09-23). A live run's is claimed by its process and never
    // listed, so the fix below cannot pull one out from under it.
    const std::vector<models::AbandonedStaging> leftovers = models::find_abandoned_staging(roots);
    if (!leftovers.empty()) {
        std::uintmax_t bytes = 0;
        for (const models::AbandonedStaging& leftover : leftovers) {
            bytes += leftover.bytes;
        }
        add(report, Status::Warn, "Models", "leftovers",
            std::to_string(leftovers.size()) +
                " staging folder(s) an interrupted pull, convert or quantize left behind, " +
                format_progress_size(static_cast<std::int64_t>(bytes)),
            "apogee check --fix");
    }

    // The prompt cache (26j): what the saved states take, against the cap
    // that keeps them from growing without end.
    const backends::PromptCacheUsage cached =
        backends::PromptCache{inputs.home / "cache" / "prompt"}.usage();
    if (cached.files > 0) {
        add(report, Status::Ok, "Models", "prompt cache",
            std::to_string(cached.files) + " saved state(s), " +
                format_progress_size(static_cast<std::int64_t>(cached.bytes)) + " of " +
                format_progress_size(static_cast<std::int64_t>(backends::kPromptCacheCapBytes)) +
                " -- cache/prompt/, safe to delete");
    }

    if (found == 0 && legacy.empty()) {
        // The fresh-install criterion in one row: no models is CORRECT.
        // Apogee bundles none and downloads none without being asked.
        add(report, Status::Ok, "Models", "models/",
            "no models installed -- Apogee bundles none; pull one with 'apogee models pull'");
    }
}

/// `text` as one word of a command a person copies: double-quoted when a
/// shell would split it or read a backslash as an escape (a Windows path), as
/// `--adapters` spells a set (53a).
[[nodiscard]] std::string shell_word(const std::string& text) {
    return text.find_first_of(" '\"\\") == std::string::npos ? text : "\"" + text + "\"";
}

/// The door that promotes `run` onto `name` again (53c), as it would run
/// now, so it runs as printed: on a quantized base its gate scores the
/// candidate there and needs a suite -- named where neither the run's
/// recorded eval nor the config names one; on any other base the gate reads
/// the run's recorded eval, which is scored first where none passed and the
/// gate is hard.
[[nodiscard]] std::string promote_again_door(const CheckInputs& inputs,
                                             const training::TrainingStore& store,
                                             const std::string& run, const std::string& name,
                                             bool quantized) {
    const std::string promote = "apogee train promote " + run + " --as " + name + " --adapter";
    std::string error;
    const std::optional<training::RunManifest> manifest =
        training::read_manifest(store.run_dir(run), error);
    const std::optional<training::EvalResults> recorded =
        manifest.has_value() ? manifest->eval : std::nullopt;
    if (quantized) {
        const bool suite_known = (recorded.has_value() && !recorded->suite.empty()) ||
                                 !inputs.config.training.eval_suite_path.empty();
        return promote + (suite_known ? "" : " --suite <path|name>");
    }
    const bool hard = training::gate_mode_from_string(
                          inputs.config.training.effective_gate_mode()) == training::GateMode::Hard;
    if (hard && !(recorded.has_value() && recorded->passed)) {
        return "apogee train eval " + run + " --suite <path|name> --force && " + promote;
    }
    return promote;
}

/// The `Adapters` section (53c): a row for every adapter an entry applies,
/// its facts read through 53a's one implementation (`backends/adapters`) --
/// a scale the loader allows; its file there and the kind its runtime reads;
/// pinned; and the base's weights the ones it was pinned to, the base hashed
/// once per entry and only when a pin asks. That hash comes from the memo a
/// load keeps, read and never written back: the doctor edits nothing, a
/// cache included. Each failure names its doors, each a command that runs as
/// printed -- for a version `train promote --adapter` made, the promote that
/// makes it again -- and a pin that cannot be checked is skipped, never
/// passed. Absent when no entry applies an adapter.
void check_adapters(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_missing || !inputs.config_error.empty()) {
        return;
    }
    const training::TrainingStore store{inputs.home / "training"};
    const models::StoreRoots roots = check_roots(inputs);
    for (const auto& [name, backend] : inputs.config.backends) {
        if (backend.adapters.empty() || !harness::takes_adapters(backend.type)) {
            continue;
        }
        const bool mlx = backend.type == harness::BackendType::Mlx;
        const std::filesystem::path base{harness::expand_env_and_home(backend.model_path)};
        const backends::AdapterCheck check{
            .backend = name,
            .base = base,
            .directories = mlx,
            .hashing = {.memo = inputs.home / "cache" / "weight-hashes.json", .remember = false}};
        const std::string set_backend = "apogee config set-backend " + name;
        const std::string pin_all =
            set_backend + " --adapters " + backends::spell_adapters(backend.adapters);

        // The run a promoted version came from (53b's ledger), while its
        // manifest is still there to promote it from again.
        std::string ledger_error;
        const std::optional<training::VersionLedger> ledger =
            training::load_ledger(store.versions_dir(), name, ledger_error);
        const auto promoted_from = [&](const harness::AdapterConfig& adapter) {
            if (!ledger.has_value()) {
                return std::string{};
            }
            std::error_code code;
            for (const training::VersionEntry& version : ledger->versions) {
                if (version.adapter() && training::valid_run_id(version.run_id) &&
                    std::filesystem::path{version.adapter_path}.lexically_normal() ==
                        std::filesystem::path{adapter.path}.lexically_normal() &&
                    std::filesystem::is_regular_file(
                        training::manifest_path(store.run_dir(version.run_id)), code)) {
                    return version.run_id;
                }
            }
            return std::string{};
        };
        // The weights an adapter was pinned to, when the store holds them
        // apart from the base -- filed under the hash's first twelve
        // characters -- so a door can point model_path back at them.
        const auto stored_base = [&](const std::string& pin) {
            const std::string id = pin.substr(0, models::kWeightIdLength);
            const auto elsewhere = [&base](const std::filesystem::path& found) {
                return found.lexically_normal() != base.lexically_normal();
            };
            if (mlx) {
                for (const models::StoredMlx& stored : models::list_store_mlx(roots)) {
                    if (stored.id == id && elsewhere(stored.dir)) {
                        return stored.dir;
                    }
                }
            } else {
                for (const models::StoredGguf& stored : models::list_store_ggufs(roots)) {
                    if (stored.id == id && elsewhere(stored.file)) {
                        return stored.file;
                    }
                }
            }
            return std::filesystem::path{};
        };

        std::optional<std::string> base_sha256;  // read once, when a pin asks
        // What the base is, for a promoted version's door (its header alone).
        std::optional<training::ServingBase> serving;
        for (std::size_t index = 0; index < backend.adapters.size(); ++index) {
            const harness::AdapterConfig& adapter = backend.adapters[index];
            // A directory named with its trailing slash is still that directory.
            const std::filesystem::path named =
                std::filesystem::path{adapter.path}.lexically_normal();
            const std::string label =
                name + ": " +
                (named.has_filename() ? named : named.parent_path()).filename().string();
            std::vector<harness::AdapterConfig> rest = backend.adapters;
            rest.erase(rest.begin() + static_cast<std::ptrdiff_t>(index));
            const std::string take_off =
                set_backend + " --adapters " + backends::spell_adapters(rest) + " to take it off";
            const std::string run = promoted_from(adapter);
            if (!run.empty() && !serving.has_value()) {
                serving = serving_base(name, backend);
            }
            const std::string promote_again =
                run.empty() ? std::string{}
                            : promote_again_door(inputs, store, run, name, serving->quantized);
            std::ostringstream scale;
            scale << adapter.scale;

            // The scale the loader allows (53a), asked again: the doctor
            // never assumes how its config was read.
            if (!std::isfinite(adapter.scale) || adapter.scale <= 0.0 ||
                adapter.scale > harness::kMaxAdapterScale || (mlx && adapter.scale != 1.0)) {
                std::vector<harness::AdapterConfig> plain = backend.adapters;
                plain[index].scale = 1.0;
                add(report, Status::Fail, "Adapters", label,
                    "applied at scale " + scale.str() +
                        (mlx ? " -- mlx-lm applies an adapter at the scale its training wrote, "
                               "so an mlx entry's is 1"
                             : " -- a scale is above 0 and at most 2"),
                    set_backend + " --adapters " + backends::spell_adapters(plain) +
                        " to apply it at scale 1");
                continue;
            }
            switch (backends::adapter_files(adapter, check)) {
                case backends::AdapterFiles::Missing:
                    add(report, Status::Fail, "Adapters", label, "not there: " + adapter.path,
                        (promote_again.empty() ? std::string{}
                                               : promote_again + " to make it again from its run, "
                                                                 "or ") +
                            take_off);
                    continue;
                case backends::AdapterFiles::WrongKind:
                    // What the directory lacks, said as a load refuses it
                    // (55e): its configuration, or the weights mlx-lm applies.
                    add(report, Status::Fail, "Adapters", label,
                        (mlx ? "not an mlx-lm adapter directory (" +
                                   backends::adapter_kind_gap(adapter, check) + "): "
                             : "not a file -- a llamacpp entry applies a GGUF LoRA file: ") +
                            adapter.path,
                        take_off);
                    continue;
                case backends::AdapterFiles::Unpinned:
                    add(report, Status::Fail, "Adapters", label,
                        "pinned to no base -- a load refuses it until the set is pinned to the "
                        "weights it applies over",
                        pin_all + " to pin the set to the weights model_path names");
                    continue;
                case backends::AdapterFiles::Pinned:
                    break;
            }
            const std::string pin = adapter.base_sha256.substr(0, models::kWeightIdLength);
            const auto point_back = [&] {
                const std::filesystem::path pinned = stored_base(adapter.base_sha256);
                return pinned.empty()
                           ? std::string{}
                           : set_backend + " --model-path " + shell_word(pinned.string()) +
                                 " to run it over the weights it was pinned to";
            };
            if (backend.model_path.empty()) {
                add(report, Status::Skipped, "Adapters", label,
                    "its pin cannot be checked: " + name +
                        " names no model_path, the weights it applies over",
                    point_back());
                continue;
            }
            if (!base_sha256.has_value()) {
                // The slow part, when the memo does not hold the base: a
                // whole read of its weights.
                say(inputs, "checking adapters: " + name + "'s base");
                base_sha256 = backends::weights_sha256(base, check.hashing);
            }
            if (base_sha256->empty()) {
                add(report, Status::Skipped, "Adapters", label,
                    "its pin cannot be checked: the base weights at " + base.string() +
                        " could not be read -- the backend's own row says why",
                    point_back());
                continue;
            }
            if (*base_sha256 != adapter.base_sha256) {
                // The re-promote door for a version the ledger knows: its
                // gate runs again over the weights the entry serves now.
                std::string doors;
                const auto door = [&doors](const std::string& line) {
                    if (!line.empty()) {
                        doors += (doors.empty() ? "" : ", or ") + line;
                    }
                };
                door(promote_again.empty()
                         ? std::string{}
                         : promote_again + " to promote its run again over these weights");
                door(point_back());
                door(promote_again.empty()
                         ? pin_all +
                               " to pin the set to these weights, if they are the ones "
                               "it was made for"
                         : std::string{});
                add(report, Status::Fail, "Adapters", label,
                    "pinned to other weights (sha256 " + pin + ") than the base " + base.string() +
                        " holds now (" + base_sha256->substr(0, models::kWeightIdLength) +
                        ") -- an adapter applies only over the weights it was trained on, so a "
                        "load refuses it",
                    doors);
                continue;
            }
            add(report, Status::Ok, "Adapters", label,
                "applies over " + models::weights_label(base) + " at scale " + scale.str() +
                    ", pinned to its weights (sha256 " + pin + ")");
        }
    }
}

}  // namespace

std::string_view to_string(Status status) noexcept {
    switch (status) {
        case Status::Ok:
            return "ok";
        case Status::Warn:
            return "warn";
        case Status::Fail:
            return "fail";
        case Status::Skipped:
            break;
    }
    return "skip";
}

std::size_t CheckReport::count(Status status) const noexcept {
    return static_cast<std::size_t>(std::count_if(
        rows.begin(), rows.end(), [status](const CheckRow& row) { return row.status == status; }));
}

/// Per-install secrets beside the config: the admin token today, the
/// credential store next. Never seeded -- each is created by the command that
/// needs it -- so install parity never sees them; but when one exists, it had
/// better be private.
void check_secrets(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_path.empty()) {
        return;
    }
    const std::filesystem::path token = httpserver::admin_token_path(inputs.config_path);
    const std::string label = "Admin token";
    std::error_code code;
    if (!std::filesystem::exists(token, code)) {
        add(report, Status::Ok, "Secrets", label,
            "not generated yet -- created at the first 'apogee serve'");
        return;
    }
    if (!harness::supports_private_modes()) {
        add(report, Status::Skipped, "Secrets", label,
            "mode check not applicable on this platform (no POSIX file modes)");
        return;
    }
    const std::filesystem::perms mode =
        std::filesystem::status(token, code).permissions() & std::filesystem::perms::mask;
    const bool leaks =
        (mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) !=
        std::filesystem::perms::none;
    if (leaks) {
        add(report, Status::Fail, "Secrets", label,
            "is readable by other users, and it is the /v1/admin bearer",
            "chmod 600 '" + token.string() + "'   (or: apogee check --fix)");
        return;
    }
    add(report, Status::Ok, "Secrets", label, "present, private (0600)");
}

/// The credential store, the same way: never seeded, private when present.
void check_credential_store(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_path.empty()) {
        return;
    }
    const std::filesystem::path file = secrets::credentials_path(inputs.config_path);
    const std::string label = "Credential store";
    std::error_code code;
    if (!std::filesystem::exists(file, code)) {
        add(report, Status::Ok, "Secrets", label, "none -- 'apogee auth add' creates it");
        return;
    }
    if (!harness::supports_private_modes()) {
        add(report, Status::Skipped, "Secrets", label,
            "mode check not applicable on this platform (no POSIX file modes)");
        return;
    }
    const std::filesystem::perms mode =
        std::filesystem::status(file, code).permissions() & std::filesystem::perms::mask;
    const bool leaks =
        (mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) !=
        std::filesystem::perms::none;
    if (leaks) {
        add(report, Status::Fail, "Secrets", label,
            "is readable by other users, and it holds provider API keys",
            "chmod 600 '" + file.string() + "'   (or: apogee check --fix)");
        return;
    }
    const secrets::CredentialStore store{file};
    const std::size_t slots = store.list().size();
    if (!store.warning().empty()) {
        add(report, Status::Warn, "Secrets", label, store.warning());
        return;
    }
    add(report, Status::Ok, "Secrets", label,
        "present, private (0600), " + std::to_string(slots) + " key(s) stored");
}

/// The native toolsets: the permission keys, the sandbox root, the toolset
/// switches, and whether `git` is there to be spawned. Warnings, never
/// failures -- a keyless, toolless install is valid.
void check_tools(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_missing || !inputs.config_error.empty()) {
        return;
    }
    const harness::Config& config = inputs.config;
    const std::span<const std::string_view> destructive = tools::destructive_tool_names();
    for (const auto& [tool, level] : config.permissions.levels) {
        const bool known =
            std::find(destructive.begin(), destructive.end(), tool) != destructive.end();
        const bool namespaced = tool.starts_with("mcp__");
        if (tool == agent::kFetchUrlToolName) {
            // The key someone reaches for first, and it does nothing: fetch_url
            // is decided per website, not per tool.
            add(report, Status::Warn, "Tools", "permissions." + tool,
                "has no effect: fetch_url asks per website",
                "list the websites it may reach without asking in tools.allowed_hosts");
            continue;
        }
        if (!known && !namespaced) {
            std::string names;
            for (const std::string_view name : destructive) {
                names += names.empty() ? "" : ", ";
                names += name;
            }
            add(report, Status::Warn, "Tools", "permissions." + tool,
                "not a native destructive tool (they are: " + names + ")",
                "remove the key, or check its spelling");
            continue;
        }
        add(report, Status::Ok, "Tools", "permissions." + tool,
            std::string{harness::to_string(level)});
    }
    for (const std::string_view tool : destructive) {
        if (!config.permissions.levels.contains(tool)) {
            add(report, Status::Ok, "Tools", "permissions." + std::string{tool},
                "ask (not listed; the default)");
        }
    }

    const tools::FsRoot root = tools::effective_fs_root(harness::expand_env(config.tools.fs_root));
    std::error_code code;
    if (!root.from_config) {
        add(report, Status::Ok, "Tools", "fs_root",
            root.path.string() + " -- unset, so the folder Apogee was started in");
    } else if (!std::filesystem::is_directory(root.path, code)) {
        add(report, Status::Warn, "Tools", "fs_root", root.path.string() + " is not a directory",
            "set tools.fs_root to an existing directory, or remove it");
    } else {
        add(report, Status::Ok, "Tools", "fs_root", root.path.string() + " -- from tools.fs_root");
    }

    if (config.tools.allowed_hosts.empty()) {
        add(report, Status::Ok, "Tools", "allowed_hosts",
            "none -- every website asks first, and a pipe or `serve` fetches nothing");
    }
    for (const std::string& host : config.tools.allowed_hosts) {
        if (harness::canonical_host(host).has_value()) {
            add(report, Status::Ok, "Tools", "allowed_hosts", host);
        } else {
            add(report, Status::Warn, "Tools", "allowed_hosts",
                "'" + host + "' is not a host name, so it allows nothing",
                "write the host alone, e.g. docs.python.org, not a URL or a pattern");
        }
    }

    // Configuration only: `check` never waits on the network, and a failing
    // instance is reported by the tool when used, with its error (25e).
    std::string problem;
    if (const std::optional<agent::SearchInstance> search =
            agent::search_instance(config.tools.search, problem);
        search.has_value()) {
        add(report, Status::Ok, "Tools", "search",
            search->provider + " at " + search->base.str() + ", " +
                std::to_string(search->results) + " results -- web_search is on, and " +
                search->base.host + " is reached without asking");
    } else if (!problem.empty()) {
        add(report, Status::Warn, "Tools", "search", problem + ", so there is no web_search",
            "fix tools.search in the config, or remove it");
    } else {
        add(report, Status::Ok, "Tools", "search",
            "not configured -- no web_search tool. To add one, run SearXNG with JSON on and "
            "add under tools: search: {provider: searxng, url: http://127.0.0.1:8888}");
    }

    const std::span<const std::string_view> toolsets = tools::toolset_names();
    for (const std::string& name : config.tools.disabled) {
        if (std::find(toolsets.begin(), toolsets.end(), name) == toolsets.end()) {
            std::string names;
            for (const std::string_view toolset : toolsets) {
                names += names.empty() ? "" : ", ";
                names += toolset;
            }
            add(report, Status::Warn, "Tools", "tools.disabled",
                "'" + name + "' is not a toolset (they are: " + names + ")");
        } else {
            add(report, Status::Ok, "Tools", "tools.disabled", name + " -- switched off");
        }
    }

    if (!config.tools.is_disabled("git")) {
        if (platform::find_on_path("git").empty()) {
            add(report, Status::Warn, "Tools", "git",
                "not found on PATH -- the git tools will fail",
                "install git, or add git to tools.disabled");
        } else {
            add(report, Status::Ok, "Tools", "git", "found on PATH");
        }
    }
}

/// Each enabled MCP server's command: a bare name is looked up on PATH; a
/// path must exist and be executable. `--fix` adds the execute bit and never
/// edits the config -- a dangling entry is the user's to delete.
/// A seeded driver an earlier Apogee wrote and nobody edited (27c): it runs,
/// but as that Apogee shipped it, and the fix replaces it in place.
[[nodiscard]] std::string stale_script_detail(const std::string& script) {
    return script +
           " is an earlier Apogee's copy, unedited -- 'apogee check --fix' brings it up to this "
           "build's";
}

/// The code-execution server (47b): the seeded script against the shipped
/// copy, then where it stands -- registered under which name, or the one
/// line that registers it with the interpreter it would run under, said
/// rather than guessed. Not registered is not a defect: it is optional.
void check_code_exec(CheckReport& report, const CheckInputs& inputs) {
    const operations::CodeExecRegistration registration =
        operations::code_exec_registration(inputs.home);
    const std::string script = registration.script.filename().string();
    const harness::SeededScript seeded =
        harness::inspect_seeded_script(inputs.home, script, inputs.retired_scripts);
    switch (seeded) {
        case harness::SeededScript::Missing:
            add(report, Status::Warn, "MCP", "code-exec",
                script + " is missing from " + registration.script.parent_path().string(),
                "apogee check --fix");
            return;
        case harness::SeededScript::Stale:
            add(report, Status::Warn, "MCP", "code-exec", stale_script_detail(script),
                "apogee check --fix");
            return;
        case harness::SeededScript::Edited:
        case harness::SeededScript::Current:
            break;
    }
    const bool edited = seeded == harness::SeededScript::Edited;
    const std::string copy = edited ? script + " differs from the shipped copy (your edit runs)"
                                    : script + " matches the shipped copy";
    if (const std::string name =
            operations::registered_code_exec_server(inputs.config, registration.script);
        !name.empty()) {
        add(report, edited ? Status::Warn : Status::Ok, "MCP", "code-exec",
            copy + "; registered as '" + name + "' (its tools: mcp__" + name +
                "__run_python, mcp__" + name + "__write_input)");
        return;
    }
    if (registration.interpreter.empty()) {
        add(report, Status::Skipped, "MCP", "code-exec",
            copy + "; not registered, and " + registration.missing, "apogee train setup");
        return;
    }
    add(report, Status::Skipped, "MCP", "code-exec",
        copy + "; not registered -- it would run under " + registration.interpreter.string() +
            " (" + registration.interpreter_source + ")",
        registration.mcp_create_line(operations::kCodeExecServerName));
}

void check_mcp(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_missing || !inputs.config_error.empty()) {
        return;
    }
    std::error_code code;
    for (const auto& [name, server] : inputs.config.mcp_servers) {
        const std::string label = "server: " + name;
        if (!server.enabled) {
            add(report, Status::Ok, "MCP", label, "disabled");
            continue;
        }
        if (server.command.empty()) {
            add(report, Status::Warn, "MCP", label, "no command set",
                "set mcp_servers." + name + ".command, or: apogee config delete-mcp-server " +
                    name);
            continue;
        }
        const bool bare = server.command.find('/') == std::string::npos &&
                          server.command.find('\\') == std::string::npos;
        if (bare) {
            if (platform::find_on_path(server.command).empty()) {
                add(report, Status::Fail, "MCP", label,
                    "command not found on PATH: " + server.command,
                    "install it, or: apogee config delete-mcp-server " + name);
            } else {
                add(report, Status::Ok, "MCP", label, server.command + " (on PATH)");
            }
            continue;
        }
        const std::filesystem::path command{server.command};
        if (!std::filesystem::exists(command, code)) {
            add(report, Status::Fail, "MCP", label, "command not found: " + server.command,
                "apogee config delete-mcp-server " + name);
            continue;
        }
        if (harness::supports_private_modes()) {
            const std::filesystem::perms mode =
                std::filesystem::status(command, code).permissions();
            if ((mode & std::filesystem::perms::owner_exec) == std::filesystem::perms::none) {
                add(report, Status::Warn, "MCP", label, server.command + " is not executable",
                    "chmod +x '" + server.command + "'   (or: apogee check --fix)");
                continue;
            }
        }
        add(report, Status::Ok, "MCP", label, server.command);
    }
    check_code_exec(report, inputs);
}

/// The `Xcode` section (56d): the two routes joining Apogee and Xcode, read
/// from Xcode's own command-line tools and said where a person sets them up.
/// `xcode` is the Xcode selected; `mcpbridge` whether it serves its tools to
/// Apogee's agents (Xcode 26.3+, `xcrun --find`) and under which server name
/// when one is registered; `acp agents` whether it hosts Apogee as its agent
/// (Xcode 26.6+, by version); `approval`, shown beside either, wall W3.
///
/// Every row is Ok or Skipped, never Warn or Fail: Xcode is optional, so a
/// fresh install with none stays green (M11). Off macOS the section is one
/// skipped row and nothing is asked. What `check` cannot see -- whether
/// Xcode lets external agents use its tools -- it says in words; it reads
/// none of Xcode's settings.
void check_xcode(CheckReport& report, const CheckInputs& inputs) {
    const auto row = [&report](Status status, std::string name, std::string detail,
                               std::string remedy = {}) {
        add(report, status, "Xcode", std::move(name), std::move(detail), std::move(remedy));
    };
    const std::string host =
        inputs.host_target.empty() ? std::string{platform::host_target()} : inputs.host_target;
    if (!host.starts_with("macos")) {
        row(Status::Skipped, "xcode", "Xcode runs on macOS only (this build is " + host + ")");
        return;
    }
    std::string error;
    const std::optional<platform::XcodeFacts> facts =
        inputs.xcode_probe ? inputs.xcode_probe(error)
                           : platform::xcode_facts(
                                 platform::CapturedLimits{.timeout = platform::kXcodeProbeTimeout,
                                                          .max_output = std::uint64_t{64} * 1024},
                                 error);
    if (!facts.has_value()) {
        row(Status::Skipped, "xcode", "could not ask Xcode's command-line tools -- " + error);
        return;
    }
    constexpr std::string_view kOnlyNeeded =
        " -- needed only to use Apogee from Xcode, or Xcode's tools from Apogee";
    const std::string select = facts->xcode_app.empty()
                                   ? std::string{}
                                   : "sudo xcode-select -s " + facts->xcode_app.string();
    switch (facts->install) {
        case platform::XcodeInstall::None:
            row(Status::Skipped, "xcode", "not installed" + std::string{kOnlyNeeded});
            return;
        case platform::XcodeInstall::Missing:
            row(Status::Skipped, "xcode",
                "the selected developer directory, " + facts->developer_dir.string() +
                    ", is not there" + std::string{kOnlyNeeded},
                select);
            return;
        case platform::XcodeInstall::CommandLineTools:
            row(Status::Skipped, "xcode",
                "only the Command Line Tools are selected (" + facts->developer_dir.string() +
                    "), not Xcode" + std::string{kOnlyNeeded},
                select);
            return;
        case platform::XcodeInstall::Xcode:
            break;
    }
    const std::string xcode =
        "Xcode " + facts->version + (facts->build.empty() ? "" : " (" + facts->build + ")");
    row(Status::Ok, "xcode", xcode + ", " + facts->developer_dir.string());

    const bool bridge = !facts->mcpbridge.empty();
    if (bridge) {
        const std::string registered = inputs.config_missing || !inputs.config_error.empty()
                                           ? std::string{}
                                           : operations::registered_xcode_server(inputs.config);
        row(Status::Ok, "mcpbridge",
            (registered.empty()
                 ? std::string{"Xcode's tools for Apogee's agents: apogee mcp create xcode --from "
                               "xcode"}
                 : "Xcode's tools for Apogee's agents: registered as '" + registered + "'") +
                " -- they answer while Xcode runs with the project open and its Settings › "
                "Intelligence › Model Context Protocol › \"Allow External Agents to Use Xcode "
                "Tools\" is on, which check cannot see");
    } else {
        row(Status::Skipped, "mcpbridge",
            xcode +
                " has no mcpbridge -- Xcode's tools for Apogee's agents need Xcode 26.3 or "
                "later");
    }

    const bool agents = platform::xcode_at_least(facts->version, 26, 6);
    if (agents) {
        row(Status::Ok, "acp agents",
            "Xcode " + facts->version + " hosts ACP agents -- add Apogee in " +
                std::string{operations::kAcpRegistrationWhere} +
                ": apogee acp registration prints the fields");
    } else {
        row(Status::Skipped, "acp agents",
            xcode + " hosts no ACP agents -- Apogee as Xcode's agent needs Xcode " +
                std::string{operations::kAcpXcodeMinimum} + " or later");
    }
    if (bridge || agents) {
        row(Status::Ok, "approval", std::string{operations::kXcodeApproval});
    }
}

/// The `Agents` section: the bundled assets present, and every configured
/// agent runnable -- its files there, its schema a schema, its model and
/// collection things the config knows.
void check_agents(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_missing || !inputs.config_error.empty()) {
        return;
    }
    std::error_code code;
    for (const harness::BundledAgent& bundled : harness::bundled_agents()) {
        for (const std::string& relative : {harness::bundled_prompt_relative_path(bundled.name),
                                            harness::bundled_schema_relative_path(bundled.name)}) {
            const std::filesystem::path path = inputs.home / relative;
            if (std::filesystem::exists(path, code)) {
                add(report, Status::Ok, "Agents", "bundled: " + relative, "present");
            } else {
                // Not a failure: the compiled-in text runs meanwhile. But an
                // edit made to a file that is not there goes nowhere.
                add(report, Status::Warn, "Agents", "bundled: " + relative,
                    "not seeded -- the compiled-in text is used until it is", "apogee check --fix");
            }
        }
    }
    const harness::Config& config = inputs.config;
    for (const auto& [name, agent] : config.agents) {
        const std::string label = "agent: " + name;
        bool clean = true;
        for (const std::filesystem::path& file : scaffold::agent_files(inputs.config_path, agent)) {
            if (!std::filesystem::exists(file, code)) {
                add(report, Status::Fail, "Agents", label, "file missing: " + file.string(),
                    "apogee agents create " + name + " --force   (or fix agents." + name +
                        ".prompts / .schemas)");
                clean = false;
            }
        }
        if (agent.prompts.empty()) {
            add(report, Status::Warn, "Agents", label, "no prompts: the agent has no persona",
                "apogee agents edit " + name);
            clean = false;
        }
        const std::filesystem::path home = harness::home_for_config(inputs.config_path);
        for (const std::string& schema : agent.schemas) {
            const std::filesystem::path path = harness::resolve_agent_path(home, schema);
            std::ifstream in(path, std::ios::binary);
            if (!in) {
                continue;  // reported above
            }
            std::ostringstream buffer;
            buffer << in.rdbuf();
            const nlohmann::json parsed = nlohmann::json::parse(buffer.str(), nullptr, false);
            if (parsed.is_discarded() || !parsed.is_object()) {
                add(report, Status::Fail, "Agents", label,
                    "schema is not a JSON object: " + path.string(), "apogee agents edit " + name);
                clean = false;
                continue;
            }
            const agentloop::ValidationResult valid = agentloop::validate_schema(parsed);
            if (!valid.ok) {
                add(report, Status::Fail, "Agents", label,
                    "schema is not a valid draft-07 JSON Schema: " + path.string() + " -- " +
                        valid.errors.front(),
                    "apogee agents edit " + name);
                clean = false;
            }
        }
        if (!agent.model.empty() && !names_a_configured_backend(config, agent.model)) {
            add(report, Status::Fail, "Agents", label,
                "model '" + agent.model + "' is not a configured backend",
                "apogee config add-backend " + agent.model + " --type <type>   (or change agents." +
                    name + ".model)");
            clean = false;
        }
        if (!agent.collection.empty() &&
            !std::filesystem::exists(collection_path(agent.collection), code)) {
            add(report, Status::Warn, "Agents", label,
                "collection '" + agent.collection + "' does not exist; the agent runs without it",
                "apogee embed ingest " + agent.collection + " <path>");
            clean = false;
        }
        for (const std::string& server : agent.mcp) {
            if (config.find_mcp_server(server) == nullptr) {
                add(report, Status::Warn, "Agents", label,
                    "mcp server '" + server + "' is not configured; it is skipped",
                    "apogee mcp create " + server);
                clean = false;
            }
        }
        if (clean) {
            add(report, Status::Ok, "Agents", label,
                std::string{harness::to_string(agent.tools)} + ", " +
                    std::to_string(agent.prompts.size()) + " prompt(s), " +
                    std::to_string(agent.schemas.size()) + " schema(s)");
        }
    }
}

/// The `Knowledge` section: the records collection readable with its text
/// index intact, and the raw archive private. Neither exists on a fresh
/// install, and that is fine -- the first capture creates both.
void check_knowledge(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_missing || !inputs.config_error.empty()) {
        return;
    }
    std::error_code code;
    const std::string db = inputs.config.knowledge.collection();
    // The collection lives under the embeddings row, like every other; the
    // path is spelled from the inspected home so a test tree stays hermetic.
    const std::filesystem::path collection = inputs.home / "embeddings" / (db + ".db");
    const std::string label = "collection: " + db;
    if (!std::filesystem::exists(collection, code)) {
        add(report, Status::Ok, "Knowledge", label,
            "no records yet -- 'apogee knowledge capture' creates it");
    } else {
        try {
            const knowledge::Store store{collection, {}};
            const std::size_t records = store.list().size();
            const std::string trouble = store.chunks().verify_index();
            if (trouble.empty()) {
                add(report, Status::Ok, "Knowledge", label,
                    std::to_string(records) + " record(s), text index ok");
            } else {
                add(report, Status::Fail, "Knowledge", label,
                    std::to_string(records) + " record(s), text index FAILED -- " + trouble);
            }
        } catch (const std::exception& e) {
            add(report, Status::Fail, "Knowledge", label, std::string{"unreadable -- "} + e.what());
        }
    }

    const std::filesystem::path raw = inputs.home / "knowledge" / "raw";
    const std::string archive = "raw archive";
    if (!std::filesystem::exists(raw, code)) {
        add(report, Status::Ok, "Knowledge", archive,
            "none archived yet -- created by the first capture");
        return;
    }
    if (!harness::supports_private_modes()) {
        add(report, Status::Skipped, "Knowledge", archive,
            "mode check not applicable on this platform (no POSIX file modes)");
        return;
    }
    const std::filesystem::perms mode =
        std::filesystem::status(raw, code).permissions() & std::filesystem::perms::mask;
    const bool leaks =
        (mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) !=
        std::filesystem::perms::none;
    if (leaks) {
        add(report, Status::Fail, "Knowledge", archive,
            "is readable by other users, and it holds raw conversations",
            "chmod 700 '" + raw.string() + "'   (or: apogee check --fix)");
        return;
    }
    std::size_t files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(raw, code)) {
        if (code) {
            break;
        }
        if (entry.is_regular_file(code)) {
            ++files;
        }
    }
    add(report, Status::Ok, "Knowledge", archive,
        "private (0700), " + std::to_string(files) + " conversation(s)");
}

/// The `Attachments` section (26d): the converters an attachment is read
/// with, which are optional and found on PATH, and the chats' indexes. The
/// indexes' folder is a layout row, so its mode is the filesystem check's.
/// Whether the backend `key` reads `medium`, from the config alone.
[[nodiscard]] bool reads(const harness::Config& config, const backends::MlxHost& host,
                         const std::string& key, Medium medium) {
    const harness::BackendConfig* backend = key.empty() ? nullptr : config.find_backend(key);
    return backend != nullptr && medium_gap(*backend, medium, host).reason.empty();
}

/// The model a helper role reads `medium` with for a chat on `chat`: the
/// role's own when it can, else the chat's when it can -- the rule the
/// attachments follow (26e).
[[nodiscard]] std::string helper_reading(const harness::Config& config,
                                         const backends::MlxHost& host, const std::string& chat,
                                         harness::ModelRole role, Medium medium) {
    const std::string key = harness::resolve_backend_key(
        config, harness::RoleRequest{.role = role, .conversation = chat});
    if (reads(config, host, key, medium)) {
        return key;
    }
    return reads(config, host, chat, medium) ? chat : std::string{};
}

/// Which models read images, audio and video for the default chat (26e):
/// as they are, and into text.
void check_media_readers(CheckReport& report, const CheckInputs& inputs, bool decoder) {
    if (inputs.config_missing || !inputs.config_error.empty()) {
        return;
    }
    const harness::Config& config = inputs.config;
    const backends::MlxHost host = mlx_host(inputs);
    const std::string chat = harness::resolve_chat_backend(config, "");
    const std::string describer =
        helper_reading(config, host, chat, harness::ModelRole::Vision, Medium::Image);
    const std::string transcriber =
        helper_reading(config, host, chat, harness::ModelRole::Transcription, Medium::Audio);
    const bool sees = reads(config, host, chat, Medium::Image);
    const bool hears = reads(config, host, chat, Medium::Audio);
    // A clip goes as its frames only to a local model with a vision
    // projector; a cloud one is sent its timeline.
    const harness::BackendConfig* chat_backend = chat.empty() ? nullptr : config.find_backend(chat);
    const bool clips =
        sees && chat_backend != nullptr && chat_backend->type == harness::BackendType::LlamaCpp;
    const std::string no_ffmpeg = decoder ? "" : " -- but ffmpeg is not installed";

    std::string images = describer.empty()
                             ? "nothing here reads them -- set a vision model with 'apogee config "
                               "set-default-vision'"
                             : "described by " + describer;
    if (sees) {
        images = chat + " sees them as they are; " + images;
    }
    add(report, Status::Ok, "Attachments", "images", images);

    std::string audio = transcriber.empty()
                            ? "nothing here hears it -- set a transcription model with 'apogee "
                              "config set-default-transcription'"
                            : "transcribed by " + transcriber;
    if (hears) {
        audio = chat + " hears up to a minute as it is; " + audio;
    }
    add(report, Status::Ok, "Attachments", "audio", audio + no_ffmpeg);

    std::string video;
    if (describer.empty() && transcriber.empty()) {
        video = "nothing here reads its frames or hears its sound";
    } else {
        video = "a timeline of its frames" + (describer.empty() ? "" : " by " + describer) +
                (transcriber.empty() ? ", its sound untranscribed"
                                     : " and its sound by " + transcriber);
    }
    if (clips) {
        video = chat + " reads up to a minute as its frames; " + video;
    }
    add(report, Status::Ok, "Attachments", "video", video + no_ffmpeg);
}

void check_attachments(CheckReport& report, const CheckInputs& inputs) {
    // Optional, as pdftotext is: without it, audio and video are refused by
    // name and the rest attaches as before (26e).
    const bool decoder = !platform::ffmpeg_path().empty() && !platform::ffprobe_path().empty();
    add(report, Status::Ok, "Attachments", "ffmpeg",
        decoder ? "found -- attached audio and video are decoded"
                : "not installed -- attached audio and video are refused (install ffmpeg, "
                  "with its ffprobe)");
    check_media_readers(report, inputs, decoder);
    add(report, Status::Ok, "Attachments", "pdftotext",
        embedstore::pdftotext_available()
            ? "found -- attached PDFs are read"
            : "not installed -- an attached PDF is skipped with that reason (install poppler)");
    add(report, Status::Ok, "Attachments", "git",
        platform::find_on_path("git").empty()
            ? "not installed -- an attached folder is walked without its .gitignore"
            : "found -- an attached folder inside a repository leaves out what git ignores");
    std::error_code code;
    const std::filesystem::path folder = inputs.home / "attachments";
    std::size_t indexes = 0;
    std::uintmax_t bytes = 0;
    if (std::filesystem::is_directory(folder, code)) {
        for (const auto& entry : std::filesystem::directory_iterator(folder, code)) {
            if (entry.is_regular_file(code)) {
                bytes += entry.file_size(code);
                indexes += entry.path().extension() == ".db" ? 1U : 0U;
            }
        }
    }
    add(report, Status::Ok, "Attachments", "chat indexes",
        indexes == 0 ? "none yet -- '/attach' in a chat creates its index"
                     : std::to_string(indexes) + " chat index(es), " +
                           std::to_string((bytes + (1024 * 1024) - 1) / (1024 * 1024)) +
                           " MB -- each deleted with its chat");
}

/// The `Memory` section (26l): recall's summaries -- how many chats, what
/// they take -- and that the index is its owner's alone, like the sessions it
/// is distilled from.
void check_memory(CheckReport& report, const CheckInputs& inputs) {
    std::error_code code;
    const std::filesystem::path index = inputs.home / "memory" / "chats.db";
    const bool recall =
        inputs.config_missing || !inputs.config_error.empty() || inputs.config.memory.recall;
    if (!std::filesystem::exists(index, code)) {
        add(report, Status::Ok, "Memory", "recall",
            recall ? "nothing summarised yet -- a chat of two turns or more is, when it ends"
                   : "off (memory.recall)");
        return;
    }
    const std::size_t chats = agentloop::RecallIndex{index, std::nullopt}.chats();
    const std::uintmax_t bytes = std::filesystem::file_size(index, code);
    const std::string counted = std::to_string(chats) + " chat(s) summarised, " +
                                format_progress_size(static_cast<std::int64_t>(bytes));
    const std::string off = recall ? "" : "; recall is off (memory.recall)";
    if (!harness::supports_private_modes()) {
        // Reported, not silently passed: see the Status::Skipped comment.
        add(report, Status::Skipped, "Memory", "recall",
            counted + " -- mode check not applicable on this platform (no POSIX file modes)" + off);
        return;
    }
    const std::filesystem::perms mode = std::filesystem::status(index, code).permissions();
    if ((mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) !=
        std::filesystem::perms::none) {
        add(report, Status::Fail, "Memory", "recall",
            "the summaries' index is readable by others: " + index.string(),
            "chmod 600 " + index.string());
        return;
    }
    add(report, Status::Ok, "Memory", "recall", counted + " -- private" + off);
}

/// The `Graph` section: every collection whose `graph:` block says anything
/// -- `extract_backend` must name a configured backend, and an enabled graph
/// should exist on disk. Hops and the entity cap are validated at load.
void check_graphs(CheckReport& report, const CheckInputs& inputs) {
    if (inputs.config_missing || !inputs.config_error.empty()) {
        return;
    }
    std::error_code code;
    for (const auto& [name, collection] : inputs.config.embeddings) {
        const harness::GraphConfig& graph = collection.graph;
        if (!graph.enabled && graph.extract_backend.empty()) {
            continue;
        }
        const std::string label = "graph: " + name;
        if (!graph.extract_backend.empty() &&
            inputs.config.find_backend(graph.extract_backend) == nullptr) {
            add(report, Status::Fail, "Graph", label,
                "extract_backend names a backend that is not configured: '" +
                    graph.extract_backend + "'",
                "set graph.extract_backend to a configured backend, or remove it to use the "
                "extraction role");
            continue;
        }
        std::string detail = "hops " + std::to_string(graph.hops) + ", max_entities " +
                             std::to_string(graph.max_entities) + ", extractor " +
                             (graph.extract_backend.empty() ? std::string{"(the extraction role)"}
                                                            : graph.extract_backend);
        const std::filesystem::path db = inputs.home / "embeddings" / (name + ".db");
        if (!std::filesystem::exists(db, code)) {
            add(report, graph.enabled ? Status::Warn : Status::Ok, "Graph", label,
                detail + "; no collection on disk yet",
                graph.enabled
                    ? "apogee embed ingest ... --db " + name + ", then apogee graph build " + name
                    : std::string{});
            continue;
        }
        try {
            const embedstore::Store store{db};
            const embedstore::GraphStats stats = store.graph_stats();
            if (!stats.built()) {
                add(report, graph.enabled ? Status::Warn : Status::Ok, "Graph", label,
                    detail + "; not built",
                    graph.enabled ? "apogee graph build " + name : std::string{});
                continue;
            }
            detail += "; built: " + std::to_string(stats.nodes) + " node(s), " +
                      std::to_string(stats.edges) + " edge(s)";
            if (stats.stale_files > 0) {
                detail += ", " + std::to_string(stats.stale_files) + " stale source(s)";
            }
            if (!graph.enabled) {
                detail += "; enabled: false -- retrieval does not expand through it";
            }
            add(report, Status::Ok, "Graph", label, detail);
        } catch (const std::exception& e) {
            add(report, Status::Fail, "Graph", label, std::string{"unreadable -- "} + e.what());
        }
    }

    // Every graphs: entry: the collision ban (a failure -- resolution is
    // graphs-first, so the collection's own graph becomes unreachable),
    // members, the extractor, and whether it is built. check never edits
    // config.
    for (const auto& [name, graph] : inputs.config.graphs) {
        const std::string label = "named graph: " + name;
        const std::filesystem::path collection_db = inputs.home / "embeddings" / (name + ".db");
        if (inputs.config.find_embedding(name) != nullptr ||
            std::filesystem::exists(collection_db, code)) {
            add(report, Status::Fail, "Graph", label,
                "collides with a collection name -- the collection's own graph becomes "
                "unreachable",
                "rename the graph: apogee config delete-graph " + name + ", then add-graph");
            continue;
        }
        if (graph.collections.empty() && graph.sources.empty()) {
            add(report, Status::Fail, "Graph", label, "no member collections or source trees",
                "apogee config add-graph " + name + " --collections <a,b> --force");
            continue;
        }
        // Source trees (27k): each a directory, every language vendored.
        bool sources_ok = true;
        for (const std::string& source : graph.sources) {
            if (!std::filesystem::is_directory(source, code)) {
                add(report, Status::Fail, "Graph", label,
                    "source tree '" + source + "' is not a directory",
                    "fix the path under graphs." + name + ".sources, or remove it");
                sources_ok = false;
            }
        }
        for (const std::string& language : graph.languages) {
            if (graph::code_language_by_name(language) == nullptr) {
                add(report, Status::Fail, "Graph", label,
                    "languages names '" + language + "', which no vendored grammar parses",
                    "use one of: " + graph::code_language_names());
                sources_ok = false;
            }
        }
        if (!sources_ok) {
            continue;
        }
        if (!graph.extract_backend.empty() &&
            inputs.config.find_backend(graph.extract_backend) == nullptr) {
            add(report, Status::Fail, "Graph", label,
                "extract_backend names a backend that is not configured: '" +
                    graph.extract_backend + "'",
                "set extract_backend to a configured backend, or remove it to use the "
                "extraction role");
            continue;
        }
        bool members_ok = true;
        for (const std::string& member : graph.collections) {
            const std::filesystem::path member_db = inputs.home / "embeddings" / (member + ".db");
            if (inputs.config.find_embedding(member) == nullptr &&
                !std::filesystem::exists(member_db, code)) {
                add(report, Status::Fail, "Graph", label,
                    "member collection '" + member + "' does not exist",
                    "apogee embed ingest " + member +
                        " <path>, or remove it from the graph's collections");
                members_ok = false;
            }
        }
        if (!members_ok) {
            continue;
        }
        std::string members;
        for (const std::string& member : graph.collections) {
            members += (members.empty() ? "" : ", ") + member;
        }
        std::string trees;
        for (const std::string& source : graph.sources) {
            trees += (trees.empty() ? "" : ", ") + source_member_label(source);
        }
        std::string detail = graph.collections.empty() ? std::string{} : "over [" + members + "]; ";
        if (!trees.empty()) {
            detail += "source trees [" + trees + "] (parsed, no model); ";
        }
        detail += "hops " + std::to_string(graph.hops) + ", max_entities " +
                  std::to_string(graph.max_entities);
        if (!graph.collections.empty()) {
            detail += ", extractor " + (graph.extract_backend.empty()
                                            ? std::string{"(the extraction role)"}
                                            : graph.extract_backend);
        }
        const std::filesystem::path db = inputs.home / "embeddings" / "graphs" / (name + ".db");
        if (std::filesystem::exists(db, code)) {
            add(report, Status::Ok, "Graph", label, detail + "; built");
        } else {
            add(report, Status::Ok, "Graph", label, detail + "; not yet built",
                "apogee graph build " + name);
        }
    }
}

/// The setup that installs `set` into an environment on Python `on` (M19's
/// rule): one its Python cannot take names the rebuild that can.
[[nodiscard]] std::string setup_remedy(training::RequirementSet set,
                                       const training::PythonVersion& on) {
    const std::string name{training::to_string(set)};
    return on.known() && !training::python_range(set).contains(on)
               ? "apogee train setup --rebuild --with " + name
               : "apogee train setup --with " + name;
}

/// One MLX set's row, once its package is there (M23).
struct SetRow {
    Status status = Status::Ok;
    std::string detail{};
    std::string remedy{};
};

/// An MLX set present in the environment, as its row says it (M23): the
/// version -- or, for one built from a commit, the commit -- its
/// distribution record states, against its floor or its pin, read from the
/// record's files alone. Below its floor or off its pin is a warning naming
/// the setup that brings it up; never a failure, since the older library
/// still runs what it ran. A record that states nothing comparable is said
/// as such and held to nothing. `declared` is the version the package's own
/// files declare, said where no record states one; `use` closes a row that
/// is not a warning.
[[nodiscard]] SetRow present_set_row(const training::InstalledSet& installed,
                                     const std::filesystem::path& venv, std::string_view declared,
                                     std::string_view use, const training::PythonVersion& on) {
    const training::SetRequirement& wants = installed.requirement;
    std::string shown = !installed.version.empty() ? installed.version : std::string{declared};
    if (wants.pin.has_value() && !installed.commit.empty()) {
        shown +=
            (shown.empty() ? "" : " ") + std::string{"at commit "} + installed.commit.substr(0, 12);
    }
    std::string detail = wants.package + " " + (shown.empty() ? "(version unstated)" : shown) +
                         " is present in " + venv.string() + " (its files, not imported by check)";
    const std::string against =
        wants.pin.has_value() ? "its pin, " + wants.describe() : "its floor, " + wants.describe();
    const std::string name{training::to_string(installed.set)};
    switch (installed.standing) {
        case training::SetStanding::Current:
            detail += wants.pin.has_value()
                          ? ", the commit Apogee pins (" + std::string{wants.pin->committed} + ")"
                          : ", at or above " + against;
            break;
        case training::SetStanding::BelowFloor:
            return {.status = Status::Warn,
                    .detail = detail + ", below " + against +
                              " -- kept from an earlier setup until the '" + name +
                              "' set is installed again",
                    .remedy = setup_remedy(installed.set, on)};
        case training::SetStanding::OffPin:
            return {.status = Status::Warn,
                    .detail = detail +
                              (installed.commit.empty()
                                   ? ", but its record names no commit it was built from, so "
                                     "it is not " +
                                         against
                                   : ", off " + against) +
                              " -- the one commit this Apogee trusts",
                    .remedy = setup_remedy(installed.set, on)};
        case training::SetStanding::Unstated:
            detail +=
                installed.version.empty()
                    ? "; its distribution record states no version, so it is not held to " + against
                    : "; its distribution record's version, '" + installed.version +
                          "', is not a release number, so it is not held to " + against;
            break;
        case training::SetStanding::NoRecord:
            detail += "; no distribution record of it is there, so it is not held to " + against;
            break;
    }
    return {.status = Status::Ok, .detail = detail + std::string{use}, .remedy = {}};
}

/// The MLX runtime (27a): whether this host can run an `mlx` backend at all,
/// and the seeded driver against its compiled-in copy. Off Apple silicon it
/// is skipped, never passed; an environment without mlx-lm is a warning only
/// where an `mlx` entry needs it. A pass says what was found -- the package's
/// files, never an import -- and nothing more. Since M23 every MLX set's row
/// says its version, or mlx-video's commit, against its floor or pin; and
/// the opt-in sets, mlx-video and mlx-audio, have rows of their own, a skip
/// while they are not installed, since nobody asked for them (M11).
void check_mlx(CheckReport& report, const CheckInputs& inputs) {
    const backends::MlxHost host = mlx_host(inputs);
    if (host.target != backends::kMlxTarget) {
        add(report, Status::Skipped, "MLX", "runtime",
            "MLX runs on Apple silicon macOS only (this build is " + host.target +
                ") -- llama.cpp is the local runtime here");
        return;
    }
    const bool wanted = !inputs.config_missing && inputs.config_error.empty() &&
                        std::ranges::any_of(inputs.config.backends, [](const auto& entry) {
                            return entry.second.type == harness::BackendType::Mlx;
                        });
    const backends::MlxReadiness runtime = backends::probe_mlx_runtime(host);
    // The Python the environment is on (M19), so a set it cannot take names
    // the rebuild -- read from its record and pyvenv.cfg, never asked.
    const training::PythonVersion on = training::PythonEnv{host.venv}.status().base_version;
    switch (runtime.refusal) {
        case backends::MlxRefusal::NoEnvironment:
        case backends::MlxRefusal::NoMlxLm:
            add(report, wanted ? Status::Warn : Status::Skipped, "MLX", "runtime",
                wanted ? runtime.reason
                       : "not set up -- only an mlx backend needs it (" + runtime.reason + ")",
                runtime.remedy);
            break;
        case backends::MlxRefusal::None:
        case backends::MlxRefusal::NoDriver: {
            const SetRow row = present_set_row(
                training::read_installed_set(host.venv, training::RequirementSet::Mlx), host.venv,
                runtime.version, "", on);
            add(report, row.status, "MLX", "runtime", row.detail, row.remedy);
            break;
        }
        case backends::MlxRefusal::Platform:
        case backends::MlxRefusal::NoModelPath:
        case backends::MlxRefusal::ModelMissing:
        case backends::MlxRefusal::NotAModelDirectory:
        case backends::MlxRefusal::NoMlxVlm:
        case backends::MlxRefusal::NoMlxEmbeddings:
        case backends::MlxRefusal::Drafter:
        case backends::MlxRefusal::Kind:
        case backends::MlxRefusal::Draft:
            break;  // the runtime's rungs never answer these
    }

    // The vision dependency (27c): mlx-vlm, which only a vision model needs.
    // Reported from its files, as mlx-lm is -- and a vision entry without it
    // is told where its images go instead, never claimed to see them.
    const backends::MlxVlmPackage vlm = backends::find_mlx_vlm(host);
    std::string sighted;
    if (wanted) {
        for (const auto& [name, backend] : inputs.config.backends) {
            if (backend.type == harness::BackendType::Mlx &&
                backends::inspect_mlx_model(
                    std::filesystem::path{harness::expand_env_and_home(backend.model_path)})
                    .vision) {
                sighted += (sighted.empty() ? "" : ", ") + name;
            }
        }
    }
    if (vlm.installed()) {
        const SetRow row = present_set_row(
            training::read_installed_set(host.venv, training::RequirementSet::MlxVlm), host.venv,
            vlm.version, " -- an MLX vision model reads images as they are", on);
        add(report, row.status, "MLX", "vision", row.detail, row.remedy);
    } else if (!sighted.empty()) {
        add(report, Status::Warn, "MLX", "vision",
            "mlx-vlm is not installed in " + host.venv.string() + ", so " + sighted +
                " cannot read images as they are -- the vision role describes them instead, "
                "when one is set",
            std::string{backends::kMlxVisionRemedy});
    } else {
        add(report, Status::Skipped, "MLX", "vision",
            "not set up -- only an MLX vision model needs it (mlx-vlm is not installed in " +
                host.venv.string() + ")",
            std::string{backends::kMlxVisionRemedy});
    }

    // The embedding dependency (53h): mlx-embeddings, which only an MLX
    // embedding model needs -- reported from its files, as mlx-vlm is, and an
    // embedding entry without it said to embed nothing.
    const backends::MlxEmbeddingsPackage embeddings = backends::find_mlx_embeddings(host);
    std::string embedders;
    if (wanted) {
        for (const auto& [name, backend] : inputs.config.backends) {
            if (backends::mlx_entry_embeds(backend)) {
                embedders += (embedders.empty() ? "" : ", ") + name;
            }
        }
    }
    if (embeddings.installed()) {
        const SetRow row = present_set_row(
            training::read_installed_set(host.venv, training::RequirementSet::MlxEmbeddings),
            host.venv, embeddings.version, " -- an MLX embedding model embeds through it", on);
        add(report, row.status, "MLX", "embeddings", row.detail, row.remedy);
    } else if (!embedders.empty()) {
        add(report, Status::Warn, "MLX", "embeddings",
            "mlx-embeddings is not installed in " + host.venv.string() + ", so " + embedders +
                " cannot embed",
            std::string{backends::kMlxEmbeddingsRemedy});
    } else {
        add(report, Status::Skipped, "MLX", "embeddings",
            "not set up -- only an MLX embedding model needs it (mlx-embeddings is not installed "
            "in " +
                host.venv.string() + ")",
            std::string{backends::kMlxEmbeddingsRemedy});
    }

    // The opt-in sets (M23): mlx-video, built from its pinned commit (53k),
    // and mlx-audio (53m), each found by its package's files and held to its
    // pin or floor. Absent is a skip and never a warning -- nobody asked for
    // either, and `check --fix` installs neither -- so a fresh install stays
    // green (M11).
    for (const auto& [set, label, package, use] :
         {std::tuple{training::RequirementSet::MlxVideo, "video", "mlx_video",
                     "'models convert --mlx' of a Wan set and 'video generate' on MLX"},
          std::tuple{training::RequirementSet::MlxAudio, "voice", "mlx_audio",
                     "'models convert --mlx' of a voice model and 'voice say' on MLX"}}) {
        if (const std::filesystem::path found = training::find_site_package(host.venv, package);
            !found.empty()) {
            const SetRow row =
                present_set_row(training::read_installed_set(host.venv, set), host.venv,
                                training::installed_package_version(found),
                                " -- " + std::string{use} + " run through it", on);
            add(report, row.status, "MLX", label, row.detail, row.remedy);
            continue;
        }
        const training::SetRequirement wants = training::set_requirement(set);
        const training::PythonRange range = training::python_range(set);
        add(report, Status::Skipped, "MLX", label,
            "not installed (opt-in) -- only " + std::string{use} + " need it; " +
                (wants.pin.has_value() ? "pip builds " + wants.package +
                                             " from GitHub at its pin, " + wants.describe()
                                       : wants.package + "'s floor is " + wants.describe()) +
                (on.known() && !range.contains(on)
                     ? "; the Python environment is on Python " + on.str() + ", and the '" +
                           std::string{training::to_string(set)} + "' set runs on " +
                           range.describe()
                     : std::string{}),
            setup_remedy(set, on));
    }

    // The seeded drivers: the backend's, `convert --mlx`'s (27b), and the
    // embedding backend's (53h).
    for (const auto& [label, file] :
         {std::pair<std::string, std::filesystem::path>{"driver", host.driver},
          std::pair<std::string, std::filesystem::path>{
              "conversion driver", inputs.home / harness::bundled_mlx_converter_relative_path()},
          std::pair<std::string, std::filesystem::path>{"embedding driver", host.embedder}}) {
        const std::string script = file.filename().string();
        switch (harness::inspect_seeded_script(inputs.home, script, inputs.retired_scripts)) {
            case harness::SeededScript::Missing:
                add(report, Status::Warn, "MLX", label,
                    script + " is missing from " + file.parent_path().string(),
                    "apogee check --fix");
                break;
            case harness::SeededScript::Current:
                add(report, Status::Ok, "MLX", label, script + " matches the shipped copy");
                break;
            case harness::SeededScript::Stale:
                add(report, Status::Warn, "MLX", label, stale_script_detail(script),
                    "apogee check --fix");
                break;
            case harness::SeededScript::Edited:
                add(report, Status::Warn, "MLX", label,
                    script +
                        " differs from the shipped copy -- your edit is kept and runs; delete the "
                        "file and run 'apogee check --fix' to restore the shipped one");
                break;
        }
    }
}

/// The command that brings an ffmpeg with `ability` on `host` (52e): a
/// package that carries it, runnable as written.
[[nodiscard]] std::string ffmpeg_remedy(std::string_view host, std::string_view ability) {
    if (host.starts_with("macos")) {
        // Homebrew's ffmpeg carries x264 and no libass; its keg-only
        // ffmpeg-full carries both (2026-10-20).
        return ability == "x264" ? "brew install ffmpeg" : remedy_line(macos_captions_remedy());
    }
    if (host.starts_with("windows")) {
        return "winget install Gyan.FFmpeg";
    }
    return "sudo apt install ffmpeg";
}

/// What the ffmpeg on PATH can do for a video (52e): whether a
/// composition's captions burn into its frames (the `subtitles` filter,
/// libass) and whether its clips encode as H.264 (`libx264`), read from
/// ffmpeg's own listings through the seam's one child. ffmpeg missing, or
/// not answering, skips both -- never a pass. A missing ability is a skip
/// too, with the command that brings it: an optional ability, as pdftotext
/// is, so a fresh install still ends green (M11) -- a composition works
/// without either, in a lesser form.
void check_ffmpeg_abilities(CheckReport& report, const CheckInputs& inputs) {
    const std::string host =
        inputs.host_target.empty() ? std::string{platform::host_target()} : inputs.host_target;
    constexpr std::string_view kCaptions = "ffmpeg captions";
    constexpr std::string_view kH264 = "ffmpeg h264";
    if (!inputs.ffmpeg_listing && platform::ffmpeg_path().empty()) {
        for (const std::string_view name : {kCaptions, kH264}) {
            add(report, Status::Skipped, "Video", std::string{name},
                "ffmpeg is not installed -- video generate and compose need it to assemble",
                ffmpeg_remedy(host, name == kH264 ? "x264" : "libass"));
        }
        return;
    }
    const auto ask = [&inputs](std::string_view flag, std::string& error) {
        if (inputs.ffmpeg_listing) {
            return inputs.ffmpeg_listing(flag, error);
        }
        return platform::ffmpeg_listing(flag, {}, error);
    };
    std::string error;
    if (const std::optional<std::string> filters = ask("-filters", error); !filters.has_value()) {
        add(report, Status::Skipped, "Video", std::string{kCaptions},
            "could not ask ffmpeg for its filters -- " + error);
    } else if (platform::filter_listed(*filters, "subtitles")) {
        add(report, Status::Ok, "Video", std::string{kCaptions},
            "libass -- a composition burns its captions into the frames");
    } else {
        add(report, Status::Skipped, "Video", std::string{kCaptions},
            "no subtitles filter (this ffmpeg was built without libass) -- a composition keeps its "
            "captions beside the video as an SRT",
            ffmpeg_remedy(host, "libass"));
    }
    error.clear();
    if (const std::optional<std::string> encoders = ask("-encoders", error);
        !encoders.has_value()) {
        add(report, Status::Skipped, "Video", std::string{kH264},
            "could not ask ffmpeg for its encoders -- " + error);
    } else if (platform::encoder_listed(*encoders, "libx264")) {
        add(report, Status::Ok, "Video", std::string{kH264},
            "libx264 -- clips and compositions encode as H.264");
    } else {
        add(report, Status::Skipped, "Video", std::string{kH264},
            "no libx264 encoder -- clips encode as MPEG-4 Part 2, larger and less widely played",
            ffmpeg_remedy(host, "x264"));
    }
}

/// The video runtime (49b): torch and diffusers in the environment Apogee
/// owns, read from their files (not set up is a skip naming the set, as the
/// MLX rows are), and the seeded driver against its compiled-in copy.
void check_video(CheckReport& report, const CheckInputs& inputs) {
    const video::VideoHost host = video::VideoHost::at(inputs.home);
    const video::VideoReadiness ready = video::probe_video(host);
    switch (ready.refusal) {
        case video::VideoRefusal::NoEnvironment:
        case video::VideoRefusal::NoTorch:
        case video::VideoRefusal::NoDiffusers:
            add(report, Status::Skipped, "Video", "runtime", "not set up -- " + ready.reason,
                ready.remedy);
            break;
        default: {
            const std::string found =
                "torch " +
                (ready.torch_version.empty() ? "(version unstated)" : ready.torch_version) +
                " and diffusers " +
                (ready.diffusers_version.empty() ? "(version unstated)" : ready.diffusers_version) +
                " in " + host.venv.string() + " (their files, not imported by check)";
            if (ready.refusal == video::VideoRefusal::NoFfmpeg) {
                add(report, Status::Warn, "Video", "runtime", found + ", but " + ready.reason,
                    ready.remedy);
            } else {
                add(report, Status::Ok, "Video", "runtime", found);
            }
            break;
        }
    }
    // The speech runtime (50a): the same reading of files, its own set.
    const video::VideoReadiness voice = video::probe_voice(host);
    switch (voice.refusal) {
        case video::VideoRefusal::NoEnvironment:
        case video::VideoRefusal::NoTorch:
        case video::VideoRefusal::NoKokoro:
            // The probe's own remedy: on a Python the set does not run on,
            // the rebuild (M19), else the install.
            add(report, Status::Skipped, "Video", "voice runtime", "not set up -- " + voice.reason,
                voice.remedy);
            break;
        default:
            add(report, Status::Ok, "Video", "voice runtime",
                "torch " +
                    (voice.torch_version.empty() ? "(version unstated)" : voice.torch_version) +
                    " and kokoro " +
                    (voice.kokoro_version.empty() ? "(version unstated)" : voice.kokoro_version) +
                    " in " + host.venv.string() + " (their files, not imported by check)");
            break;
    }
    // The seeded drivers: the video one (49b), the speech one (50a), the
    // MLX engine's video one (53l) and its speech one (53m).
    for (const auto& [label, file] :
         {std::pair<std::string, std::filesystem::path>{"driver", host.driver},
          std::pair<std::string, std::filesystem::path>{"voice driver", host.voice_driver},
          std::pair<std::string, std::filesystem::path>{"mlx driver", host.mlx_driver},
          std::pair<std::string, std::filesystem::path>{"mlx voice driver",
                                                        host.voice_mlx_driver}}) {
        const std::string script = file.filename().string();
        switch (harness::inspect_seeded_script(inputs.home, script, inputs.retired_scripts)) {
            case harness::SeededScript::Missing:
                add(report, Status::Warn, "Video", label,
                    script + " is missing from " + file.parent_path().string(),
                    "apogee check --fix");
                break;
            case harness::SeededScript::Current:
                add(report, Status::Ok, "Video", label, script + " matches the shipped copy");
                break;
            case harness::SeededScript::Stale:
                add(report, Status::Warn, "Video", label, stale_script_detail(script),
                    "apogee check --fix");
                break;
            case harness::SeededScript::Edited:
                add(report, Status::Warn, "Video", label,
                    script +
                        " differs from the shipped copy -- your edit is kept and runs; delete the "
                        "file and run 'apogee check --fix' to restore the shipped one");
                break;
        }
    }
}

/// The training track's install: the Python environment (created only on
/// request, so its absence is a warning with the command that creates it),
/// every seeded script against its compiled-in copy (a user's edit is kept
/// and shown, a missing one is a failure `--fix` repairs), every installed
/// kit parsing and validating, and `paths.hf_dir` when it is set.
void check_training(CheckReport& report, const CheckInputs& inputs) {
    std::error_code code;
    const std::filesystem::path training = inputs.home / "training";

    const training::PythonEnv env{training / "venv"};
    const training::PythonEnvStatus status = env.status();
    if (!status.exists) {
        // Not set up is not a defect (M11): the environment is created on
        // request, like the MLX runtime rows beside it -- a skip with its
        // command, so a fresh install reports zero warnings.
        add(report, Status::Skipped, "Training", "python env",
            "not set up -- only 'datasets prepare' and the trainers need it (never the system "
            "Python)",
            "apogee train setup");
    } else if (!status.error.empty()) {
        add(report, Status::Warn, "Training", "python env",
            "exists at " + env.dir().string() + " but its record is unreadable: " + status.error,
            "apogee train setup");
    } else {
        std::string sets;
        for (const std::string& set : status.sets) {
            sets += sets.empty() ? set : ", " + set;
        }
        add(report, Status::Ok, "Training", "python env",
            "at " + env.dir().string() +
                (status.base_version.known() ? " (Python " + status.base_version.str() + ")"
                                             : std::string{}) +
                (sets.empty() ? std::string{"; no requirement sets installed"}
                              : "; sets: " + sets));
    }

    for (const harness::BundledScript& script : harness::bundled_training_scripts()) {
        const std::filesystem::path path =
            inputs.home / harness::bundled_script_relative_path(script.name);
        const std::string label = "script: " + std::string{script.name};
        switch (harness::inspect_seeded_script(inputs.home, script.name, inputs.retired_scripts)) {
            case harness::SeededScript::Missing:
                // A warning, not a failure: a fresh install passes, and the
                // remedy is the doctor's own repair.
                add(report, Status::Warn, "Training", label,
                    "missing from " + path.parent_path().string(), "apogee check --fix");
                break;
            case harness::SeededScript::Current:
                add(report, Status::Ok, "Training", label, "matches the shipped copy");
                break;
            case harness::SeededScript::Stale:
                add(report, Status::Warn, "Training", label,
                    stale_script_detail(std::string{script.name}), "apogee check --fix");
                break;
            case harness::SeededScript::Edited:
                add(report, Status::Warn, "Training", label,
                    "differs from the shipped copy -- your edit is kept and runs; delete the file "
                    "and run 'apogee check --fix' to restore the shipped one");
                break;
        }
    }

    // The vendored converter: one row over the whole seeded tree, since a
    // driver's worth of rows per file would drown the section.
    {
        const std::filesystem::path converter =
            inputs.home / harness::bundled_converter_relative_dir();
        const std::size_t files = harness::bundled_converter_files().size();
        if (!std::filesystem::is_directory(converter, code)) {
            add(report, Status::Warn, "Training", "converter",
                "convert_hf_to_gguf.py is not seeded under " + converter.string(),
                "apogee check --fix");
        } else if (const harness::ConverterTreeState tree =
                       harness::inspect_converter_tree(converter);
                   tree.current()) {
            add(report, Status::Ok, "Training", "converter",
                "convert_hf_to_gguf.py matches the vendored copy (" + std::to_string(files) +
                    " files)");
        } else if (tree.stale > 0 || tree.missing > 0) {
            // An earlier Apogee's converter, or part of one: the fix updates
            // it in place, and an edit elsewhere in the tree is kept. Stale,
            // it is a failure waiting to happen -- its llama.cpp is older
            // than the runtime's, and a model only the new one knows fails.
            add(report, Status::Warn, "Training", "converter",
                (tree.stale > 0 ? std::to_string(tree.stale) +
                                      " file(s) are from an earlier Apogee's llama.cpp"
                                : std::to_string(tree.missing) + " of the vendored converter's " +
                                      std::to_string(files) + " files are missing") +
                    " under " + converter.string(),
                "apogee check --fix");
        } else {
            add(report, Status::Warn, "Training", "converter",
                "differs from the vendored copy under " + converter.string() +
                    " -- delete the directory and run 'apogee check --fix' to restore it");
        }
    }

    // The trainer this host would use, and what promote needs -- both only
    // meaningful once the environment exists (its own row says when not).
    if (status.exists && status.error.empty()) {
        const training::TrainerChoice choice = training::select_trainer(
            inputs.config_missing ? std::string{} : inputs.config.training.trainer,
            training::detect_host());
        if (choice.name.empty()) {
            add(report, Status::Warn, "Training", "trainer", choice.error);
        } else if (choice.name == "mock") {
            add(report, Status::Ok, "Training", "trainer", "mock (training.trainer)");
        } else {
            const training::RequirementSet set = choice.name == "mlx"
                                                     ? training::RequirementSet::Mlx
                                                     : training::RequirementSet::Peft;
            if (status.has(set)) {
                add(report, Status::Ok, "Training", "trainer",
                    choice.name + " -- its requirement set is installed");
            } else {
                add(report, Status::Warn, "Training", "trainer",
                    choice.name + " fits this host, but its requirement set is not installed",
                    "apogee train setup --trainer " + choice.name);
            }
        }
        if (status.has(training::RequirementSet::Convert)) {
            add(report, Status::Ok, "Training", "convert set",
                "installed -- 'train promote' can convert to GGUF");
        } else {
            add(report, Status::Warn, "Training", "convert set",
                "not installed -- 'train promote' needs it to convert to GGUF",
                "apogee train setup --with convert");
        }
    }

    const std::vector<training::KitSummary> kits = training::list_kits(training / "kits");
    if (kits.empty()) {
        add(report, Status::Warn, "Training", "kits",
            "none installed under " + (training / "kits").string(), "apogee check --fix");
    }
    for (const training::KitSummary& kit : kits) {
        const std::string label = "kit: " + kit.name;
        if (!kit.error.empty()) {
            add(report, Status::Fail, "Training", label, kit.error,
                "fix " + kit.path.string() + ", or delete it");
            continue;
        }
        add(report, Status::Ok, "Training", label,
            std::to_string(kit.eval_items) + " eval item(s)" +
                (kit.description.empty() ? std::string{} : " -- " + kit.description));
    }

    if (inputs.config_missing || !inputs.config_error.empty()) {
        return;
    }

    // Every version ledger: the active version's GGUF -- or MLX directory
    // (27c) -- exists and the backend's model_path names it -- or, for an
    // adapter version (53b), its adapter exists and the backend's adapters
    // name it -- else the two have drifted, and a chat runs something other
    // than what `train versions` says is active.
    for (const training::VersionLedger& ledger : training::TrainingStore{training}.all_versions()) {
        const std::string label = "versions: " + ledger.backend;
        const training::VersionEntry* active = ledger.active();
        if (active == nullptr) {
            add(report, Status::Warn, "Training", label,
                "the ledger names active version " + std::to_string(ledger.active_version) +
                    ", which it does not hold",
                "apogee train promote <run> --as " + ledger.backend);
            continue;
        }
        const bool there = active->mlx()
                               ? std::filesystem::is_directory(active->weights(), code)
                               : std::filesystem::is_regular_file(active->weights(), code);
        const std::string kind = active->adapter() ? "an adapter"
                                 : active->mlx()   ? "an MLX model"
                                                   : "a GGUF";
        if (!there) {
            add(report, Status::Warn, "Training", label,
                "active v" + std::to_string(active->version) + " names " + kind +
                    " that is not there: " + active->weights(),
                "apogee train rollback " + ledger.backend + ", or promote again");
            continue;
        }
        const harness::BackendConfig* backend = inputs.config.find_backend(ledger.backend);
        if (backend == nullptr) {
            add(report, Status::Warn, "Training", label,
                "active v" + std::to_string(active->version) +
                    " exists, but no backend of that name is configured",
                "apogee config add-backend " + ledger.backend + " --type " +
                    (active->mlx() ? "mlx" : "llamacpp") +
                    (active->adapter() ? " --model-path <base> && apogee config set-backend " +
                                             ledger.backend + " --adapters "
                                       : std::string{" --model-path "}) +
                    active->weights());
            continue;
        }
        if (active->adapter()) {
            // 53b: the base is the entry's own; the version is an adapter on it.
            const bool applied =
                std::ranges::any_of(backend->adapters, [&](const harness::AdapterConfig& adapter) {
                    return std::filesystem::path{adapter.path}.lexically_normal() ==
                           std::filesystem::path{active->adapter_path}.lexically_normal();
                });
            if (!applied) {
                add(report, Status::Warn, "Training", label,
                    "the ledger says v" + std::to_string(active->version) + " (" +
                        active->weights() + ") but the backend's adapters do not name it",
                    "apogee train rollback " + ledger.backend + ", or promote again");
                continue;
            }
            add(report, Status::Ok, "Training", label,
                "active v" + std::to_string(active->version) + " of " +
                    std::to_string(ledger.versions.size()) + ", and the backend applies it");
            continue;
        }
        if (harness::expand_env_and_home(backend->model_path) != active->weights()) {
            add(report, Status::Warn, "Training", label,
                "the ledger says v" + std::to_string(active->version) + " (" + active->weights() +
                    ") but the backend's model_path is " + backend->model_path,
                "apogee train rollback " + ledger.backend + ", or promote again");
            continue;
        }
        add(report, Status::Ok, "Training", label,
            "active v" + std::to_string(active->version) + " of " +
                std::to_string(ledger.versions.size()) + ", and the backend points at it");
    }

    if (!inputs.config.paths.hf_dir.empty()) {
        const std::filesystem::path hf_dir{
            harness::expand_env_and_home(inputs.config.paths.hf_dir)};
        if (!std::filesystem::is_directory(hf_dir, code)) {
            add(report, Status::Warn, "Training", "paths.hf_dir",
                "names a directory that does not exist: " + hf_dir.string(),
                "create it, or clear paths.hf_dir so snapshots land under models/");
        } else {
            add(report, Status::Ok, "Training", "paths.hf_dir",
                "SafeTensors snapshots land under " + hf_dir.string());
        }
    }

    // Every named pipeline: its student a snapshot and each stage's dataset
    // present -- warnings, since a cycle's pipeline has its datasets
    // overridden and a student may be pulled later; a stage that names
    // nothing at all already failed the load.
    const models::StoreRoots roots = check_roots(inputs);
    for (const auto& [name, spec] : inputs.config.training.pipelines) {
        const std::string label = "pipeline: " + name;
        std::vector<std::string> problems;
        if (spec.student.empty()) {
            problems.emplace_back("no student named");
        } else if (const SnapshotChoice student = choose_snapshot(roots, spec.student);
                   !student.error.empty()) {
            // The same lookup `train` makes, so this row and a run agree.
            problems.push_back("student '" + spec.student + "': " + student.error);
        }
        for (const harness::PipelineStageSpec& stage : spec.stages) {
            const std::filesystem::path given{stage.dataset};
            const bool found = std::filesystem::is_regular_file(given, code) ||
                               std::filesystem::is_regular_file(
                                   training / "datasets" / (stage.dataset + ".jsonl"), code);
            if (!found) {
                problems.push_back("stage '" + stage.name + "' names dataset '" + stage.dataset +
                                   "', which is neither a file nor under training/datasets/");
            }
        }
        if (problems.empty()) {
            add(report, Status::Ok, "Training", label,
                std::to_string(spec.stages.size()) + " stage(s) over " + spec.student);
            continue;
        }
        std::string detail;
        for (const std::string& problem : problems) {
            detail += (detail.empty() ? "" : "; ") + problem;
        }
        add(report, Status::Warn, "Training", label, detail,
            "apogee models pull <owner>/<repo> --safetensors, or apogee datasets list");
    }

    // The cycle: what it names must exist, a halted loop is reported with
    // the way back, and a running one is visible.
    const harness::CycleConfig& cycle = inputs.config.training.cycle;
    if (!cycle.pipeline.empty() || !cycle.backend.empty() || !cycle.sources.empty()) {
        if (!cycle.configured()) {
            add(report, Status::Fail, "Training", "cycle",
                "training.cycle needs all three of pipeline, backend and sources",
                "complete the training.cycle block, or remove it");
        } else {
            const bool named = inputs.config.training.pipelines.contains(cycle.pipeline);
            const bool file = std::filesystem::is_regular_file(cycle.pipeline, code);
            if (!named && !file) {
                add(report, Status::Fail, "Training", "cycle",
                    "names pipeline '" + cycle.pipeline +
                        "', which is neither a training.pipelines entry nor a spec file",
                    "add it under training.pipelines, or point training.cycle.pipeline at a file");
            } else {
                const harness::BackendConfig* backend = inputs.config.find_backend(cycle.backend);
                if (backend != nullptr && backend->type != harness::BackendType::LlamaCpp) {
                    add(report, Status::Fail, "Training", "cycle",
                        "promotes into '" + cycle.backend + "', which is a " +
                            std::string{harness::to_string(backend->type)} +
                            " backend, not a llamacpp one",
                        "name a new backend, or an existing llamacpp entry");
                } else {
                    std::string sources;
                    for (const harness::CycleSourceConfig& source : cycle.sources) {
                        sources += (sources.empty() ? "" : "+") + source.type;
                    }
                    add(report, Status::Ok, "Training", "cycle",
                        "pipeline '" + cycle.pipeline + "' -> " + cycle.backend +
                            (backend == nullptr ? " (created by the first passing cycle)" : "") +
                            "; sources " + sources + "; circuit_breaker_k " +
                            std::to_string(cycle.circuit_breaker_k));
                }
            }
        }
    }
    const std::filesystem::path cycle_dir = training / "cycle";
    if (training::history_exists(cycle_dir)) {
        std::string error;
        const training::CycleHistory history =
            training::load_history(cycle_dir, cycle.backend, error);
        if (!error.empty()) {
            add(report, Status::Warn, "Training", "cycle history", "unreadable: " + error,
                "fix or remove " + cycle_dir.string() + "/history.json");
        } else if (history.halted) {
            add(report, Status::Warn, "Training", "cycle history",
                "the cycle is HALTED: " + history.halt_reason + " (" +
                    std::to_string(history.consecutive_fails) + " consecutive failure(s))",
                "apogee train cycle resume");
        } else {
            add(report, Status::Ok, "Training", "cycle history",
                std::to_string(history.total_runs) + " run(s), " +
                    std::to_string(history.consecutive_fails) + " consecutive failure(s)" +
                    (history.anchor_version > 0
                         ? ", anchor v" + std::to_string(history.anchor_version)
                         : std::string{", no anchor yet"}) +
                    (training::cycle_lock_held(cycle_dir) ? "; a cycle is running now" : ""));
        }
    }
}

CheckReport run_checks(const CheckInputs& inputs) {
    CheckReport report;
    say(inputs, "checking the version");
    check_version(report, inputs);
    say(inputs, "checking config");
    check_config(report, inputs);
    say(inputs, "checking adapters");
    check_adapters(report, inputs);
    say(inputs, "checking providers");
    check_providers(report, inputs);
    say(inputs, "checking tools");
    check_tools(report, inputs);
    say(inputs, "checking MCP servers");
    check_mcp(report, inputs);
    say(inputs, "checking Xcode");
    check_xcode(report, inputs);
    say(inputs, "checking agents");
    check_agents(report, inputs);
    say(inputs, "checking knowledge");
    check_knowledge(report, inputs);
    say(inputs, "checking attachments");
    check_attachments(report, inputs);
    check_memory(report, inputs);
    say(inputs, "checking graphs");
    check_graphs(report, inputs);
    say(inputs, "checking training");
    check_training(report, inputs);
    say(inputs, "checking MLX");
    check_mlx(report, inputs);
    say(inputs, "checking video");
    check_video(report, inputs);
    check_ffmpeg_abilities(report, inputs);
    say(inputs, "checking the data directory");
    check_filesystem(report, inputs);
    say(inputs, "checking secrets");
    check_secrets(report, inputs);
    check_credential_store(report, inputs);
    say(inputs, "checking models");
    check_models(report, inputs);
    return report;
}

/// Tightens a secret file's mode. Returns what it did, or nothing.
std::string fix_secret_mode(const std::filesystem::path& file) {
    std::error_code code;
    if (!harness::supports_private_modes() || !std::filesystem::exists(file, code)) {
        return {};
    }
    const std::filesystem::perms mode =
        std::filesystem::status(file, code).permissions() & std::filesystem::perms::mask;
    if ((mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) ==
        std::filesystem::perms::none) {
        return {};
    }
    std::filesystem::permissions(
        file, std::filesystem::perms::owner_read | std::filesystem::perms::owner_write,
        std::filesystem::perm_options::replace, code);
    if (code) {
        return "could not set the mode of " + file.string() + ": " + code.message();
    }
    return "set " + file.string() + " to 0600";
}

std::vector<std::string> apply_fixes(const CheckInputs& inputs) {
    // Delegates to the ONE seeding implementation rather than walking the rows
    // itself. It used to walk them, which meant two pieces of code knew how to
    // build the tree -- and a deliberate drift injected into the other one was
    // not caught by anything, because the copy the installers actually reached
    // was still correct. Two implementations is the bug, even when both agree.
    //
    // An EXISTING config is still untouched, whatever its format or state:
    // repairing the local install is a repair; guessing what a dangling
    // model_path meant is not. The one config act below (M11) is seeding the
    // starter template where NO config file exists at all -- an absent file
    // has no meaning to preserve, so creating it decides nothing -- and only
    // at the install's own path, never one a --config flag named elsewhere.
    const harness::SeedResult seeded = harness::seed_data_directory(inputs.home);

    std::vector<std::string> done;
    done.reserve(seeded.created.size());
    for (const std::string& name : seeded.created) {
        done.push_back("created " + (inputs.home / name).string());
    }
    for (const std::string& name : seeded.updated) {
        done.push_back("updated " + (inputs.home / name).string());
    }
    for (const std::string& name : seeded.removed) {
        done.push_back("removed " + (inputs.home / name).string());
    }
    if (!seeded.ok()) {
        done.push_back("could not finish: " + seeded.error);
    }
    if (inputs.config_missing && inputs.config_error.empty() &&
        inputs.config_path == inputs.home / "config" / harness::kConfigFileName) {
        try {
            harness::save_config_template(inputs.config_path, /*force=*/false);
            done.push_back("seeded " + inputs.config_path.string() +
                           " -- the starter config (no config file existed)");
        } catch (const std::exception& e) {
            done.push_back("could not seed the starter config: " + std::string{e.what()});
        }
    }
    // Staging an interrupted run left in the model store: Apogee's own
    // scratch, never the user's file, and never a live run's (see the
    // `leftovers` row). Removing it is a repair like seeding one.
    for (const models::AbandonedStaging& leftover :
         models::find_abandoned_staging(check_roots(inputs))) {
        if (const std::string error = models::remove_weights(leftover.dir); error.empty()) {
            done.push_back("removed " + leftover.dir.string() + " (" +
                           format_progress_size(static_cast<std::int64_t>(leftover.bytes)) +
                           ", left by an interrupted run)");
        } else {
            done.push_back(error);
        }
    }
    // A scaffolded server that lost its execute bit is a repair of the same
    // kind: the file is the user's, its mode is the install's.
    if (harness::supports_private_modes() && inputs.config_error.empty() &&
        !inputs.config_missing) {
        for (const auto& [name, server] : inputs.config.mcp_servers) {
            if (!server.enabled || server.command.find('/') == std::string::npos) {
                continue;
            }
            std::error_code code;
            const std::filesystem::path command{server.command};
            if (!std::filesystem::exists(command, code)) {
                continue;
            }
            const std::filesystem::perms mode =
                std::filesystem::status(command, code).permissions();
            if ((mode & std::filesystem::perms::owner_exec) != std::filesystem::perms::none) {
                continue;
            }
            std::filesystem::permissions(command, std::filesystem::perms::owner_exec,
                                         std::filesystem::perm_options::add, code);
            if (!code) {
                done.push_back("made executable " + server.command);
            }
        }
    }
    // The raw archive is a private directory the first capture made rather
    // than the seeding path; its mode is the install's to repair all the same.
    if (harness::supports_private_modes()) {
        const std::filesystem::path raw = inputs.home / "knowledge" / "raw";
        std::error_code code;
        if (std::filesystem::is_directory(raw, code)) {
            const std::filesystem::perms mode =
                std::filesystem::status(raw, code).permissions() & std::filesystem::perms::mask;
            if ((mode & (std::filesystem::perms::group_all | std::filesystem::perms::others_all)) !=
                std::filesystem::perms::none) {
                std::filesystem::permissions(raw, std::filesystem::perms::owner_all,
                                             std::filesystem::perm_options::replace, code);
                if (!code) {
                    done.push_back("set " + raw.string() + " to 0700");
                }
            }
        }
    }
    // A per-install secret left readable by others is a repair too -- the
    // same kind as a private directory's mode, one file down.
    if (!inputs.config_path.empty()) {
        for (const std::filesystem::path& secret :
             {httpserver::admin_token_path(inputs.config_path),
              secrets::credentials_path(inputs.config_path)}) {
            if (const std::string fixed = fix_secret_mode(secret); !fixed.empty()) {
                done.push_back(fixed);
            }
        }
    }
    return done;
}

std::string check_verdict(const CheckReport& report) {
    const std::size_t failures = report.count(Status::Fail);
    const std::size_t warnings = report.count(Status::Warn);
    if (failures > 0) {
        return std::to_string(failures) + " failure(s), " + std::to_string(warnings) +
               " warning(s)";
    }
    // Warnings are not failures, and the exit code says so. A keyless,
    // modelless install is a VALID install -- the acceptance criterion for
    // this command is that it passes on exactly that.
    return warnings > 0 ? "no failures, " + std::to_string(warnings) + " warning(s)"
                        : "everything checks out";
}

std::string render_report(const CheckReport& report, bool use_color) {
    const ansi::Style style{use_color};
    std::ostringstream out;

    std::string section;
    for (const CheckRow& row : report.rows) {
        if (row.section != section) {
            section = row.section;
            out << "\n" << style.bold(section) << "\n";
        }

        std::string_view mark;
        ansi::Color color = ansi::Color::Default;
        switch (row.status) {
            case Status::Ok:
                mark = "  ok  ";
                color = ansi::Color::Green;
                break;
            case Status::Warn:
                mark = " warn ";
                color = ansi::Color::Yellow;
                break;
            case Status::Fail:
                mark = " FAIL ";
                color = ansi::Color::Red;
                break;
            case Status::Skipped:
                mark = " skip ";
                color = ansi::Color::BrightBlack;
                break;
        }

        out << style.colorize(mark, color) << " " << row.name << "  " << row.detail << "\n";
        if (!row.remedy.empty()) {
            out << "        " << style.dim("run: " + row.remedy) << "\n";
        }
    }

    out << "\n";
    const ansi::Color verdict_color = report.count(Status::Fail) > 0   ? ansi::Color::Red
                                      : report.count(Status::Warn) > 0 ? ansi::Color::Yellow
                                                                       : ansi::Color::Green;
    out << style.colorize(check_verdict(report), verdict_color) << "\n";
    return out.str();
}

nlohmann::json render_report_document(const CheckReport& report,
                                      const std::optional<std::vector<std::string>>& fixed) {
    nlohmann::json rows = nlohmann::json::array();
    for (const CheckRow& row : report.rows) {
        nlohmann::json entry{
            {"section", row.section},
            {"name", row.name},
            {"status", row.status == Status::Skipped ? std::string{"skipped"}
                                                     : std::string{to_string(row.status)}},
            {"detail", row.detail}};
        if (!row.remedy.empty()) {
            entry["remedy"] = row.remedy;
        }
        rows.push_back(std::move(entry));
    }
    nlohmann::json document{{"rows", std::move(rows)},
                            {"ok", report.passed()},
                            {"failures", report.count(Status::Fail)},
                            {"warnings", report.count(Status::Warn)}};
    if (fixed.has_value()) {
        document["fixed"] = *fixed;
    }
    return document;
}

std::string_view CheckCommand::name() const noexcept {
    return "check";
}

std::string_view CheckCommand::summary() const noexcept {
    return "Diagnose this installation";
}

void CheckCommand::bind(CLI::App& root, const RootContext& context) {
    struct Flags {
        bool fix = false;
        bool no_color = false;
        bool quiet = false;
        bool refresh_providers = false;
    };

    auto flags = std::make_shared<Flags>();
    auto format = std::make_shared<ReadFormat>(ReadFormat::Text);

    CLI::App* cmd = root.add_subcommand(std::string{name()}, std::string{summary()});
    cmd->add_flag("--fix", flags->fix,
                  "Repair what is safely repairable: missing directories, private modes, and "
                  "the starter config where no config file exists at all. Never touches an "
                  "existing config.");
    cmd->add_flag("--no-color", flags->no_color, "Disable coloured output");
    cmd->add_flag("-q,--quiet", flags->quiet, "No progress line while it checks");
    cmd->add_flag("--refresh-providers", flags->refresh_providers,
                  "Scan the providers first -- each vendor CLI on PATH, its login evidence, each "
                  "key -- as 'apogee providers scan' does");
    add_read_format(cmd, format);

    cmd->callback([&context, flags, format]() {
        CheckInputs inputs;
        try {
            inputs = check_inputs(context);
        } catch (const std::exception& e) {
            std::cerr << "apogee check: " << e.what() << "\n";
            throw CLI::RuntimeError(1);
        }

        // Non-zero on failure so a script can gate on it -- the reason this is
        // a command rather than a page of documentation.
        if (!run_check_pass(std::move(inputs),
                            CheckPassOptions{.fix = flags->fix,
                                             .quiet = flags->quiet,
                                             .no_color = flags->no_color,
                                             .refresh_providers = flags->refresh_providers,
                                             .json = *format == ReadFormat::Json})) {
            throw CLI::RuntimeError(1);
        }
    });
}

namespace {

/// The environment and the running binary, defaulted where a caller left
/// them unset.
void default_seams(CheckInputs& inputs) {
    if (!inputs.env) {
        inputs.env = [](std::string_view name) {
            const char* value = std::getenv(std::string{name}.c_str());
            return value == nullptr ? std::string{} : std::string{value};
        };
    }
    if (inputs.executable.empty()) {
        inputs.executable = platform::executable_path();
    }
}

/// The config as it stands: loaded, broken, or not there yet.
void read_config_state(CheckInputs& state) {
    std::error_code exists_code;
    if (!state.config_error.empty()) {
        // Refused before it was read: nothing to load.
        return;
    }
    if (!std::filesystem::exists(state.config_path, exists_code)) {
        state.config_missing = true;
        return;
    }
    state.config_missing = false;
    try {
        const std::string content = harness::read_config_file(state.config_path);
        state.config = harness::parse_config(content, state.config_path.string());
        state.config_legacy = !harness::jsonc::looks_like_jsonc(content);
        state.config_missing_options = harness::missing_template_options(content);
    } catch (const harness::ConfigError& e) {
        state.config_error = e.what();
    }
}

/// `apply_fixes`' repairs as the command says them.
[[nodiscard]] std::vector<std::string> said_fixes(const std::vector<std::string>& done,
                                                  bool fold_created) {
    std::vector<std::string> lines;
    std::size_t created = 0;
    for (const std::string& line : done) {
        if (fold_created && line.starts_with("created ")) {
            ++created;
            continue;
        }
        lines.push_back("fixed: " + line);
    }
    if (created > 0) {
        lines.push_back("fixed: created " + std::to_string(created) +
                        " directories and files, the layout as a fresh install has it");
    }
    if (done.empty()) {
        lines.emplace_back("nothing to fix");
    }
    return lines;
}

}  // namespace

CheckInputs check_inputs(const RootContext& context) {
    CheckInputs inputs;
    try {
        inputs.config_path = harness::resolve_config_path(context.config_path);
    } catch (const std::exception& e) {
        // Both formats side by side (28i): the Config row says so; the rest
        // of the install is still worth a look.
        try {
            inputs.config_path = harness::config_dir() / harness::kConfigFileName;
        } catch (const std::exception&) {
            throw std::runtime_error{e.what()};
        }
        inputs.config_error = e.what();
    }
    inputs.home = harness::apogee_home();
    inputs.root = harness::current_root();  // which rung chose it (M10)
    return inputs;
}

CheckReport read_check_report(CheckInputs inputs, const BusyProgress& progress) {
    default_seams(inputs);
    read_config_state(inputs);
    inputs.progress = progress;
    return run_checks(inputs);
}

std::vector<std::string> fix_install(CheckInputs inputs, bool fold_created) {
    default_seams(inputs);
    read_config_state(inputs);
    return said_fixes(apply_fixes(inputs), fold_created);
}

bool run_check_pass(CheckInputs inputs, const CheckPassOptions& options) {
    default_seams(inputs);
    read_config_state(inputs);

    std::optional<std::vector<std::string>> fixed;
    if (options.fix && options.json) {
        // In the document, never on stdout beside it (28h).
        fixed = apply_fixes(inputs);
    } else if (options.fix) {
        for (const std::string& line : said_fixes(apply_fixes(inputs), options.fold_created)) {
            std::cout << line << "\n";
        }
    }
    if (options.fix && inputs.config_missing) {
        // A seeded starter config (M11) is part of this same pass's report:
        // read it the one way the pass reads any config.
        read_config_state(inputs);
    }

    if (options.refresh_providers) {
        // The explicit scan (28a), before the report reads its cache: the
        // one place `check` may start a vendor's binary, and only when asked.
        BusyLine busy{std::cerr, "scanning providers", busy_options(options.quiet || options.json)};
        const secrets::CredentialStore store{secrets::credentials_path(inputs.config_path)};
        (void)backends::scan_host_providers(backends::ScanOptions{}, &store);
    }

    CheckReport report;
    {
        // Every model's header is read, so on a full store this takes
        // seconds: said on one line, gone before the report (M1).
        BusyLine busy{std::cerr, "checking", busy_options(options.quiet || options.json)};
        inputs.progress = busy.sink();
        report = run_checks(inputs);
        inputs.progress = nullptr;
    }
    if (options.json) {
        write_document(std::cout, render_report_document(report, fixed));
        return report.passed();
    }
    const ansi::Style style =
        ansi::Style::detect(options.no_color ? ansi::ColorMode::Never : ansi::ColorMode::Auto);
    std::cout << render_report(report, style.color_enabled());
    return report.passed();
}

std::vector<RemedyCommand> macos_captions_remedy() {
    return {{.argv = {"brew", "install", "ffmpeg-full"}},
            {.argv = {"brew", "unlink", "ffmpeg"}, .may_fail = true},
            {.argv = {"brew", "link", "--force", "ffmpeg-full"}}};
}

std::string remedy_line(const std::vector<RemedyCommand>& commands) {
    std::string out;
    for (std::size_t index = 0; index < commands.size(); ++index) {
        if (index > 0) {
            out += commands[index - 1].may_fail ? "; " : " && ";
        }
        std::string words;
        for (const std::string& word : commands[index].argv) {
            words += (words.empty() ? "" : " ") + word;
        }
        out += words;
    }
    return out;
}

}  // namespace apogee::commands
