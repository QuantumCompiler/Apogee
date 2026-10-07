#include "modelstore/mlx_info.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <set>
#include <string_view>
#include <system_error>

#include "modelstore/kv_cache.h"
#include "modelstore/snapshot.h"

namespace apogee::models {
namespace {

/// A SafeTensors header past this is not one: the format caps it at 100 MB,
/// and reading an arbitrary length would be reading the weights.
constexpr std::uint64_t kMaxHeader = 100ULL * 1024 * 1024;

[[nodiscard]] nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return nlohmann::json{};
    }
    const nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);
    return parsed.is_discarded() ? nlohmann::json{} : parsed;
}

[[nodiscard]] const nlohmann::json* object_at(const nlohmann::json& parent, const char* key) {
    if (!parent.is_object()) {
        return nullptr;
    }
    const auto found = parent.find(key);
    return found != parent.end() && found->is_object() ? &*found : nullptr;
}

[[nodiscard]] std::string string_at(const nlohmann::json& parent, const char* key) {
    if (!parent.is_object()) {
        return {};
    }
    const auto found = parent.find(key);
    return found != parent.end() && found->is_string() ? found->get<std::string>() : std::string{};
}

[[nodiscard]] int int_at(const nlohmann::json& object, const char* key) {
    const auto found = object.find(key);
    return found != object.end() && found->is_number_integer() ? found->get<int>() : 0;
}

/// `bfloat16` as a name carries it: `bf16`.
[[nodiscard]] std::string short_dtype(std::string_view dtype) {
    if (dtype == "bfloat16") {
        return "bf16";
    }
    if (dtype == "float16") {
        return "f16";
    }
    if (dtype == "float32") {
        return "f32";
    }
    return std::string{dtype};
}

/// The window, from the first container that states one: the top level
/// (what `mlx-lm` reads for a text model), then a composite's text model
/// (`text_config`, `language_config`, `llm_config`, an omni model's
/// `thinker_config.text_config`). Every key a Hugging Face config uses for
/// it, in the order they are common.
void read_window(const nlohmann::json& config, MlxInfo& info) {
    std::vector<std::pair<std::string, const nlohmann::json*>> containers{{"", &config}};
    for (const char* key : {"text_config", "language_config", "llm_config"}) {
        if (const nlohmann::json* found = object_at(config, key)) {
            containers.emplace_back(std::string{key} + ".", found);
        }
    }
    if (const nlohmann::json* thinker = object_at(config, "thinker_config")) {
        if (const nlohmann::json* text = object_at(*thinker, "text_config")) {
            containers.emplace_back("thinker_config.text_config.", text);
        }
    }
    constexpr std::array<const char*, 6> keys{"max_position_embeddings",
                                              "n_positions",
                                              "max_sequence_length",
                                              "seq_length",
                                              "max_seq_len",
                                              "n_ctx"};
    for (const auto& [prefix, container] : containers) {
        for (const char* key : keys) {
            const auto found = container->find(key);
            if (found != container->end() && found->is_number_integer() &&
                found->get<std::int64_t>() > 0) {
                info.context_length = found->get<std::int64_t>();
                info.context_key = prefix + key;
                return;
            }
        }
    }
}

/// `mlx-lm`'s `quantization` (or its copy under `quantization_config`): the
/// default bits, group and mode, and any layer quantized otherwise -- a
/// mixed recipe writes those as objects keyed by the layer's path.
void read_quantization(const nlohmann::json& config, MlxInfo& info) {
    MlxQuantization& quant = info.quantization;
    std::string dtype = string_at(config, "torch_dtype");
    if (dtype.empty()) {
        dtype = string_at(config, "dtype");
    }
    if (const nlohmann::json* text = object_at(config, "text_config");
        dtype.empty() && text != nullptr) {
        dtype = string_at(*text, "dtype");
        if (dtype.empty()) {
            dtype = string_at(*text, "torch_dtype");
        }
    }
    quant.dtype = short_dtype(dtype);

    const nlohmann::json* scheme = object_at(config, "quantization");
    if (scheme == nullptr || int_at(*scheme, "bits") <= 0) {
        const nlohmann::json* published = object_at(config, "quantization_config");
        scheme = published != nullptr && int_at(*published, "bits") > 0 ? published : nullptr;
        if (scheme == nullptr && published != nullptr) {
            quant.method = string_at(*published, "quant_method");
        }
    }
    if (scheme == nullptr) {
        return;
    }
    info.mlx_format = true;
    quant.bits = int_at(*scheme, "bits");
    quant.group_size = int_at(*scheme, "group_size");
    quant.mode = string_at(*scheme, "mode");
    for (const auto& [key, layer] : scheme->items()) {
        if (!layer.is_object()) {
            continue;
        }
        const int bits = int_at(layer, "bits");
        if (bits > 0 && bits != quant.bits) {
            quant.mixed = true;
            quant.high_bits = std::max(quant.high_bits, bits);
        }
    }
}

