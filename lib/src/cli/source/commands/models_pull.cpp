#include "commands/models_pull.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <system_error>

#include "backends/http_client.h"
#include "models/acquire.h"
#include "models/gguf_inspect.h"
#include "models/quantize.h"
#include "models/source_hf.h"
#include "models/source_ollama.h"

namespace apogee::commands {
namespace {

[[noreturn]] void fail(const std::string& message) {
    std::cerr << "apogee models: " << message << "\n";
    throw CLI::RuntimeError(1);
}

/// A ref shaped "owner/repo…" is Hugging Face; anything else is an Ollama ref.
///
/// Ollama refs are `name:tag` with no slash (or `namespace/name:tag`, which is
/// ambiguous with HF and resolved in Ollama's favour only when the store
/// actually has it — see `pull_one`).
[[nodiscard]] bool looks_like_hf(std::string_view ref) noexcept {
    return ref.find('/') != std::string_view::npos;
}

/// The filename a pulled model gets: the ref with the characters a filesystem
/// dislikes replaced, plus `.gguf`.
[[nodiscard]] std::string filename_for(std::string_view ref) {
    std::string name;
    name.reserve(ref.size());
    for (const char c : ref) {
        name += (c == '/' || c == ':' || c == '@' || c == ' ') ? '-' : c;
    }
    if (!name.ends_with(".gguf")) {
        name += ".gguf";
    }
    return name;
}

/// Human-readable byte count.
[[nodiscard]] std::string human_size(std::int64_t bytes) {
    if (bytes <= 0) {
        return "unknown size";
    }
    if (bytes < 1024LL * 1024) {
        return std::to_string(bytes / 1024) + " KiB";
    }
    if (bytes < 1024LL * 1024 * 1024) {
        return std::to_string(bytes / (1024LL * 1024)) + " MiB";
    }
    return std::to_string(bytes / (1024LL * 1024 * 1024)) + " GiB";
}

}  // namespace

DeletePlan plan_delete(const std::filesystem::path& models_dir, std::string_view name) {
    DeletePlan plan;

    // "Delete a model by name" must never become "delete a file by path". Both
    // an absolute path and a `..` are refused before anything is touched.
    const std::filesystem::path requested{name};
    if (requested.is_absolute() || name.find("..") != std::string_view::npos) {
        plan.error = "a model is named, not pathed: '" + std::string{name} +
                     "' would reach outside the models directory";
        return plan;
    }

    std::filesystem::path candidate = models_dir / requested;
    if (candidate.extension() != ".gguf") {
        candidate += ".gguf";
    }

    std::error_code code;
    const std::filesystem::path canonical_dir = std::filesystem::weakly_canonical(models_dir, code);
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(candidate, code);
    // Belt and braces: even a name that passed the checks above must resolve
    // INSIDE the directory once symlinks are followed.
    if (!canonical.string().starts_with(canonical_dir.string())) {
        plan.error = "'" + std::string{name} + "' does not resolve inside " + models_dir.string();
        return plan;
    }

    if (!std::filesystem::exists(candidate, code)) {
        plan.error = "no model named '" + std::string{name} + "' in " + models_dir.string();
        return plan;
    }

    plan.model = candidate;
    plan.sidecar = models::sidecar_path_for(candidate);
    plan.has_sidecar = std::filesystem::exists(plan.sidecar, code);

    if (const std::optional<models::Sidecar> sidecar = models::load_sidecar(candidate)) {
        if (sidecar->source == "ollama") {
            plan.ollama_sourced = true;
            plan.ollama_ref = sidecar->ref;
        }
    }

    plan.ok = true;
    return plan;
}

std::string render_repair(const std::filesystem::path& models_dir, std::string_view name) {
    const DeletePlan located = plan_delete(models_dir, name);
    if (!located.ok) {
        return {};
    }

    std::ostringstream out;
    out << "model:   " << located.model.filename().string() << "\n";

    const std::optional<models::Sidecar> sidecar = models::load_sidecar(located.model);
    if (!sidecar.has_value()) {
        // A model a user dropped in by hand has no record, and that is fine --
        // it is simply not something integrity can be rechecked against.
        const models::GgufInfo info = models::inspect_gguf(located.model);
        out << "record:  none (not acquired through 'apogee models pull')\n";
        out << "header:  " << (info.parsed ? "ok" : "FAILED -- " + info.parse_error) << "\n";
        if (!info.parsed) {
            out << "\nThis file is not a readable GGUF. Replace it, or delete it with\n"
                << "  apogee models delete " << name << "\n";
        }
        return out.str();
    }

    out << "source:  " << sidecar->source << "  (" << sidecar->ref << ")\n";
    out << "pulled:  " << sidecar->pulled_at << "\n";
    out << "at pull: " << sidecar->verification.summary() << "\n";

    const models::Verification now = models::reverify(located.model, *sidecar);
    out << "now:     " << now.summary() << "\n";

    if (now.sound()) {
        out << "\nNothing to repair.\n";
        return out.str();
    }

    out << "\nThis model no longer matches its record. Re-acquire it:\n";
    out << "  apogee models delete " << name << "\n";
    out << "  apogee models pull " << sidecar->ref << "\n";
    return out.str();
}

void bind_model_mutations(CLI::App& models, const std::filesystem::path& models_dir) {
    // ---- pull ---------------------------------------------------------------
    auto pull_ref = std::make_shared<std::string>();
    auto pull_yes = std::make_shared<bool>(false);
    CLI::App* pull = models.add_subcommand("pull", "Download a model from Hugging Face or Ollama");
    pull->add_option("ref", *pull_ref,
                     "owner/repo[:file.gguf] for Hugging Face, or name:tag for Ollama")
        ->required();
    pull->add_flag("-y,--yes", *pull_yes, "Do not stop for the unrunnable-architecture warning");

    pull->callback([pull_ref, pull_yes, models_dir]() {
        const std::string& ref = *pull_ref;
        const std::filesystem::path destination = models_dir / filename_for(ref);

        models::SourcePromise promise;
        models::ByteSource source;

        // Ollama first when the store actually has it: a ref like "user/model"
        // is ambiguous, and a local copy is free while a download is not.
        const std::filesystem::path store = models::ollama_store_root();
        std::optional<models::OllamaEntry> entry = models::find_in_store(store, ref);

        std::unique_ptr<backends::HttpClient> client;
        models::HfRef hf;
        const harness::CancellationToken cancellation;

        if (entry.has_value()) {
            promise = models::promise_for(*entry);
            source = models::blob_source(*entry);
            std::cout << "copying from your Ollama store (" << human_size(entry->size) << ")\n";
        } else if (looks_like_hf(ref)) {
            std::optional<models::HfRef> parsed = models::parse_hf_ref(ref);
            if (!parsed.has_value()) {
                fail("'" + ref + "' is not a Hugging Face ref (expected owner/repo[:file.gguf])");
            }
            hf = *parsed;

            client =
                std::make_unique<backends::HttpClient>(std::make_unique<backends::CurlTransport>());
            const std::string token = models::hf_token({});

            std::string error;
            if (!models::resolve_file(*client, hf, token, cancellation, error)) {
                fail(error);
            }
            source = models::http_source(*client, hf, token, cancellation, promise);
            std::cout << "downloading " << promise.source_url << "\n";
        } else if (models::store_has_manifest(store, ref)) {
            // Found in the store, but with no model layer: a CLOUD model, whose
            // weights are on Ollama's servers. Telling this user to `ollama
            // pull` would send them to re-fetch what they already have.
            fail("'" + ref +
                 "' is an Ollama cloud model -- its weights run on Ollama's servers, so there is "
                 "nothing local to copy. Use it through a 'ollama-cli' backend instead, or pull a "
                 "local variant such as '" +
                 std::string{models::split_ref(ref).first} + "' without the -cloud tag");
        } else {
            fail("'" + ref +
                 "' is not in your Ollama store and is not a Hugging Face ref. Pull it with "
                 "'ollama pull " +
                 ref + "' first, or name a Hugging Face repository as owner/repo");
        }

        // The advisory warning, BEFORE the bytes move. It never refuses -- the
        // open-model policy means the user's answer is final.
        if (entry.has_value() && !*pull_yes) {
            const models::GgufInfo peek = models::inspect_gguf(entry->blob);
            if (peek.parsed && models::is_known_unrunnable(peek.architecture)) {
                std::cout << "warning: architecture '" << peek.architecture
                          << "' is not known to run in Apogee's llama.cpp.\n"
                          << "         Pulling anyway is fine -- pass --yes to skip this notice.\n";
            }
        }

        std::int64_t last_report = 0;
        const models::AcquireResult result = models::acquire(
            destination, promise, source, [&last_report](std::int64_t written, std::int64_t total) {
                // Coarse on purpose: a progress line per 64 MiB is legible in a
                // log and does not flood a pipe.
                if (written - last_report < 64LL * 1024 * 1024) {
                    return;
                }
                last_report = written;
                std::cout << "  " << human_size(written);
                if (total > 0) {
                    std::cout << " of " << human_size(total);
                }
                std::cout << "\n";
            });

        if (!result.ok) {
            fail(result.error);
        }

        std::cout << "\n" << result.path.string() << "\n";
        std::cout << "verified: " << result.sidecar.verification.summary() << "\n";
        if (!result.error.empty()) {
            std::cout << "note: " << result.error << "\n";
        }

        // A vision model's projector is a SEPARATE layer in Ollama's manifest,
        // and useless to leave behind: without it the model can only do text,
        // and the user has no way to know a second file was sitting there.
        std::filesystem::path projector;
        if (entry.has_value() && entry->has_projector()) {
            const std::filesystem::path destination_projector =
                models_dir / filename_for(ref + "-mmproj");
            std::cout << "\nthis model has a vision projector; copying that too ("
                      << human_size(entry->projector_size) << ")\n";

            const models::AcquireResult vision =
                models::acquire(destination_projector, models::projector_promise_for(*entry),
                                models::projector_source(*entry));
            if (!vision.ok) {
                // The model is already in place and usable for text, so this is
                // a warning rather than a failure of the whole pull.
                std::cout << "warning: the projector could not be copied -- " << vision.error
                          << "\n         this model will work for text but not for images\n";
            } else {
                projector = vision.path;
                std::cout << vision.path.string() << "\n";
            }
        }

        std::cout << "\nUse it by adding a backend:\n"
                  << "  apogee config add-backend <name> --type llamacpp --model-path "
                  << result.path.string() << "\n";
        if (!projector.empty()) {
            std::cout << "\nthen add its projector to that backend so it can read images:\n"
                      << "  mmproj_path: \"" << projector.string() << "\"\n";
        }
    });

    // ---- delete -------------------------------------------------------------
    auto delete_name = std::make_shared<std::string>();
    auto delete_yes = std::make_shared<bool>(false);
    CLI::App* remove = models.add_subcommand("delete", "Remove a model Apogee acquired");
    remove->add_option("name", *delete_name, "Model file name")->required();
    remove->add_flag("-y,--yes", *delete_yes, "Do not ask for confirmation");

    remove->callback([delete_name, delete_yes, models_dir]() {
        const DeletePlan plan = plan_delete(models_dir, *delete_name);
        if (!plan.ok) {
            fail(plan.error);
        }

        std::cout << "will remove:\n  " << plan.model.string() << "\n";
        if (plan.has_sidecar) {
            std::cout << "  " << plan.sidecar.string() << "\n";
        }
        if (plan.ollama_sourced) {
            // Apogee's copy only. Ollama's blobs are shared between models, so
            // deleting one by hand corrupts every sibling that referenced it --
            // `ollama rm` is the only supported way to remove the original.
            std::cout << "\nthis was copied from your Ollama store; the original stays.\n"
                      << "to remove that too:  ollama rm " << plan.ollama_ref << "\n";
        }

        if (!*delete_yes) {
            std::cout << "\nre-run with --yes to remove.\n";
            return;
        }

        std::error_code code;
        std::filesystem::remove(plan.model, code);
        if (code) {
            fail("could not remove " + plan.model.string() + ": " + code.message());
        }
        if (plan.has_sidecar) {
            std::filesystem::remove(plan.sidecar, code);
        }
        std::cout << "\nremoved.\n";
    });

    // ---- quantize -----------------------------------------------------------
    auto quant_in = std::make_shared<std::string>();
    auto quant_out = std::make_shared<std::string>();
    auto quant_type = std::make_shared<std::string>("Q4_K_M");
    auto quant_list = std::make_shared<bool>(false);
    CLI::App* quantize = models.add_subcommand("quantize", "Make a smaller copy of a GGUF");
    // Not `->required()`: --types is a listing, and CLI11 would demand the two
    // positionals before ever reaching the callback that prints the list.
    quantize->add_option("input", *quant_in, "Model file to read");
    quantize->add_option("output", *quant_out, "Where to write the smaller copy");
    quantize->add_option("-t,--type", *quant_type, "Quantization type (default Q4_K_M)");
    quantize->add_flag("--types", *quant_list, "List the accepted quantization types and exit");

    quantize->callback([quant_in, quant_out, quant_type, quant_list]() {
        if (*quant_list) {
            for (const models::QuantType& type : models::quant_types()) {
                std::cout << "  " << type.name << "   " << type.summary << "\n";
            }
            return;
        }
        if (quant_in->empty() || quant_out->empty()) {
            fail(
                "quantize needs an input and an output, e.g.\n"
                "  apogee models quantize model.gguf smaller.gguf --type Q4_K_M\n"
                "  apogee models quantize --types    (to see the choices)");
        }

        const models::QuantizeResult result = models::quantize(
            std::filesystem::path{*quant_in}, std::filesystem::path{*quant_out}, *quant_type);
        if (!result.ok) {
            // Reported BEFORE any "this will take a while" note: announcing
            // work and then refusing to do it reads as a crash rather than as
            // a refusal.
            fail(result.error);
        }

        std::cout << *quant_out << "\n"
                  << human_size(result.input_bytes) << " -> " << human_size(result.output_bytes)
                  << "\n";
    });

    // ---- repair -------------------------------------------------------------
    auto repair_name = std::make_shared<std::string>();
    CLI::App* repair = models.add_subcommand("repair", "Re-verify a model against its record");
    repair->add_option("name", *repair_name, "Model file name")->required();
    repair->callback([repair_name, models_dir]() {
        const std::string body = render_repair(models_dir, *repair_name);
        if (body.empty()) {
            fail("no model named '" + *repair_name + "' in " + models_dir.string());
        }
        std::cout << body;
    });
}

}  // namespace apogee::commands
