#include "commands/models_pull.h"

#include <CLI/CLI.hpp>

#include <iostream>
#include <memory>
#include <optional>
#include <system_error>
#include <vector>

#include "backends/http_client.h"
#include "harness/config.h"
#include "harness/paths.h"
#include "models/acquire.h"
#include "models/gguf_inspect.h"
#include "models/quantize.h"
#include "models/snapshot.h"
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

/// `models pull <owner>/<repo> --safetensors`: the whole full-weight
/// repository -- shards, configuration, tokenizer -- into one directory
/// through the tree ladder, each shard checked against the sha256 Hugging
/// Face publishes for it, the directory committed by rename so a half
/// snapshot never appears. What the training track fine-tunes.
void pull_snapshot(const std::string& ref, const std::filesystem::path& root) {
    if (!looks_like_hf(ref)) {
        fail("--safetensors takes a Hugging Face repository (owner/repo); '" + ref +
             "' is not one");
    }
    const std::optional<models::HfRef> parsed = models::parse_hf_ref(ref);
    if (!parsed.has_value()) {
        fail("'" + ref + "' is not a Hugging Face ref (expected owner/repo[@revision])");
    }
    if (!parsed->file.empty()) {
        fail("--safetensors downloads the whole repository; drop ':" + parsed->file + "'");
    }
    backends::HttpClient client{std::make_unique<backends::CurlTransport>()};
    const std::string token = models::hf_token({});
    const harness::CancellationToken cancellation;

    const models::HfTree tree =
        models::list_repo_tree(client, *parsed, models::HfRepoKind::Model, token, cancellation);
    if (!tree.ok) {
        fail(tree.error);
    }
    std::vector<models::HfFile> wanted;
    bool any_shard = false;
    std::int64_t total = 0;
    for (const models::HfFile& file : tree.files) {
        if (!models::snapshot_wanted(file.path)) {
            continue;
        }
        any_shard = any_shard || file.path.ends_with(".safetensors");
        total += file.size;
        wanted.push_back(file);
    }
    if (!any_shard) {
        fail("'" + parsed->repo_id() +
             "' holds no .safetensors shards -- nothing to snapshot. For a GGUF, pull it "
             "without --safetensors");
    }

    const std::filesystem::path destination = root / models::repo_directory_name(*parsed);
    std::cout << "downloading " << wanted.size() << " file(s), " << human_size(total) << ", into "
              << destination.string() << "\n";

    std::vector<models::TreeItem> items;
    std::vector<models::SourcePromise> promises;
    promises.reserve(wanted.size());
    for (const models::HfFile& file : wanted) {
        promises.emplace_back();
        models::TreeItem item;
        item.relative = file.path;
        item.source = models::http_source(client, *parsed, models::HfRepoKind::Model, file, token,
                                          cancellation, promises.back());
        item.promise = promises.back();
        items.push_back(std::move(item));
    }

    std::size_t last_index = 0;
    std::int64_t last_report = 0;
    const models::AcquireTreeResult result = models::acquire_tree(
        destination, items,
        [&](std::size_t index, std::size_t count, std::string_view relative, std::int64_t written,
            std::int64_t size) {
            if (index != last_index) {
                last_index = index;
                last_report = 0;
                std::cout << "  [" << index << "/" << count << "] " << relative << "\n";
            }
            if (written - last_report < 64LL * 1024 * 1024) {
                return;
            }
            last_report = written;
            std::cout << "      " << human_size(written);
            if (size > 0) {
                std::cout << " of " << human_size(size);
            }
            std::cout << "\n";
        });
    if (!result.ok) {
        fail(result.error);
    }

    models::Snapshot record;
    record.ref = parsed->repo_id();
    record.revision = parsed->revision.empty() ? "main" : parsed->revision;
    record.source = "huggingface";
    record.pulled_at = result.sidecars.empty() ? std::string{} : result.sidecars.front().pulled_at;
    for (std::size_t i = 0; i < wanted.size() && i < result.sidecars.size(); ++i) {
        record.files.push_back(
            {wanted[i].path, result.sidecars[i].file_size, result.sidecars[i].file_digest});
    }
    if (!models::write_snapshot(destination, record)) {
        std::cout << "note: the snapshot landed but its record could not be written\n";
    }

    std::size_t digests = 0;
    for (const models::Sidecar& sidecar : result.sidecars) {
        if (sidecar.verification.digest_checked) {
            ++digests;
        }
    }
    std::cout << "\n"
              << result.path.string() << "\n"
              << "verified: " << result.files << " file(s), " << human_size(result.bytes) << ", "
              << digests << " checked against a published sha256\n"
              << "\nA full-weight snapshot: trainable with 'apogee train', not runnable. It "
                 "appears in 'apogee models list' as safetensors.\n";
}

}  // namespace