/// The 8-byte little-endian length a SafeTensors file opens with.
[[nodiscard]] std::optional<std::uint64_t> header_length(std::ifstream& in) {
    std::array<char, 8> bytes{};
    if (!in.read(bytes.data(), bytes.size())) {
        return std::nullopt;
    }
    std::uint64_t length = 0;
    for (std::size_t i = 0; i < bytes.size(); ++i) {
        length |= static_cast<std::uint64_t>(static_cast<unsigned char>(bytes.at(i))) << (8 * i);
    }
    return length;
}

/// Whether `shard`'s header reads and every tensor it lists lies within the
/// file -- the check that tells a whole shard from a half-downloaded one with
/// a perfectly good start. The problem, or empty; `mlx` set when the shard
/// was saved by `mlx-lm` (`__metadata__.format`).
[[nodiscard]] std::string check_shard(const std::filesystem::path& shard, std::uintmax_t size,
                                      bool& mlx) {
    const std::string name = shard.filename().string();
    std::ifstream in{shard, std::ios::binary};
    const std::optional<std::uint64_t> length = header_length(in);
    if (!length.has_value() || *length == 0 || *length > kMaxHeader || 8 + *length > size) {
        return name + " is truncated or not a SafeTensors file (its header does not fit in its " +
               std::to_string(size) + " bytes)";
    }
    std::string header(static_cast<std::size_t>(*length), '\0');
    if (!in.read(header.data(), static_cast<std::streamsize>(header.size()))) {
        return name + " is truncated (its header could not be read)";
    }
    const nlohmann::json table = nlohmann::json::parse(header, nullptr, false);
    if (!table.is_object()) {
        return name + " has a SafeTensors header that is not a JSON table";
    }
    std::uint64_t end = 0;
    for (const auto& [key, tensor] : table.items()) {
        if (key == "__metadata__") {
            if (string_at(tensor, "format") == "mlx") {
                mlx = true;
            }
            continue;
        }
        const nlohmann::json* offsets = nullptr;
        if (tensor.is_object()) {
            const auto found = tensor.find("data_offsets");
            offsets = found != tensor.end() && found->is_array() && found->size() == 2 ? &*found
                                                                                       : nullptr;
        }
        if (offsets == nullptr || !offsets->at(1).is_number_unsigned()) {
            return name + " lists tensor '" + key + "' without its data offsets";
        }
        end = std::max(end, offsets->at(1).get<std::uint64_t>());
    }
    const std::uint64_t needed = 8 + *length + end;
    if (needed > size) {
        return name + " is truncated: its header describes " + std::to_string(needed) +
               " bytes and the file holds " + std::to_string(size);
    }
    return {};
}

/// The weights: at least one shard, every shard the index names present,
/// and each one whole -- the problem, or empty. Marks `info.mlx_format` when
/// a shard was saved by `mlx-lm`.
[[nodiscard]] std::string check_weights(const std::filesystem::path& dir, MlxInfo& info) {
    if (info.shards.empty()) {
        return "no weights (*.safetensors) in " + dir.string();
    }
    std::error_code code;
    const nlohmann::json index = read_json(dir / "model.safetensors.index.json");
    if (const nlohmann::json* map = object_at(index, "weight_map")) {
        std::set<std::string> named;
        for (const auto& [tensor, shard] : map->items()) {
            if (shard.is_string()) {
                named.insert(shard.get<std::string>());
            }
        }
        for (const std::string& shard : named) {
            if (!std::filesystem::is_regular_file(dir / shard, code)) {
                return shard + " is missing (model.safetensors.index.json names it)";
            }
        }
    }
    for (const MlxShard& shard : info.shards) {
        bool mlx = false;
        if (std::string problem = check_shard(dir / shard.name, shard.bytes, mlx);
            !problem.empty()) {
            return problem;
        }
        info.mlx_format = info.mlx_format || mlx;
    }
    return {};
}

[[nodiscard]] bool has_tokenizer(const std::filesystem::path& dir) {
    std::error_code code;
    for (const char* name : {"tokenizer.json", "tokenizer.model", "tokenizer_config.json",
                             "vocab.json", "tekken.json"}) {
        if (std::filesystem::is_regular_file(dir / name, code)) {
            return true;
        }
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (entry.is_regular_file(code) && entry.path().extension() == ".tiktoken") {
            return true;
        }
    }
    return false;
}

}  // namespace

