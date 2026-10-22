#include "modelstore/snapshot.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <random>
#include <set>
#include <system_error>

#include "modelstore/sidecar.h"
#include "modelstore/store.h"

namespace apogee::models {

std::filesystem::path snapshot_record_path(const std::filesystem::path& dir) {
    return dir / "apogee-snapshot.json";
}

bool write_snapshot(const std::filesystem::path& dir, const Snapshot& snapshot) {
    nlohmann::json files = nlohmann::json::array();
    for (const SnapshotFile& file : snapshot.files) {
        files.push_back({{"path", file.path}, {"size", file.size}, {"sha256", file.sha256}});
    }
    nlohmann::json record{{"ref", snapshot.ref},
                          {"revision", snapshot.revision},
                          {"source", snapshot.source},
                          {"pulled_at", snapshot.pulled_at},
                          {"files", files}};
    if (!snapshot.transform.empty()) {
        record["transform"] = snapshot.transform;
    }
    if (!snapshot.kind.empty()) {
        record["kind"] = snapshot.kind;
    }
    if (!snapshot.bases.empty()) {
        record["bases"] = snapshot.bases;
    }
    std::filesystem::path temp = snapshot_record_path(dir);
    temp += ".tmp-" + std::to_string(std::random_device{}());
    {
        std::ofstream out{temp, std::ios::binary};
        if (!out) {
            return false;
        }
        out << record.dump(2) << "\n";
    }
    std::error_code code;
    std::filesystem::rename(temp, snapshot_record_path(dir), code);
    if (code) {
        std::filesystem::remove(temp, code);
        return false;
    }
    return true;
}

std::optional<Snapshot> load_snapshot(const std::filesystem::path& dir) {
    std::ifstream in{snapshot_record_path(dir), std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    const nlohmann::json record = nlohmann::json::parse(in, nullptr, false);
    if (record.is_discarded() || !record.is_object()) {
        return std::nullopt;
    }
    Snapshot snapshot;
    snapshot.ref = record.value("ref", std::string{});
    snapshot.revision = record.value("revision", std::string{});
    snapshot.source = record.value("source", std::string{});
    snapshot.pulled_at = record.value("pulled_at", std::string{});
    snapshot.transform = record.value("transform", std::string{});
    // A word, as written; anything else is no kind recorded (53e).
    if (const auto kind = record.find("kind"); kind != record.end() && kind->is_string()) {
        snapshot.kind = kind->get<std::string>();
    }
    // A drafter's bases (53i): the names that are text, each once; anything
    // else names none.
    if (const auto bases = record.find("bases"); bases != record.end() && bases->is_array()) {
        for (const nlohmann::json& base : *bases) {
            if (base.is_string() && !base.get<std::string>().empty() &&
                std::ranges::find(snapshot.bases, base.get<std::string>()) ==
                    snapshot.bases.end()) {
                snapshot.bases.push_back(base.get<std::string>());
            }
        }
    }
    if (const auto files = record.find("files"); files != record.end() && files->is_array()) {
        for (const nlohmann::json& file : *files) {
            if (!file.is_object()) {
                continue;
            }
            SnapshotFile entry;
            entry.path = file.value("path", std::string{});
            entry.size = file.value("size", std::int64_t{0});
            entry.sha256 = file.value("sha256", std::string{});
            snapshot.files.push_back(std::move(entry));
        }
    }
    return snapshot;
}

bool is_snapshot_dir(const std::filesystem::path& dir) {
    std::error_code code;
    if (!std::filesystem::is_directory(dir, code) ||
        !std::filesystem::is_regular_file(dir / "config.json", code)) {
        return false;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (code) {
            return false;
        }
        if (entry.is_regular_file(code) && entry.path().extension() == ".safetensors") {
            return true;
        }
    }
    return false;
}

std::string snapshot_architecture(const std::filesystem::path& dir) {
    std::ifstream in{dir / "config.json", std::ios::binary};
    if (!in) {
        return {};
    }
    const nlohmann::json config = nlohmann::json::parse(in, nullptr, false);
    if (config.is_discarded() || !config.is_object()) {
        return {};
    }
    if (const auto architectures = config.find("architectures");
        architectures != config.end() && architectures->is_array() && !architectures->empty() &&
        architectures->front().is_string()) {
        std::string name = architectures->front().get<std::string>();
        for (const std::string_view suffix : {"ForCausalLM", "ForConditionalGeneration"}) {
            if (name.ends_with(suffix)) {
                name.erase(name.size() - suffix.size());
                break;
            }
        }
        return name;
    }
    return config.value("model_type", std::string{});
}

namespace {

/// `config.json` parsed, or a non-object when absent or not JSON.
[[nodiscard]] nlohmann::json read_model_config(const std::filesystem::path& dir) {
    std::ifstream in{dir / "config.json", std::ios::binary};
    if (!in) {
        return {};
    }
    nlohmann::json config = nlohmann::json::parse(in, nullptr, false);
    return config.is_object() ? config : nlohmann::json{};
}

[[nodiscard]] std::string family_of(const nlohmann::json& config) {
    const auto type = config.find("model_type");
    if (type == config.end() || !type->is_string()) {
        return {};
    }
    const std::string name = type->get<std::string>();
    return name.substr(0, name.find('_'));
}

[[nodiscard]] std::int64_t positive_integer(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_number_integer() && found->get<std::int64_t>() > 0
               ? found->get<std::int64_t>()
               : 0;
}

/// The architectures llama.cpp's converter registers as a draft head at the
/// pinned revision (`third_party/llama.cpp-convert/conversion/gemma.py`'s
/// `Gemma4AssistantModel`) -- named, for a config that does not say its
/// base's width.
constexpr std::array<std::string_view, 2> kDraftHeadArchitectures{
    "Gemma4AssistantForCausalLM", "Gemma4UnifiedAssistantForCausalLM"};

[[nodiscard]] std::string joined(const std::vector<std::string>& names) {
    std::string out;
    for (const std::string& name : names) {
        out += (out.empty() ? "" : ", ") + name;
    }
    return out;
}

/// The base, as said after a draft head: the stored models, else its width.
[[nodiscard]] std::string base_of(const DraftHead& head, const std::vector<std::string>& bases) {
    if (!bases.empty()) {
        return joined(bases);
    }
    if (head.backbone_hidden_size > 0) {
        return "a " + std::to_string(head.backbone_hidden_size) + "-wide " +
               (head.family.empty() ? std::string{"model"} : head.family + " model");
    }
    return {};
}

}  // namespace

std::optional<DraftHead> snapshot_draft_head(const std::filesystem::path& dir) {
    const nlohmann::json config = read_model_config(dir);
    if (!config.is_object()) {
        return std::nullopt;
    }
    DraftHead head;
    head.backbone_hidden_size = positive_integer(config, "backbone_hidden_size");
    head.family = family_of(config);
    if (const auto type = config.find("model_type"); type != config.end() && type->is_string()) {
        head.model_type = type->get<std::string>();
    }
    if (const auto architectures = config.find("architectures");
        architectures != config.end() && architectures->is_array() && !architectures->empty() &&
        architectures->front().is_string()) {
        head.architecture = architectures->front().get<std::string>();
    } else {
        head.architecture = config.value("model_type", std::string{});
    }
    const bool named = std::ranges::find(kDraftHeadArchitectures, head.architecture) !=
                       kDraftHeadArchitectures.end();
    if (head.backbone_hidden_size == 0 && !named) {
        return std::nullopt;
    }
    return head;
}

ConfigWidth config_width(const std::filesystem::path& dir) {
    const nlohmann::json config = read_model_config(dir);
    if (!config.is_object()) {
        return {};
    }
    ConfigWidth width{.hidden_size = positive_integer(config, "hidden_size"),
                      .family = family_of(config)};
    if (const auto text = config.find("text_config");
        width.hidden_size == 0 && text != config.end() && text->is_object()) {
        width.hidden_size = positive_integer(*text, "hidden_size");
    }
    return width;
}

std::string describe_draft_head(const DraftHead& head, const std::vector<std::string>& bases) {
    const std::string base = base_of(head, bases);
    return "a draft head (" + head.architecture +
           "): speculative decoding runs it beside its base model" +
           (base.empty() ? std::string{} : " (" + base + ")") +
           ", reading that model's hidden states, and never alone";
}

std::string draft_head_refusal(std::string_view subject, const DraftHead& head,
                               const std::vector<std::string>& bases) {
    return std::string{subject} + " is " + describe_draft_head(head, bases) +
           " -- nothing converted from it could answer a chat; convert its base model instead";
}

std::string draft_head_note(const DraftHead& head, const std::vector<std::string>& bases) {
    const std::string base = base_of(head, bases);
    return "draft head -- runs beside " + (base.empty() ? std::string{"its base model"} : base) +
           ", never alone";
}

namespace {

/// The class a diffusers-written config names (M20): `_class_name` beside a
/// `_diffusers_version`, with no `architectures`; empty for anything else.
[[nodiscard]] std::string diffusers_class(const nlohmann::json& config) {
    if (!config.is_object() || config.contains("architectures")) {
        return {};
    }
    const auto class_name = config.find("_class_name");
    if (class_name == config.end() || !class_name->is_string() ||
        !config.contains("_diffusers_version")) {
        return {};
    }
    return class_name->get<std::string>();
}

/// The root `.pth` files under `dir`, sorted by name.
[[nodiscard]] std::vector<std::string> root_checkpoints(const std::filesystem::path& dir) {
    std::vector<std::string> out;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (code) {
            break;
        }
        if (entry.is_regular_file(code) && entry.path().extension() == ".pth") {
            out.push_back(entry.path().filename().string());
        }
    }
    std::ranges::sort(out);
    return out;
}

/// The VAE a Wan transformer decodes through, as Wan's releases name it: by
/// the latent width its `out_dim` declares -- 48 channels Wan2.2's, 16
/// Wan2.1's.
[[nodiscard]] std::string wan_vae_name(const nlohmann::json& config) {
    switch (positive_integer(config, "out_dim")) {
        case 48:
            return "Wan2.2_VAE.pth";
        case 16:
            return "Wan2.1_VAE.pth";
        default:
            return "*_VAE.pth";
    }
}

[[nodiscard]] bool is_text_encoder_file(std::string_view name) noexcept {
    return name.starts_with("models_t5_");
}

[[nodiscard]] bool is_vae_file(std::string_view name) noexcept {
    return name.find("VAE") != std::string_view::npos || name.find("vae") != std::string_view::npos;
}

/// "a", "a and b", "a, b and c".
[[nodiscard]] std::string said_together(const std::vector<std::string>& names) {
    std::string out;
    for (std::size_t i = 0; i < names.size(); ++i) {
        out += (i == 0 ? "" : i + 1 == names.size() ? " and " : ", ") + names[i];
    }
    return out;
}

}  // namespace

bool WanLayout::whole() const noexcept {
    return std::ranges::all_of(components, &WanComponent::present);
}

std::vector<std::string> WanLayout::missing() const {
    std::vector<std::string> out;
    for (const WanComponent& component : components) {
        if (!component.present) {
            out.push_back(component.file);
        }
    }
    return out;
}

bool is_wan_transformer_class(std::string_view class_name) noexcept {
    return class_name.find("Wan") != std::string_view::npos &&
           class_name.find("Model") != std::string_view::npos &&
           class_name.find("Transformer") == std::string_view::npos;
}

std::string wan_transformer_class(std::string_view config_text) {
    const nlohmann::json config = nlohmann::json::parse(config_text, nullptr, false);
    std::string name = diffusers_class(config);
    return is_wan_transformer_class(name) ? name : std::string{};
}

std::optional<WanLayout> snapshot_wan_layout(const std::filesystem::path& dir) {
    const nlohmann::json config = read_model_config(dir);
    WanLayout layout{.class_name = diffusers_class(config), .components = {}};
    if (!is_wan_transformer_class(layout.class_name)) {
        return std::nullopt;
    }
    const std::vector<std::string> on_disk = root_checkpoints(dir);
    // What a pull that took the components recorded, for a set that lost one
    // since: named as it was pulled.
    std::vector<std::string> recorded;
    if (const std::optional<Snapshot> record = load_snapshot(dir)) {
        for (const SnapshotFile& file : record->files) {
            if (file.path.find('/') == std::string::npos && file.path.ends_with(".pth")) {
                recorded.push_back(file.path);
            }
        }
    }
    const auto resolve = [&](std::string role, bool (*is)(std::string_view) noexcept,
                             std::string named) {
        WanComponent component{.role = std::move(role), .file = std::move(named), .present = false};
        if (const auto found = std::ranges::find_if(on_disk, is); found != on_disk.end()) {
            component.file = *found;
            component.present = true;
        } else if (const auto listed = std::ranges::find_if(recorded, is);
                   listed != recorded.end()) {
            component.file = *listed;
        }
        layout.components.push_back(std::move(component));
    };
    resolve("text encoder", is_text_encoder_file, "models_t5_umt5-xxl-enc-bf16.pth");
    resolve("VAE", is_vae_file, wan_vae_name(config));
    // Any other root checkpoint the set holds or its record lists -- an image
    // encoder, say -- is a component too.
    std::vector<std::string> others = on_disk;
    others.insert(others.end(), recorded.begin(), recorded.end());
    std::ranges::sort(others);
    others.erase(std::ranges::unique(others).begin(), others.end());
    for (const std::string& name : others) {
        if (std::ranges::find(layout.components, name, &WanComponent::file) ==
            layout.components.end()) {
            layout.components.push_back(
                {.role = "checkpoint",
                 .file = name,
                 .present = std::ranges::find(on_disk, name) != on_disk.end()});
        }
    }
    return layout;
}

bool DiffusionModel::mlx_convertible() const noexcept {
    return wan.has_value() && wan->whole() && mlx_video.value_or(false);
}

std::optional<DiffusionModel> snapshot_diffusion_model(const std::filesystem::path& dir,
                                                       const VideoConverts& mlx_video) {
    const std::string class_name = diffusers_class(read_model_config(dir));
    if (class_name.empty()) {
        return std::nullopt;
    }
    DiffusionModel model{
        .class_name = class_name, .wan = snapshot_wan_layout(dir), .ref = {}, .mlx_video = {}};
    if (const std::optional<Snapshot> record = load_snapshot(dir);
        record.has_value() && !record->ref.empty()) {
        model.ref = record->ref;
        if (!record->revision.empty() && record->revision != "main") {
            model.ref += "@" + record->revision;
        }
    }
    // Whether mlx-video is there to convert it, asked of the caller only for
    // the layout it converts (53k).
    if (model.wan.has_value() && mlx_video) {
        model.mlx_video = mlx_video();
    }
    return model;
}

std::string describe_diffusion_model(const DiffusionModel& model) {
    if (!model.wan.has_value()) {
        return "a diffusion model's weights (" + model.class_name +
               ", a diffusers class): neither converter takes one -- llama.cpp's finds a model by "
               "its architectures, and mlx-lm's builds a language model";
    }
    // What it is, whole or what it lacks and the way to complete it.
    const WanLayout& wan = *model.wan;
    std::string out = "Wan's original layout (" + wan.class_name + ")";
    if (wan.whole()) {
        std::vector<std::string> files;
        for (const WanComponent& component : wan.components) {
            files.push_back(component.file);
        }
        out += ", complete -- its transformer beside " + said_together(files);
    } else {
        const std::vector<std::string> missing = wan.missing();
        out += ", missing " + said_together(missing) + " -- " +
               (model.ref.empty() ? std::string{"pulling its repository again with --safetensors"}
                                  : "'apogee models pull " + model.ref + " --safetensors'") +
               " fetches " + (missing.size() == 1 ? "it" : "them") + " into this set";
    }
    // How it converts (53k): through mlx-video to MLX once it is whole --
    // said ready where mlx-video is installed, its setup named where it is
    // not -- and never through llama.cpp's converter.
    if (wan.whole() && model.mlx_video == true) {
        out += "; --mlx converts it to an MLX video model through mlx-video";
    } else {
        out += std::string{"; "} + (wan.whole() ? "" : "once it is complete, ") +
               "mlx-video converts it to an MLX video model (--mlx)";
        if (model.mlx_video == false) {
            out +=
                ", and is not installed in Apogee's Python environment -- 'apogee train setup "
                "--with mlx-video'";
        }
    }
    // And what runs the model today: the pipeline Wan publishes beside the
    // original layout, by its repository's name and without its revision.
    const std::string repository = model.ref.substr(0, model.ref.find('@'));
    out +=
        "; llama.cpp's converter takes no diffusion model -- it finds a model by its "
        "architectures; what runs this model today is its Diffusers pipeline in 'apogee video "
        "generate', pulled whole";
    out += repository.empty() ? std::string{" (a repository with model_index.json at its root)"}
                              : " with 'apogee models pull " + repository + "-Diffusers'";
    return out;
}

std::string diffusion_model_refusal(std::string_view subject, const DiffusionModel& model) {
    return std::string{subject} + " is " + describe_diffusion_model(model);
}

std::optional<EmbeddingModel> snapshot_embedding_model(const std::filesystem::path& dir) {
    std::ifstream in{dir / "modules.json", std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    const nlohmann::json modules = nlohmann::json::parse(in, nullptr, false);
    if (!modules.is_array()) {
        return std::nullopt;
    }
    EmbeddingModel model;
    bool pooled = false;
    for (const nlohmann::json& module : modules) {
        if (!module.is_object() || !module.contains("type") || !module["type"].is_string()) {
            continue;
        }
        const std::string type = module["type"].get<std::string>();
        const std::string word = type.substr(type.rfind('.') + 1);
        model.modules.push_back(word);
        pooled = pooled || word == "Pooling";
        // The converter takes a Dense module only when its weights are there
        // (conversion/gemma.py).
        std::error_code code;
        if (word == "Dense" && module.contains("path") && module["path"].is_string() &&
            std::filesystem::is_regular_file(
                dir / module["path"].get<std::string>() / "model.safetensors", code)) {
            model.dense = true;
        }
    }
    if (!pooled) {
        return std::nullopt;
    }
    return model;
}

std::string describe_embedding_model(const EmbeddingModel& model) {
    std::string modules;
    for (const std::string& word : model.modules) {
        modules += (modules.empty() ? "" : ", ") + word;
    }
    return "an embedding model (sentence-transformers: " + modules +
           "): it converts to a GGUF a llamacpp backend embeds with -- mlx-lm's converter builds "
           "language models, and the mlx backend does not embed";
}

std::string embedding_model_mlx_refusal(std::string_view subject, const EmbeddingModel& model) {
    return std::string{subject} + " is " + describe_embedding_model(model) +
           " -- convert it without --mlx";
}

std::string describe_embedding_model_both(const EmbeddingModel& model) {
    std::string modules;
    for (const std::string& word : model.modules) {
        modules += (modules.empty() ? "" : ", ") + word;
    }
    return "an embedding model (sentence-transformers: " + modules +
           "): it converts to a GGUF a llamacpp backend embeds with, and through mlx-embeddings "
           "to an MLX model an mlx backend embeds with";
}

std::vector<std::string> snapshot_conversions(const std::filesystem::path& dir,
                                              const EmbeddingBuilds& mlx_embeddings,
                                              const DrafterBuilds& mlx_drafters,
                                              const VideoConverts& mlx_video) {
    if (const std::optional<DraftHead> head = snapshot_draft_head(dir)) {
        // An MLX drafter where the installed mlx-vlm drafts its type (53i),
        // never a GGUF: llama.cpp's would be no backend Apogee runs.
        if (mlx_drafters && !head->model_type.empty() && mlx_drafters(head->model_type)) {
            return {std::string{kMlxFormat}};
        }
        return {};
    }
    if (const std::optional<DiffusionModel> diffusion = snapshot_diffusion_model(dir, mlx_video)) {
        // An MLX video model where mlx-video converts it (53k): Wan's
        // original layout, whole, mlx-video installed. Never a GGUF.
        if (diffusion->mlx_convertible()) {
            return {std::string{kMlxFormat}};
        }
        return {};
    }
    if (snapshot_embedding_model(dir).has_value()) {
        // M21's answer, unless the installed mlx-embeddings builds its type
        // (53h): then MLX joins the GGUF, offered second.
        const nlohmann::json config = read_model_config(dir);
        const std::string type =
            config.is_object() && config.contains("model_type") && config["model_type"].is_string()
                ? config["model_type"].get<std::string>()
                : std::string{};
        if (mlx_embeddings && !type.empty() && mlx_embeddings(type)) {
            return {std::string{kGgufFormat}, std::string{kMlxFormat}};
        }
        return {std::string{kGgufFormat}};
    }
    return {std::string{kGgufFormat}, std::string{kMlxFormat}};
}

std::vector<std::string> snapshot_towers(const std::filesystem::path& dir) {
    std::vector<std::string> out;
    const nlohmann::json config = read_model_config(dir);
    if (!config.is_object()) {
        return out;
    }
    // As the GGUF path reads its encoders (`models::snapshot_encoders`): a
    // config that declares itself its language model alone has none, and a
    // key set to null, to an empty object or to anything else declares none.
    if (const auto only = config.find("language_model_only");
        only != config.end() && only->is_boolean() && only->get<bool>()) {
        return out;
    }
    for (const char* key : {"vision_config", "audio_config", "thinker_config"}) {
        if (const auto found = config.find(key);
            found != config.end() && found->is_object() && !found->empty()) {
            out.emplace_back(key);
        }
    }
    return out;
}

std::string describe_towers(const std::vector<std::string>& towers) {
    std::string out;
    for (const std::string& tower : towers) {
        out += (out.empty() ? "" : ", ") + tower;
    }
    return out;
}

bool is_download_record(const std::filesystem::path& file) {
    std::ifstream in{file, std::ios::binary};
    if (!in) {
        return false;
    }
    const nlohmann::json record = nlohmann::json::parse(in, nullptr, false);
    // A sidecar's own fields; no model configuration carries them.
    return record.is_object() && record.contains("source_url") && record.contains("verification") &&
           record.contains("file_digest");
}

bool config_is_download_record(const std::filesystem::path& dir) {
    return is_download_record(dir / "config.json");
}

SnapshotDamage find_snapshot_damage(const std::filesystem::path& dir) {
    SnapshotDamage damage;
    const std::optional<Snapshot> record = load_snapshot(dir);
    if (!record.has_value()) {
        damage.error = "no " + snapshot_record_path(dir).filename().string() + " in " +
                       dir.string() + ", so there is nothing to judge its files against";
        return damage;
    }
    std::set<std::string> listed;
    for (const SnapshotFile& file : record->files) {
        listed.insert(file.path);
    }
    std::error_code code;
    for (const auto& entry : std::filesystem::recursive_directory_iterator(dir, code)) {
        if (!entry.is_regular_file(code) || entry.path().extension() != ".json" ||
            entry.path() == snapshot_record_path(dir)) {
            continue;
        }
        const std::string relative = entry.path().lexically_relative(dir).generic_string();
        if (!listed.contains(relative) && is_download_record(entry.path())) {
            damage.stray_records.push_back(entry.path());
        }
    }
    std::ranges::sort(damage.stray_records);
    // Small files are hashed as well as sized: a tokenizer replaced by a record
    // of the right size would otherwise pass. Shards are sized only -- hashing
    // fifty gigabytes to repair a configuration file is not a repair.
    constexpr std::int64_t kHashBelow = 64LL * 1024 * 1024;
    for (const SnapshotFile& file : record->files) {
        const std::filesystem::path path = dir / file.path;
        const bool broken = [&] {
            if (!std::filesystem::is_regular_file(path, code)) {
                return true;
            }
            const auto size = static_cast<std::int64_t>(std::filesystem::file_size(path, code));
            if (file.size > 0 && size != file.size) {
                return true;
            }
            if (is_download_record(path)) {
                return true;
            }
            return !file.sha256.empty() && size < kHashBelow && file_sha256(path) != file.sha256;
        }();
        if (broken) {
            damage.refetch.push_back(file);
        }
    }
    return damage;
}

SnapshotRepair repair_snapshot(const std::filesystem::path& dir, const SnapshotDamage& damage,
                               const SnapshotFetchFn& fetch) {
    SnapshotRepair repair;
    std::error_code code;
    for (const SnapshotFile& file : damage.refetch) {
        const std::filesystem::path path = dir / file.path;
        std::filesystem::path fresh = path;
        fresh += ".repair";
        std::filesystem::remove(fresh, code);
        std::string error = fetch(file, fresh);
        if (error.empty() && file.size > 0 &&
            static_cast<std::int64_t>(std::filesystem::file_size(fresh, code)) != file.size) {
            error = "the fetched copy is not the recorded size";
        }
        if (error.empty() && !file.sha256.empty() && file_sha256(fresh) != file.sha256) {
            error = "the fetched copy does not match the recorded sha256";
        }
        if (error.empty()) {
            std::filesystem::rename(fresh, path, code);
            if (code) {
                error = "could not put it in place: " + code.message();
            }
        }
        if (!error.empty()) {
            std::filesystem::remove(fresh, code);
            repair.error = file.path + ": " + error;
            return repair;
        }
        repair.fetched.push_back(file.path);
    }
    for (const std::filesystem::path& stray : damage.stray_records) {
        if (std::filesystem::remove(stray, code)) {
            repair.removed.push_back(stray.lexically_relative(dir).generic_string());
        }
    }
    return repair;
}

std::string damaged_snapshot_error(const std::filesystem::path& dir) {
    if (!config_is_download_record(dir)) {
        return {};
    }
    return dir.string() +
           "'s config.json is an Apogee download record, not the model's configuration: "
           "'models pull --safetensors' replaced every JSON file of a snapshot this way until "
           "2026-09-23. Delete it and pull it again";
}

std::optional<std::int64_t> snapshot_elements(
    const std::filesystem::path& dir, const std::function<bool(std::string_view)>& include) {
    // A header past this is not a SafeTensors header: the format caps it at
    // 100 MB, and reading an arbitrary length would be reading the weights.
    constexpr std::uint64_t kMaxHeader = 100ULL * 1024 * 1024;
    std::int64_t total = 0;
    bool any = false;
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (code) {
            return std::nullopt;
        }
        if (!entry.is_regular_file(code) || entry.path().extension() != ".safetensors") {
            continue;
        }
        std::ifstream in{entry.path(), std::ios::binary};
        std::array<char, 8> length_bytes{};
        if (!in.read(length_bytes.data(), length_bytes.size())) {
            return std::nullopt;
        }
        std::uint64_t length = 0;  // little-endian
        for (std::size_t i = 0; i < length_bytes.size(); ++i) {
            length |= static_cast<std::uint64_t>(static_cast<unsigned char>(length_bytes.at(i)))
                      << (8 * i);
        }
        if (length == 0 || length > kMaxHeader) {
            return std::nullopt;
        }
        std::string header(static_cast<std::size_t>(length), '\0');
        if (!in.read(header.data(), static_cast<std::streamsize>(length))) {
            return std::nullopt;
        }
        const nlohmann::json table = nlohmann::json::parse(header, nullptr, false);
        if (!table.is_object()) {
            return std::nullopt;
        }
        for (const auto& [name, tensor] : table.items()) {
            if (name == "__metadata__" || !tensor.is_object() || (include && !include(name))) {
                continue;
            }
            const auto shape = tensor.find("shape");
            if (shape == tensor.end() || !shape->is_array()) {
                return std::nullopt;
            }
            std::int64_t elements = 1;
            for (const nlohmann::json& dimension : *shape) {
                if (!dimension.is_number_integer() || dimension.get<std::int64_t>() < 0) {
                    return std::nullopt;
                }
                elements *= dimension.get<std::int64_t>();
            }
            total += elements;
        }
        any = true;
    }
    if (!any) {
        return std::nullopt;
    }
    return total;
}

std::vector<std::filesystem::path> list_snapshots(const std::filesystem::path& root) {
    std::vector<std::filesystem::path> out;
    std::error_code code;
    if (!std::filesystem::is_directory(root, code)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(root, code)) {
        if (code) {
            break;
        }
        if (entry.is_directory(code) && is_snapshot_dir(entry.path())) {
            out.push_back(entry.path());
        }
    }
    std::ranges::sort(out);
    return out;
}

}  // namespace apogee::models