DeletePlan plan_delete(const std::filesystem::path& models_dir, std::string_view name) {
    DeletePlan plan;

    // "Delete a model by name" must never become "delete a file by path". Both
    // an absolute path and a `..` are refused before anything is touched. Any
    // root counts, not only what is_absolute() accepts: on Windows "/etc/x" is
    // root-relative rather than absolute, and "C:x" has a drive but no root
    // directory, and either would have walked out of the models directory.
    const std::filesystem::path requested{name};
    if (requested.is_absolute() || requested.has_root_name() || requested.has_root_directory() ||
        name.find("..") != std::string_view::npos) {
        plan.error = "a model is named, not pathed: '" + std::string{name} +
                     "' would reach outside the models directory";
        return plan;
    }

    std::filesystem::path candidate = models_dir / requested;
    std::error_code snapshot_code;
    const bool snapshot = std::filesystem::is_directory(candidate, snapshot_code) &&
                          models::is_snapshot_dir(candidate);
    if (!snapshot && candidate.extension() != ".gguf") {
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

    if (snapshot) {
        // A SafeTensors snapshot: the whole directory and its record go
        // together; there is no sidecar beside it to plan for.
        plan.model = candidate;
        plan.snapshot = true;
        plan.ok = true;
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

std::filesystem::path snapshot_root(const std::filesystem::path& models_dir,
                                    const std::string& config_flag) {
    try {
        const std::filesystem::path path = harness::resolve_config_path(config_flag);
        std::error_code code;
        if (!std::filesystem::exists(path, code)) {
            return models_dir;
        }
        const harness::Config config = harness::load_config(path);
        if (config.paths.hf_dir.empty()) {
            return models_dir;
        }
        return std::filesystem::path{harness::expand_env_and_home(config.paths.hf_dir)};
    } catch (const harness::ConfigError&) {
        // A config that does not parse is `check`'s problem to report; a
        // pull still has somewhere sensible to land.
        return models_dir;
    }
}

void bind_model_mutations(CLI::App& models, const std::filesystem::path& models_dir,
                          const RootContext& context) {
    // ---- pull ---------------------------------------------------------------
    auto pull_ref = std::make_shared<std::string>();
    auto pull_yes = std::make_shared<bool>(false);
    CLI::App* pull = models.add_subcommand("pull", "Download a model from Hugging Face or Ollama");
    pull->add_option("ref", *pull_ref,
                     "owner/repo[:file.gguf] for Hugging Face, or name:tag for Ollama")
        ->required();
    pull->add_flag("-y,--yes", *pull_yes, "Do not stop for the unrunnable-architecture warning");
    auto pull_safetensors = std::make_shared<bool>(false);
    pull->add_flag("--safetensors", *pull_safetensors,
                   "Download a Hugging Face repository's full-weight SafeTensors snapshot "
                   "(trainable, not runnable) instead of a GGUF");

    pull->callback([pull_ref, pull_yes, pull_safetensors, models_dir, &context]() {
        const std::string& ref = *pull_ref;
        if (*pull_safetensors) {
            pull_snapshot(ref, snapshot_root(models_dir, context.config_path));
            return;
        }
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

    remove->callback([delete_name, delete_yes, models_dir, &context]() {
        DeletePlan plan = plan_delete(models_dir, *delete_name);
        if (!plan.ok) {
            // A snapshot may live under paths.hf_dir rather than models/.
            const std::filesystem::path root = snapshot_root(models_dir, context.config_path);
            if (root != models_dir) {
                const DeletePlan under_hf = plan_delete(root, *delete_name);
                if (under_hf.ok && under_hf.snapshot) {
                    plan = under_hf;
                }
            }
        }
        if (!plan.ok) {
            fail(plan.error);
        }

        std::cout << "will remove:\n  " << plan.model.string()
                  << (plan.snapshot ? " (a SafeTensors snapshot, whole)" : "") << "\n";
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
        if (plan.snapshot) {
            std::filesystem::remove_all(plan.model, code);
        } else {
            std::filesystem::remove(plan.model, code);
        }
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