std::string MlxQuantization::describe() const {
    if (bits > 0) {
        const std::string group = group_size > 0 ? "group " + std::to_string(group_size) : "";
        if (mixed) {
            return "mixed " + std::to_string(bits) + "/" + std::to_string(high_bits) + "-bit" +
                   (group.empty() ? "" : " (" + group + ")");
        }
        if (!mode.empty() && mode != "affine") {
            return mode + (group.empty() ? "" : " (" + group + ")");
        }
        return std::to_string(bits) + "-bit (affine" + (group.empty() ? "" : ", " + group) + ")";
    }
    if (!method.empty()) {
        return method + " (as published)";
    }
    return dtype.empty() ? "precision not stated" : dtype;
}

std::string MlxQuantization::label() const {
    if (bits > 0) {
        if (mixed) {
            return "mixed_" + std::to_string(bits) + "_" + std::to_string(high_bits);
        }
        if (!mode.empty() && mode != "affine") {
            return mode;
        }
        return std::to_string(bits) + "bit";
    }
    return method.empty() ? dtype : method;
}

namespace {

/// `config.json`'s facts into `info`, or the reason it cannot be read.
/// True when it was.
bool read_config(const std::filesystem::path& dir, MlxInfo& info) {
    std::error_code code;
    if (!std::filesystem::is_regular_file(dir / "config.json", code)) {
        info.problem = "cannot load: no config.json in " + dir.string();
        return false;
    }
    const nlohmann::json config = read_json(dir / "config.json");
    if (!config.is_object()) {
        info.problem = "cannot load: config.json is not a JSON object";
        return false;
    }
    if (config_is_download_record(dir)) {
        info.problem =
            "cannot load: config.json is an Apogee download record, not the model's "
            "configuration -- delete it and pull it again";
        return false;
    }
    info.config_read = true;
    info.model_type = string_at(config, "model_type");
    if (const nlohmann::json* text = object_at(config, "text_config")) {
        info.text_model_type = string_at(*text, "model_type");
    }
    if (const auto architectures = config.find("architectures");
        architectures != config.end() && architectures->is_array() && !architectures->empty() &&
        architectures->front().is_string()) {
        info.architecture = architectures->front().get<std::string>();
    }
    read_window(config, info);
    read_quantization(config, info);
    return true;
}

}  // namespace

MlxInfo read_mlx_config(const std::filesystem::path& dir) {
    MlxInfo info;
    (void)read_config(dir, info);
    return info;
}

MlxInfo read_mlx_info(const std::filesystem::path& dir) {
    MlxInfo info;
    std::error_code code;
    if (!std::filesystem::is_directory(dir, code)) {
        info.problem = "cannot load: " + dir.string() + " is not a directory";
        return info;
    }
    for (auto it = std::filesystem::recursive_directory_iterator(dir, code);
         !code && it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
        if (it->is_regular_file(code)) {
            const std::uintmax_t size = it->file_size(code);
            info.bytes += size;
            if (it.depth() == 0 && it->path().extension() == ".safetensors") {
                info.shards.push_back({.name = it->path().filename().string(), .bytes = size});
            }
        }
    }
    std::ranges::sort(info.shards,
                      [](const MlxShard& a, const MlxShard& b) { return a.name < b.name; });

    // The configuration first: without it there is no model to describe.
    if (!read_config(dir, info)) {
        return info;
    }

    // Every shard whole, and every shard the index names present.
    if (const std::string problem = check_weights(dir, info); !problem.empty()) {
        info.problem = "cannot load: " + problem;
        return info;
    }
    if (!has_tokenizer(dir)) {
        info.problem =
            "cannot load: no tokenizer (tokenizer.json, tokenizer.model or "
            "tokenizer_config.json) in " +
            dir.string();
        return info;
    }
    info.complete = true;
    return info;
}

bool is_mlx_model_dir(const std::filesystem::path& dir) {
    if (!is_snapshot_dir(dir)) {
        return false;
    }
    return read_mlx_info(dir).mlx_format;
}

MlxWindow mlx_window(const MlxInfo& info, const harness::BackendConfig& backend) {
    MlxWindow window;
    window.trained = info.context_length;
    if (backend.context_size.has_value() && *backend.context_size > 0) {
        window.window = *backend.context_size;
        window.configured = true;
        return window;
    }
    // A model that declares no window gets the default, as a GGUF with no
    // trained length does (26a): an unknown trained window limits nothing.
    window.window = info.config_read ? default_local_window(info.context_length, 0) : 0;
    return window;
}

std::string describe(const MlxWindow& window) {
    if (window.window <= 0) {
        return "window unknown -- config.json could not be read; set context_size";
    }
    std::string out = std::to_string(window.window) + "-token window (" +
                      (window.configured ? "context_size" : "the default");
    if (window.trained > 0) {
        out += "; trained for " + std::to_string(window.trained);
    }
    return out + ")";
}

}  // namespace apogee::models
