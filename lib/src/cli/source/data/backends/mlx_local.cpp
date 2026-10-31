#include "backends/mlx_local.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <random>
#include <regex>
#include <sstream>
#include <system_error>
#include <utility>

#include "backends/local_prompt.h"
#include "backends/markup_filter.h"
#include "backends/native_tool_calls.h"
#include "backends/think_filter.h"
#include "contracts/assets.h"
#include "contracts/errors.h"
#include "contracts/layout.h"
#include "contracts/paths.h"
#include "logger/operational.h"
#include "modelstore/mlx_info.h"
#include "modelstore/snapshot.h"
#include "platform/platform.h"

namespace apogee::backends {
namespace {

/// The most of the driver's stderr kept for a failure message -- the vendor
/// CLIs' bound, since `mlx-lm` and `transformers` warn as freely.
constexpr std::size_t kStderrTailBytes = 8192;

/// How long one read waits before the loop looks at the cancellation token.
constexpr std::chrono::milliseconds kPoll{100};

constexpr std::string_view kSetupRemedy = "apogee train setup --with mlx";

[[nodiscard]] std::optional<std::string> read_file(const std::filesystem::path& path) {
    const std::ifstream in{path, std::ios::binary};
    if (!in) {
        return std::nullopt;
    }
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

[[nodiscard]] nlohmann::json read_json(const std::filesystem::path& path) {
    const std::optional<std::string> text = read_file(path);
    if (!text.has_value()) {
        return nlohmann::json{};
    }
    const nlohmann::json parsed = nlohmann::json::parse(*text, nullptr, false);
    return parsed.is_discarded() ? nlohmann::json{} : parsed;
}

/// `<venv>/lib/python3.*/site-packages/<name>`, when its `__init__.py` is
/// there -- a fact about files, asked without starting an interpreter, so a
/// probe at every startup costs a directory listing.
[[nodiscard]] std::filesystem::path find_package(const std::filesystem::path& venv,
                                                 std::string_view name) {
    std::error_code code;
    const std::filesystem::path lib = venv / "lib";
    if (!std::filesystem::is_directory(lib, code)) {
        return {};
    }
    for (const auto& entry : std::filesystem::directory_iterator(lib, code)) {
        if (!entry.path().filename().string().starts_with("python")) {
            continue;
        }
        const std::filesystem::path package = entry.path() / "site-packages" / std::string{name};
        if (std::filesystem::is_regular_file(package / "__init__.py", code)) {
            return package;
        }
    }
    return {};
}

/// The version `<package>/<file>` declares (`__version__ = '0.32.0'`), or
/// empty when it says none.
[[nodiscard]] std::string declared_version(const std::filesystem::path& package,
                                           std::string_view file) {
    const std::optional<std::string> text = read_file(package / std::string{file});
    if (!text.has_value()) {
        return {};
    }
    const std::size_t name = text->find("__version__");
    const std::size_t equals = name == std::string::npos ? name : text->find('=', name);
    const std::size_t open =
        equals == std::string::npos ? equals : text->find_first_of("'\"", equals);
    if (open == std::string::npos) {
        return {};
    }
    const std::size_t close = text->find((*text)[open], open + 1);
    return close == std::string::npos ? std::string{} : text->substr(open + 1, close - open - 1);
}

/// The version a package's distribution record names in its directory's
/// own name -- `mlx_vlm-0.3.9.dist-info` beside `mlx_vlm/` -- or, failing
/// that, what the package's `version.py` or `_version.py` declares.
[[nodiscard]] std::string installed_version(const std::filesystem::path& package) {
    const std::string prefix = package.filename().string() + "-";
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(package.parent_path(), code)) {
        const std::string name = entry.path().filename().string();
        const std::string_view suffix = ".dist-info";
        if (name.starts_with(prefix) && name.ends_with(suffix) &&
            name.size() > prefix.size() + suffix.size()) {
            return name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
        }
    }
    for (const std::string_view file : {"version.py", "_version.py"}) {
        if (std::string version = declared_version(package, file); !version.empty()) {
            return version;
        }
    }
    return {};
}

/// A directory mlx-vlm built, by its record (53f): it loads through mlx-vlm
/// for every turn. One recorded otherwise, or not at all, keeps its way.
[[nodiscard]] bool loads_through_vlm(const models::MlxKind& kind) noexcept {
    return kind.word == models::kMlxVision && kind.source == models::MlxKindSource::Recorded;
}

/// The platform, the environment and `mlx-lm`: the rungs an entry shares
/// with every other.
[[nodiscard]] MlxReadiness probe_environment(const MlxHost& host) {
    MlxReadiness readiness;
    if (host.target != kMlxTarget) {
        readiness.refusal = MlxRefusal::Platform;
        readiness.reason = "the mlx backend runs on Apple silicon macOS only, and this build is " +
                           (host.target.empty() ? std::string{"another platform"} : host.target);
        readiness.remedy = "use a llamacpp backend for local models on this platform";
        return readiness;
    }
    std::error_code code;
    readiness.interpreter = host.venv / "bin" / "python";
    if (!std::filesystem::is_regular_file(readiness.interpreter, code)) {
        readiness.refusal = MlxRefusal::NoEnvironment;
        readiness.reason = "there is no Python environment at " + host.venv.string() +
                           " -- the mlx backend runs mlx-lm there, never the system Python";
        readiness.remedy = std::string{kSetupRemedy};
        return readiness;
    }
    readiness.package = find_package(host.venv, "mlx_lm");
    if (readiness.package.empty()) {
        readiness.refusal = MlxRefusal::NoMlxLm;
        readiness.reason =
            "mlx-lm is not installed in the Python environment at " + host.venv.string();
        readiness.remedy = std::string{kSetupRemedy};
        return readiness;
    }
    readiness.version = declared_version(readiness.package, "_version.py");
    return readiness;
}

void refuse_driver(MlxReadiness& readiness, const std::filesystem::path& driver) {
    std::error_code code;
    if (!std::filesystem::is_regular_file(driver, code)) {
        readiness.refusal = MlxRefusal::NoDriver;
        readiness.reason = "the MLX driver is missing: " + driver.string();
        readiness.remedy = "apogee check --fix";
    }
}

/// Says a line where the surface shows notices (`--verbose`, a chat's own).
void notice(const harness::StreamOptions& options, std::string text) {
    if (!options.on_status) {
        return;
    }
    harness::StatusEvent event;
    event.type = harness::StatusEvent::Type::Notice;
    event.phase = harness::StatusEvent::Phase::Done;
    event.detail = std::move(text);
    options.on_status(event);
}

/// What a call id is made of.
constexpr std::string_view kCallIdAlphabet =
    "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789";

/// An id for a call the driver's format left without one: nine letters and
/// digits, as the llamacpp backend makes them, so a call and its result match
/// when the transcript renders again.
[[nodiscard]] std::string make_call_id() {
    thread_local std::mt19937 generator{std::random_device{}()};
    std::uniform_int_distribution<std::size_t> pick{0, kCallIdAlphabet.size() - 1};
    std::string id(9, '0');
    for (char& c : id) {
        c = kCallIdAlphabet[pick(generator)];
    }
    return id;
}

/// The last non-empty line of `text`, so a failure fits one status line.
[[nodiscard]] std::string last_line(std::string_view text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.remove_suffix(1);
    }
    const std::size_t cut = text.find_last_of('\n');
    return std::string{cut == std::string_view::npos ? text : text.substr(cut + 1)};
}

/// The spellings of an HF `model_type` the profiles' architecture lists use:
/// as written, dashed, and joined (`gpt_oss` is the GGUF's `gpt-oss`).
[[nodiscard]] std::vector<std::string> architecture_spellings(std::string_view model_type) {
    std::vector<std::string> out;
    if (model_type.empty()) {
        return out;
    }
    std::string dashed{model_type};
    std::string joined;
    for (char& c : dashed) {
        if (c == '_') {
            c = '-';
        }
    }
    for (const char c : model_type) {
        if (c != '_') {
            joined += c;
        }
    }
    out.emplace_back(model_type);
    out.push_back(dashed);
    out.push_back(joined);
    return out;
}

/// Whether `towers` holds an audio tower: an `audio_config`, or an omni
/// model's `thinker_config`, which nests the audio encoder (53f's reader).
[[nodiscard]] bool declares_audio_tower(const std::vector<std::string>& towers) {
    return std::ranges::any_of(towers, [](const std::string& tower) {
        return tower == "audio_config" || tower == "thinker_config";
    });
}

/// The module `model_type` loads by in mlx-vlm's `MODEL_REMAPPING`
/// (`"<type>": "<module>"` pairs inside its braces), or empty when the table
/// does not name it.
[[nodiscard]] std::string remapped_type(const std::filesystem::path& utils,
                                        std::string_view model_type) {
    const std::optional<std::string> text = read_file(utils);
    if (!text.has_value()) {
        return {};
    }
    const std::size_t named = text->find("MODEL_REMAPPING");
    const std::size_t open = named == std::string::npos ? named : text->find('{', named);
    const std::size_t close = open == std::string::npos ? open : text->find('}', open);
    if (close == std::string::npos) {
        return {};
    }
    const std::string body = text->substr(open + 1, close - open - 1);
    static const std::regex pair{
        R"re(["']([A-Za-z0-9_.-]+)["']\s*:\s*["']([A-Za-z0-9_.-]+)["'])re"};
    for (auto match = std::sregex_iterator(body.begin(), body.end(), pair);
         match != std::sregex_iterator(); ++match) {
        if ((*match)[1].str() == model_type) {
            return (*match)[2].str();
        }
    }
    return {};
}

/// Whether an audio part's container is a WAV: what Apogee's surfaces decode
/// every recording to through ffmpeg (26e). No format said is taken for one,
/// and the bytes are checked either way.
[[nodiscard]] bool says_wav(std::string_view format) {
    std::string folded{format};
    std::ranges::transform(folded, folded.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return folded.empty() || folded == "wav";
}

/// Whether `bytes` are a WAV file: a RIFF container of WAVE.
[[nodiscard]] bool is_wav(std::string_view bytes) {
    return bytes.size() >= 12 && bytes.substr(0, 4) == "RIFF" && bytes.substr(8, 4) == "WAVE";
}

/// A turn's recordings as the driver hears them (53g): each audio part's WAV
/// written into a private directory of the turn's own, in prompt order, and
/// the directory removed with everything in it when the turn is over --
/// answered, refused, cancelled or failed. The driver reads the files; only
/// this side writes or removes them.
class TurnAudio {
public:
    TurnAudio() = default;
    TurnAudio(const TurnAudio&) = delete;
    TurnAudio& operator=(const TurnAudio&) = delete;
    TurnAudio(TurnAudio&&) = delete;
    TurnAudio& operator=(TurnAudio&&) = delete;

    ~TurnAudio() {
        if (!dir_.empty()) {
            std::error_code code;
            std::filesystem::remove_all(dir_, code);
        }
    }

    /// Writes every audio part of `request`, decoded from its base64. Throws
    /// ProviderError, naming `backend`, for a part that is not a WAV or a
    /// file that cannot be written -- before any driver is asked anything.
    void write(const harness::ChatRequest& request, const std::string& backend) {
        for (const harness::ChatMessage& message : request.messages) {
            for (const harness::ContentPart& part : message.content.parts()) {
                if (part.kind != harness::ContentPart::Kind::InputAudio) {
                    continue;
                }
                const std::string bytes = decode_base64(part.audio_data);
                if (!is_wav(bytes)) {
                    throw harness::ProviderError(
                        backend,
                        "a recording reaches the mlx backend as a WAV file, and this one is not "
                        "one -- Apogee's attachments are decoded to WAV through ffmpeg first");
                }
                const std::filesystem::path file =
                    directory(backend) / ("audio-" + std::to_string(files_.size()) + ".wav");
                std::ofstream out{file, std::ios::binary};
                out << bytes;
                out.close();
                if (!out) {
                    throw harness::ProviderError(
                        backend,
                        "could not write a recording for the MLX driver: " + file.string());
                }
                files_.push_back(file.string());
            }
        }
    }

    [[nodiscard]] const std::vector<std::string>& files() const noexcept {
        return files_;
    }

private:
    /// The turn's directory, made on first use: under the system's
    /// temporary directory, readable by its owner alone.
    [[nodiscard]] const std::filesystem::path& directory(const std::string& backend) {
        if (!dir_.empty()) {
            return dir_;
        }
        std::error_code code;
        const std::filesystem::path root = std::filesystem::temp_directory_path(code);
        thread_local std::mt19937_64 generator{std::random_device{}()};
        for (int attempt = 0; attempt < 8 && !code; ++attempt) {
            std::ostringstream name;
            name << "apogee-mlx-audio-" << std::hex << generator();
            const std::filesystem::path candidate = root / name.str();
            if (std::filesystem::create_directory(candidate, code) && !code) {
                std::filesystem::permissions(candidate, std::filesystem::perms::owner_all,
                                             std::filesystem::perm_options::replace, code);
                dir_ = candidate;
                return dir_;
            }
        }
        throw harness::ProviderError(
            backend, "could not make a directory for the turn's recordings under " + root.string() +
                         (code ? ": " + code.message() : std::string{}));
    }

    std::filesystem::path dir_;
    std::vector<std::string> files_;
};

}  // namespace

// ---------------------------------------------------------------------------
// The refusal ladder
// ---------------------------------------------------------------------------

MlxHost MlxHost::at(const std::filesystem::path& home) {
    MlxHost host;
    host.target = platform::host_target();
    host.venv = home / "training" / "venv";
    host.driver = home / harness::bundled_mlx_driver_relative_path();
    host.embedder = home / harness::bundled_mlx_embedder_relative_path();
    host.models = home / "models";
    return host;
}

MlxHost MlxHost::current() {
    return at(harness::apogee_home());
}

std::string_view to_string(MlxRefusal refusal) noexcept {
    switch (refusal) {
        case MlxRefusal::None:
            return "ready";
        case MlxRefusal::Platform:
            return "platform";
        case MlxRefusal::NoEnvironment:
            return "no environment";
        case MlxRefusal::NoMlxLm:
            return "no mlx-lm";
        case MlxRefusal::NoModelPath:
            return "no model_path";
        case MlxRefusal::ModelMissing:
            return "model missing";
        case MlxRefusal::NotAModelDirectory:
            return "not a model directory";
        case MlxRefusal::NoMlxVlm:
            return "no mlx-vlm";
        case MlxRefusal::NoMlxEmbeddings:
            return "no mlx-embeddings";
        case MlxRefusal::Drafter:
            return "a drafter";
        case MlxRefusal::Kind:
            return "wrong kind";
        case MlxRefusal::Draft:
            return "draft";
        case MlxRefusal::NoDriver:
            return "no driver";
    }
    return "unknown";
}

std::string MlxReadiness::message() const {
    if (ready()) {
        return {};
    }
    return remedy.empty() ? reason : reason + " -- " + remedy;
}

std::string_view to_string(MlxAudioRefusal refusal) noexcept {
    switch (refusal) {
        case MlxAudioRefusal::None:
            return "hears";
        case MlxAudioRefusal::NotMlxVlmBuild:
            return "not mlx-vlm's build";
        case MlxAudioRefusal::NoAudioTower:
            return "no audio tower";
        case MlxAudioRefusal::NoMlxVlm:
            return "no mlx-vlm";
        case MlxAudioRefusal::NoTypeModule:
            return "no mlx-vlm module for its type";
    }
    return "unknown";
}

std::string MlxAudio::message() const {
    if (hears()) {
        return {};
    }
    return std::string{to_string(refusal)} + ": " +
           (remedy.empty() ? reason : reason + " -- " + remedy);
}

MlxReadiness probe_mlx_runtime(const MlxHost& host) {
    MlxReadiness readiness = probe_environment(host);
    if (readiness.ready()) {
        refuse_driver(readiness, host.driver);
    }
    return readiness;
}

MlxReadiness probe_mlx_kind(std::string_view name, const std::filesystem::path& dir) {
    MlxReadiness readiness;
    readiness.model_dir = dir;
    std::error_code code;
    if (!std::filesystem::is_regular_file(dir / "config.json", code)) {
        return readiness;
    }
    const models::MlxKind kind = models::read_mlx_kind(dir);
    // A drafter runs beside its base, never alone (53i): an entry names it as
    // its base's `draft:`, never as its model.
    if (kind.word == models::kMlxDrafter) {
        readiness.refusal = MlxRefusal::Drafter;
        readiness.reason = dir.string() +
                           " is a drafter: it runs beside its base model, never alone -- an mlx "
                           "entry over its base names it as its draft";
        readiness.remedy =
            "apogee config set-backend <its base's mlx backend> --draft " + dir.string();
        return readiness;
    }
    if (!kind.registers()) {
        readiness.refusal = MlxRefusal::Kind;
        readiness.reason =
            dir.string() + " is kind " + models::describe(kind) + ", so no mlx entry runs it";
        readiness.remedy = "apogee config add-backend " + std::string{name} +
                           " --type mlx --model-path <model directory> --force";
    }
    return readiness;
}

MlxReadiness probe_mlx_backend(std::string_view name, const harness::BackendConfig& config,
                               const MlxHost& host) {
    MlxReadiness readiness = probe_environment(host);
    if (!readiness.ready()) {
        return readiness;
    }
    const std::string fix = "apogee config add-backend " + std::string{name} +
                            " --type mlx --model-path <model directory> --force";
    if (config.model_path.empty()) {
        readiness.refusal = MlxRefusal::NoModelPath;
        readiness.reason =
            "model_path is not set -- an mlx entry names a model directory (config.json, the "
            "tokenizer files and SafeTensors weights)";
        readiness.remedy = fix;
        return readiness;
    }
    readiness.model_dir = std::filesystem::path{harness::expand_env_and_home(config.model_path)};
    std::error_code code;
    if (!std::filesystem::exists(readiness.model_dir, code)) {
        readiness.refusal = MlxRefusal::ModelMissing;
        readiness.reason = "model_path does not exist: " + readiness.model_dir.string();
        readiness.remedy = fix;
        return readiness;
    }
    if (!std::filesystem::is_directory(readiness.model_dir, code)) {
        readiness.refusal = MlxRefusal::NotAModelDirectory;
        readiness.reason = readiness.model_dir.string() +
                           " is a file, and an mlx entry names a model directory (a GGUF file runs "
                           "on a llamacpp backend)";
        readiness.remedy = fix;
        return readiness;
    }
    if (!std::filesystem::is_regular_file(readiness.model_dir / "config.json", code)) {
        readiness.refusal = MlxRefusal::NotAModelDirectory;
        readiness.reason = readiness.model_dir.string() +
                           " has no config.json, so it is not a model directory mlx-lm can load";
        readiness.remedy = fix;
        return readiness;
    }
    const models::MlxKind kind = models::read_mlx_kind(readiness.model_dir);
    // The kind decides the role (53i, 55d): a drafter, a video or voice
    // model, or a word this build does not know is no entry's model.
    if (const MlxReadiness by_kind = probe_mlx_kind(name, readiness.model_dir); !by_kind.ready()) {
        readiness.refusal = by_kind.refusal;
        readiness.reason = by_kind.reason;
        readiness.remedy = by_kind.remedy;
        return readiness;
    }
    // An embedding model mlx-embeddings built is embedded with through it
    // alone, by its own driver (53h).
    if (kind.embeds()) {
        if (!find_mlx_embeddings(host).installed()) {
            readiness.refusal = MlxRefusal::NoMlxEmbeddings;
            readiness.reason = readiness.model_dir.string() +
                               " is an embedding model mlx-embeddings built (its record says "
                               "embedding), and mlx-embeddings is not installed in the Python "
                               "environment at " +
                               host.venv.string();
            readiness.remedy = std::string{kMlxEmbeddingsRemedy};
            return readiness;
        }
        refuse_driver(readiness, host.embedder);
        return readiness;
    }
    // A directory mlx-vlm built loads through it alone (53f): without it not
    // even a text turn can be answered.
    if (loads_through_vlm(kind) && !find_mlx_vlm(host).installed()) {
        readiness.refusal = MlxRefusal::NoMlxVlm;
        readiness.reason = readiness.model_dir.string() +
                           " was built by mlx-vlm (its record says vision), which loads it for "
                           "every turn, and mlx-vlm is not installed in the Python environment "
                           "at " +
                           host.venv.string();
        readiness.remedy = std::string{kMlxVisionRemedy};
        return readiness;
    }
    // Its drafter, beside it (53i): the pairing and mlx-vlm's word on it.
    if (!config.draft.empty()) {
        if (const MlxDraft draft = probe_mlx_draft(readiness.model_dir, config.draft, host);
            !draft.pairs()) {
            readiness.refusal = MlxRefusal::Draft;
            readiness.reason = "its draft cannot run beside it: " + draft.reason;
            readiness.remedy = draft.remedy.empty() ? "apogee config set-backend " +
                                                          std::string{name} + " --draft \"\""
                                                    : draft.remedy;
            return readiness;
        }
    }
    refuse_driver(readiness, host.driver);
    return readiness;
}

MlxModelInfo inspect_mlx_model(const std::filesystem::path& dir) {
    MlxModelInfo info;
    const nlohmann::json config = read_json(dir / "config.json");
    std::error_code code;
    if (config.is_object()) {
        info.model_type = config.value("model_type", std::string{});
        if (const auto text = config.find("text_config");
            text != config.end() && text->is_object()) {
            info.text_model_type = text->value("model_type", std::string{});
        }
        // A vision tower in the configuration, and the processor that
        // turns a picture into its input beside it (27c) -- the store's one
        // reading of it, which an unrecorded directory's kind follows (53e).
        info.vision_files = models::mlx_vision_files(dir);
        // Its towers, as the converter's route reads them (53f): an audio
        // one is what a recording is heard by (53g).
        info.towers = models::snapshot_towers(dir);
    }
    // What it is, by the one reader (53e): its record's word over its files.
    // A vision model is one whose kind says so (53f), and one mlx-vlm built
    // runs through it for every turn.
    info.kind = models::read_mlx_kind(dir);
    info.vision = info.kind.word == models::kMlxVision;
    info.through_vlm = loads_through_vlm(info.kind);
    if (std::filesystem::is_regular_file(dir / "chat_template.jinja", code) ||
        std::filesystem::is_regular_file(dir / "chat_template.json", code)) {
        info.chat_template = true;
    } else {
        const nlohmann::json tokenizer = read_json(dir / "tokenizer_config.json");
        if (tokenizer.is_object()) {
            const auto it = tokenizer.find("chat_template");
            info.chat_template = it != tokenizer.end() && !it->is_null() &&
                                 !(it->is_string() && it->get<std::string>().empty());
        }
    }
    // The authors' recommendation, as `generation_config.json` states it.
    // `do_sample: false` is their way of saying greedy.
    const nlohmann::json generation = read_json(dir / "generation_config.json");
    if (generation.is_object()) {
        const auto number = [&generation](const char* key) -> std::optional<double> {
            const auto it = generation.find(key);
            return it != generation.end() && it->is_number() ? std::optional{it->get<double>()}
                                                             : std::nullopt;
        };
        info.sampling.temperature = number("temperature");
        info.sampling.top_p = number("top_p");
        if (const std::optional<double> top_k = number("top_k"); top_k.has_value()) {
            info.sampling.top_k = static_cast<std::int64_t>(*top_k);
        }
        info.sampling.min_p = number("min_p");
        info.sampling.repeat_penalty = number("repetition_penalty");
        if (const auto sample = generation.find("do_sample");
            sample != generation.end() && sample->is_boolean() && !sample->get<bool>()) {
            info.sampling.temperature = 0.0;
        }
    }
    return info;
}

MlxVlmPackage find_mlx_vlm(const MlxHost& host) {
    MlxVlmPackage found;
    found.dir = find_package(host.venv, "mlx_vlm");
    if (found.installed()) {
        found.version = installed_version(found.dir);
    }
    return found;
}

MlxEmbeddingsPackage find_mlx_embeddings(const MlxHost& host) {
    MlxEmbeddingsPackage found;
    found.dir = find_package(host.venv, "mlx_embeddings");
    if (found.installed()) {
        found.version = installed_version(found.dir);
    }
    return found;
}

MlxVision probe_mlx_vision(const MlxModelInfo& info, const MlxHost& host) {
    MlxVision vision;
    vision.model = info.vision;
    if (!vision.model) {
        if (info.kind.source == models::MlxKindSource::Recorded && info.vision_files) {
            // Its record wins over its files (53f): what was built is what
            // runs, whatever configuration came along with it.
            vision.reason =
                info.kind.word == models::kMlxChat
                    ? std::string{"its record says it is mlx-lm's build of its language model "
                                  "alone (kind chat), its vision tower left out -- 'apogee "
                                  "models convert <its SafeTensors set> --mlx' builds it through "
                                  "mlx-vlm, the tower kept"}
                    : "its record says it is kind " + info.kind.word + ", not a vision model";
            return vision;
        }
        vision.reason =
            "it is not a vision model: its config.json declares no vision_config, or no image "
            "processor (preprocessor_config.json or processor_config.json) ships with it";
        return vision;
    }
    vision.vlm = find_mlx_vlm(host);
    if (!vision.vlm.installed()) {
        vision.reason =
            "it is a vision model, but mlx-vlm is not installed in the Python "
            "environment at " +
            host.venv.string();
        vision.remedy = std::string{kMlxVisionRemedy};
    }
    return vision;
}

bool mlx_vlm_has_module(const MlxVlmPackage& vlm, std::string_view model_type) {
    if (!vlm.installed() || model_type.empty()) {
        return false;
    }
    // As mlx-vlm's loader resolves it: the remap table first, then the
    // package `models.<type>` it imports.
    std::string module = remapped_type(vlm.dir / "utils.py", model_type);
    if (module.empty()) {
        module = std::string{model_type};
    }
    std::error_code code;
    return std::filesystem::is_regular_file(vlm.dir / "models" / module / "__init__.py", code);
}

bool mlx_vlm_drafts(const MlxVlmPackage& vlm, std::string_view model_type) {
    // A type is a module's name: nothing that would walk out of the package.
    if (!vlm.installed() || model_type.empty() ||
        model_type.find_first_of("/\\.") != std::string_view::npos) {
        return false;
    }
    // Where `load_drafter` finds a drafter's class, and mlx-vlm's converter
    // falls back to: `speculative.drafters.<type>`, naming its `Model`.
    const std::optional<std::string> init =
        read_file(vlm.dir / "speculative" / "drafters" / std::string{model_type} / "__init__.py");
    return init.has_value() && init->find("Model") != std::string::npos;
}

std::int64_t drafter_block_size(const std::filesystem::path& drafter) {
    const nlohmann::json config = read_json(drafter / "config.json");
    if (!config.is_object()) {
        return 0;
    }
    const auto block = config.find("block_size");
    return block != config.end() && block->is_number_integer() && block->get<std::int64_t>() > 0
               ? block->get<std::int64_t>()
               : 0;
}

std::string MlxDraft::message() const {
    if (pairs()) {
        return {};
    }
    return remedy.empty() ? reason : reason + " -- " + remedy;
}

MlxDraft probe_mlx_draft(const std::filesystem::path& base, std::string_view draft,
                         const MlxHost& host) {
    MlxDraft out;
    out.dir = std::filesystem::path{harness::expand_env_and_home(std::string{draft})};
    // The pairing, from the files: the drafter, the base, the one it names.
    out.reason = models::mlx_drafter_refusal(
        base, out.dir, host.models.empty() ? harness::models_dir() : host.models);
    out.drafter = models::read_mlx_drafter(out.dir);
    if (!out.pairs()) {
        return out;
    }
    // mlx-vlm's speculative path runs it: installed, and drafting its type.
    out.vlm = find_mlx_vlm(host);
    const std::string type = out.drafter.has_value() ? out.drafter->head.model_type : std::string{};
    if (!out.vlm.installed()) {
        out.reason =
            "a drafter runs through mlx-vlm's speculative path, and mlx-vlm is not installed in "
            "the Python environment at " +
            host.venv.string();
        out.remedy = std::string{kMlxVisionRemedy};
        return out;
    }
    if (!mlx_vlm_drafts(out.vlm, type)) {
        const std::string installed =
            "the installed mlx-vlm" + (out.vlm.version.empty() ? "" : " " + out.vlm.version);
        out.reason = type.empty() ? out.dir.string() + "'s config.json names no model_type, so " +
                                        installed + " has no drafter to build it as"
                                  : installed + " drafts no " + type +
                                        " model -- its speculative path holds no drafter of that "
                                        "type (speculative/drafters/" +
                                        type + ")";
        out.remedy = std::string{kMlxVisionRemedy};
    }
    return out;
}

MlxAudio probe_mlx_audio(const MlxModelInfo& info, const MlxHost& host) {
    MlxAudio audio;
    audio.tower = declares_audio_tower(info.towers);
    // Only mlx-vlm's build of a set keeps its towers (53f), and only a record
    // says a directory is that build: one with no record is taken for
    // mlx-lm's layout, as every turn of it is.
    if (!info.through_vlm) {
        audio.refusal = MlxAudioRefusal::NotMlxVlmBuild;
        const std::string convert =
            "'apogee models convert <its SafeTensors set> --mlx' builds it through mlx-vlm, the "
            "towers kept";
        if (info.kind.source != models::MlxKindSource::Recorded) {
            audio.reason =
                "no record says mlx-vlm built it (kind " + info.kind.word +
                (info.kind.source == models::MlxKindSource::Files ? ", inferred from its files"
                                                                  : ", none recorded") +
                "), and only mlx-vlm's build of a set keeps an audio tower -- " + convert;
        } else if (info.kind.word == models::kMlxChat) {
            audio.reason =
                "its record says it is mlx-lm's build of its language model alone (kind chat), "
                "its towers left out -- " +
                convert;
        } else {
            audio.reason = "its record says it is kind " + info.kind.word +
                           ", not a model mlx-vlm built to chat with";
        }
        return audio;
    }
    if (!audio.tower) {
        audio.refusal = MlxAudioRefusal::NoAudioTower;
        audio.reason =
            "its config.json declares none -- no audio_config, nor an omni model's "
            "thinker_config";
        return audio;
    }
    audio.vlm = find_mlx_vlm(host);
    if (!audio.vlm.installed()) {
        audio.refusal = MlxAudioRefusal::NoMlxVlm;
        audio.reason =
            "it hears through mlx-vlm, which is not installed in the Python environment at " +
            host.venv.string();
        audio.remedy = std::string{kMlxVisionRemedy};
        return audio;
    }
    if (!mlx_vlm_has_module(audio.vlm, info.model_type)) {
        audio.refusal = MlxAudioRefusal::NoTypeModule;
        const std::string installed =
            "the installed mlx-vlm" + (audio.vlm.version.empty() ? "" : " " + audio.vlm.version);
        audio.reason = info.model_type.empty()
                           ? "its config.json names no model_type, so " + installed +
                                 " has no module to build it by"
                           : installed + " has no module for its type, " + info.model_type +
                                 ", so it cannot build the model to hear with";
        audio.remedy = std::string{kMlxVisionRemedy};
    }
    return audio;
}

// ---------------------------------------------------------------------------
// The provider
// ---------------------------------------------------------------------------

MlxLocalProvider::MlxLocalProvider(Options options, Spawner spawner)
    : options_{std::move(options)}, spawner_{std::move(spawner)} {
    if (!options_.clock) {
        options_.clock = [] { return std::chrono::steady_clock::now(); };
    }
}

MlxLocalProvider::~MlxLocalProvider() {
    end_session();
}

MlxLocalProvider::Options MlxLocalProvider::options_from(const std::string& backend_name,
                                                         const harness::BackendConfig& config,
                                                         const MlxHost& host) {
    Options options;
    options.backend_name = backend_name;
    options.model = config.model.empty() ? config.model_path : config.model;
    options.model_dir = std::filesystem::path{harness::expand_env_and_home(config.model_path)};
    options.interpreter = host.venv / "bin" / "python";
    options.driver = host.driver;
    options.info = inspect_mlx_model(options.model_dir);
    const MlxVision vision = probe_mlx_vision(options.info, host);
    options.vision = vision.reads_images();
    options.vision_gap =
        vision.remedy.empty() ? vision.reason : vision.reason + " -- " + vision.remedy;
    // Whether it hears, and the rung that says why not (53g).
    const MlxAudio audio = probe_mlx_audio(options.info, host);
    options.audio = audio.hears();
    options.audio_gap = audio.message();
    if (config.max_tokens.has_value()) {
        options.max_tokens = *config.max_tokens;
    }
    if (config.context_size.has_value()) {
        options.context_size = *config.context_size;
    }
    // The configuration alone, no shard opened: this runs for every entry at
    // every startup.
    const models::MlxInfo facts = models::read_mlx_config(options.model_dir);
    options.trained_window = facts.context_length;
    options.config_read = facts.config_read;
    if (config.idle_unload_seconds.has_value() && *config.idle_unload_seconds > 0) {
        options.idle_unload = std::chrono::seconds{*config.idle_unload_seconds};
    }
    options.sampling = config_rung(config);
    options.seed = config_seed(config);
    // The adapter, as the loader read it (53a), and where the model's hash
    // its pin is checked against is remembered.
    options.adapters = config.adapters;
    options.weight_hashing.memo = harness::weight_hash_cache_path();
    // The drafter beside it (53i), the ladder having paired them.
    if (!config.draft.empty()) {
        options.draft = std::filesystem::path{harness::expand_env_and_home(config.draft)};
        options.draft_block_size = config.draft_block_size.value_or(0);
    }
    return options;
}

std::unique_ptr<MlxLocalProvider> MlxLocalProvider::from_config(
    const std::string& backend_name, const harness::BackendConfig& config, const MlxHost& host) {
    const MlxReadiness readiness = probe_mlx_backend(backend_name, config, host);
    if (!readiness.ready()) {
        throw harness::ProviderError(backend_name, readiness.message());
    }
    return std::make_unique<MlxLocalProvider>(
        options_from(backend_name, config, host),
        [](const platform::ChildCommand& command, std::string& error) {
            return platform::start_child(command, error);
        });
}

std::string_view MlxLocalProvider::backend_name() const noexcept {
    return options_.backend_name;
}

int MlxLocalProvider::spawn_count() const noexcept {
    return spawns_;
}

bool MlxLocalProvider::has_live_child() const noexcept {
    return child_ != nullptr;
}

bool MlxLocalProvider::has_vision_child() const noexcept {
    return child_ != nullptr && vision_child_;
}

const ModelProfile* resolve_mlx_profile(const MlxModelInfo& info, std::string_view name_hint) {
    // The architecture first -- a fact the directory states -- in each
    // spelling the profiles list it under; a vision-language model's text
    // model too. Then the names, which the ladder ranks below a fact.
    for (const std::string& type : {info.model_type, info.text_model_type}) {
        for (const std::string& spelling : architecture_spellings(type)) {
            if (const ModelProfile* found = resolve_profile({}, spelling, {}); found != nullptr) {
                return found;
            }
        }
    }
    return resolve_profile({}, {}, name_hint);
}

const ModelProfile* MlxLocalProvider::profile() const {
    if (!profile_resolved_) {
        profile_ =
            resolve_mlx_profile(options_.info, options_.model + " " + options_.model_dir.string());
        profile_resolved_ = true;
    }
    return profile_;
}

harness::ModelBehavior MlxLocalProvider::model_behavior() const {
    harness::ModelBehavior behavior = behavior_for(profile());
    // A directory with no chat template is a base model (26r): a file fact.
    behavior.base_model = !options_.info.chat_template;
    return behavior;
}

ResolvedSampling MlxLocalProvider::sampling_for(const harness::ChatRequest& request) const {
    const ModelProfile* family = profile();
    return resolve_sampling(
        SamplingLadder{.request = SamplingRung{.temperature = request.temperature},
                       .config = options_.sampling,
                       .model_file = options_.info.sampling,
                       .family = family_rung(family, !request.thinking.off()),
                       .family_source = family == nullptr ? std::string{} : family->sampling_source,
                       .seed = options_.seed});
}

std::int64_t MlxLocalProvider::context_window() const {
    models::MlxInfo facts;
    facts.config_read = options_.config_read;
    facts.context_length = options_.trained_window;
    harness::BackendConfig configured;
    if (options_.context_size > 0) {
        configured.context_size = options_.context_size;
    }
    // The one rule `models info` and `check` state the window by.
    return models::mlx_window(facts, configured).window;
}

harness::StatusEvent MlxLocalProvider::model_status() const {
    harness::StatusEvent event;
    event.type = ready_.has_value() && child_ != nullptr ? harness::StatusEvent::Type::ModelReady
                                                         : harness::StatusEvent::Type::ModelLoading;
    event.phase = harness::StatusEvent::Phase::Done;
    event.name = options_.model;
    return event;
}

void MlxLocalProvider::preload(const harness::StatusSink& on_status) {
    const std::scoped_lock lock{mutex_};
    harness::StreamOptions options;
    options.on_status = on_status;
    // A directory mlx-vlm built is loaded through it from the first (53f),
    // and so is one drafted beside (53i).
    ensure_child(options, options_.info.through_vlm || drafting());
}

std::vector<harness::ModelInfo> MlxLocalProvider::list_models(
    const harness::CancellationToken& cancellation) {
    cancellation.throw_if_cancelled();
    harness::ModelInfo info;
    info.id = options_.model;
    info.name = options_.model;
    info.provider = "mlx";
    info.backend = options_.backend_name;
    return {info};
}

void MlxLocalProvider::drain_stderr() {
    if (child_ == nullptr) {
        return;
    }
    std::string chunk;
    while (child_->read_stderr(chunk, std::chrono::milliseconds{0}) == platform::ReadStatus::Data) {
        stderr_tail_ += chunk;
        if (stderr_tail_.size() > kStderrTailBytes) {
            stderr_tail_.erase(0, stderr_tail_.size() - kStderrTailBytes);
        }
    }
}

std::string MlxLocalProvider::with_tail(const std::string& message) const {
    const std::string tail = last_line(stderr_tail_);
    return tail.empty() ? message : message + ": " + tail;
}

void MlxLocalProvider::drop_child() {
    if (child_ != nullptr) {
        child_->terminate();
        (void)child_->wait_for_exit(std::chrono::seconds{2});
    }
    child_.reset();
    framer_.reset();
    lines_.clear();
    ready_.reset();
    vision_child_ = false;
}

void MlxLocalProvider::end_session() {
    if (child_ == nullptr) {
        return;
    }
    // Closing stdin, not killing: the driver's read returns end-of-file and it
    // exits on its own, its model freed the ordinary way.
    child_->close_stdin();
    if (!child_->wait_for_exit(options_.shutdown_timeout).has_value()) {
        child_->terminate();
        (void)child_->wait_for_exit(std::chrono::seconds{2});
    }
    child_.reset();
    framer_.reset();
    lines_.clear();
    ready_.reset();
    vision_child_ = false;
}

void MlxLocalProvider::set_load_listener(const harness::StatusSink& listener) {
    load_listener_ = listener;
}

void MlxLocalProvider::hold_resident(bool held) noexcept {
    held_.store(held);
}

bool MlxLocalProvider::held_resident() const noexcept {
    return held_.load();
}

void MlxLocalProvider::expire_if_idle() {
    // Held by a session using it (27e): the clock waits for the hold to go.
    if (options_.idle_unload.count() <= 0 || !used_ || child_ == nullptr || held_.load()) {
        return;
    }
    if (options_.clock() - last_use_ >= options_.idle_unload) {
        end_session();
    }
}

std::optional<mlx::Event> MlxLocalProvider::next_event(std::chrono::milliseconds stall,
                                                       const harness::CancellationToken* cancel,
                                                       bool& timed_out) {
    timed_out = false;
    const auto deadline = std::chrono::steady_clock::now() + stall;
    std::string chunk;
    for (;;) {
        while (!lines_.empty()) {
            const std::string line = std::move(lines_.front());
            lines_.pop_front();
            if (std::optional<mlx::Event> event = mlx::parse_event(line)) {
                return event;
            }
        }
        if (cancel != nullptr && cancel->stop_requested()) {
            return std::nullopt;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            timed_out = true;
            return std::nullopt;
        }
        drain_stderr();
        const platform::ReadStatus status = child_->read_stdout(chunk, kPoll);
        if (status == platform::ReadStatus::Data) {
            framer_.feed(chunk, [this](std::string_view line) { lines_.emplace_back(line); });
            continue;
        }
        if (status == platform::ReadStatus::Timeout) {
            continue;
        }
        // End of file: whatever was buffered is the child's last word.
        framer_.flush([this](std::string_view line) { lines_.emplace_back(line); });
        drain_stderr();
        if (lines_.empty()) {
            return std::nullopt;
        }
    }
}

void MlxLocalProvider::ensure_child(const harness::StreamOptions& options, bool vision) {
    if (child_ != nullptr && ready_.has_value() && !child_->exited()) {
        if (!vision || vision_child_) {
            return;
        }
        // A text driver, and this turn carries a picture: the model is loaded
        // again through mlx-vlm, which then answers every turn after (27c).
        end_session();
    }
    if (child_ != nullptr) {
        drop_child();  // it died between turns; a fresh one reloads the model
    }
    // The adapter's file facts before a driver is started (53a): not there,
    // not an adapter directory, pinned to no base, or pinned to weights the
    // model directory no longer holds -- each refused by name.
    if (const std::string refused =
            adapter_refusal(options_.adapters, AdapterCheck{.backend = options_.backend_name,
                                                            .base = options_.model_dir,
                                                            .directories = true,
                                                            .hashing = options_.weight_hashing});
        !refused.empty()) {
        throw harness::ProviderError(options_.backend_name, refused);
    }
    stderr_tail_.clear();
    say(options, harness::StatusEvent::Type::ModelLoading, harness::StatusEvent::Phase::Start,
        !vision      ? std::string{}
        : drafting() ? "through mlx-vlm, drafting beside " + options_.draft.filename().string()
        : options_.info.through_vlm ? "through mlx-vlm, which built it"
                                    : "through mlx-vlm, to read images");

    platform::ChildCommand command;
    command.program = options_.interpreter.string();
    command.arguments = {options_.driver.string(), "--model", options_.model_dir.string()};
    if (vision) {
        command.arguments.emplace_back("--vision");
    }
    // The drafter, by mlx-vlm's own `load_drafter`, beside the base it loads
    // (53i); its block size only where the entry sets one.
    if (drafting()) {
        command.arguments.emplace_back("--draft");
        command.arguments.push_back(options_.draft.string());
        if (options_.draft_block_size > 0) {
            command.arguments.emplace_back("--draft-block-size");
            command.arguments.push_back(std::to_string(options_.draft_block_size));
        }
    }
    // mlx-lm's own `adapter_path`, applied at load and never fused (53a).
    for (const harness::AdapterConfig& adapter : options_.adapters) {
        command.arguments.emplace_back("--adapter");
        command.arguments.push_back(adapter.path);
    }
    ++spawns_;
    std::string error;
    child_ = spawner_(command, error);
    if (child_ == nullptr) {
        throw harness::ProviderError(
            options_.backend_name,
            "could not start the MLX driver" + (error.empty() ? std::string{} : ": " + error));
    }
    vision_child_ = vision;

    await_ready(options.cancellation);
    const mlx::Event& ready = ready_.value();
    if (vision && !ready.vision) {
        // A driver from before 27c: it ignores --vision and loads text only,
        // and a picture sent to it would never be seen -- nor a directory
        // mlx-vlm built ever be loaded the way it was built (53f).
        drop_child();
        throw harness::ProviderError(
            options_.backend_name,
            "the MLX driver at " + options_.driver.string() +
                (options_.info.through_vlm ? " does not load models through mlx-vlm"
                                           : " does not read images") +
                " -- it is an older copy: 'apogee check --fix' brings an earlier Apogee's up to "
                "this build's, and an edited one is kept, so delete it first");
    }
    if (!options_.adapters.empty() && ready.adapter.empty()) {
        // A driver from before 53a: it ignores --adapter and loads the bare
        // base, which would answer as if the adapter were applied.
        drop_child();
        throw harness::ProviderError(
            options_.backend_name,
            "the MLX driver at " + options_.driver.string() +
                " does not apply adapters -- it is an older copy: 'apogee check --fix' brings "
                "an earlier Apogee's up to this build's, and an edited one is kept, so delete "
                "it first");
    }
    if (drafting() && ready.draft.empty()) {
        // A driver from before 53i: it ignores --draft and answers from the
        // base alone, which would read as drafting that never happened.
        drop_child();
        throw harness::ProviderError(
            options_.backend_name,
            "the MLX driver at " + options_.driver.string() +
                " does not draft -- it is an older copy: 'apogee check --fix' brings an earlier "
                "Apogee's up to this build's, and an edited one is kept, so delete it first");
    }
    say(options, harness::StatusEvent::Type::ModelReady, harness::StatusEvent::Phase::Done,
        "mlx-lm " + ready.mlx_lm_version + ", " +
            (ready.model_type.empty() ? std::string{"unknown"} : ready.model_type) +
            (ready.vision ? ", mlx-vlm " + ready.mlx_vlm_version : std::string{}) +
            (ready.adapter.empty()
                 ? std::string{}
                 : ", adapter " + std::filesystem::path{ready.adapter}.filename().string()) +
            (ready.draft.empty()
                 ? std::string{}
                 : ", drafter " + std::filesystem::path{ready.draft}.filename().string() + " (" +
                       (ready.draft_kind.empty() ? std::string{"mtp"} : ready.draft_kind) +
                       (ready.draft_block_size > 0
                            ? ", block " + std::to_string(ready.draft_block_size)
                            : std::string{}) +
                       ")"));
}

void MlxLocalProvider::await_ready(const harness::CancellationToken& cancellation) {
    // The load: until `ready`, an error the driver names, or its exit.
    for (;;) {
        bool timed_out = false;
        const std::optional<mlx::Event> event =
            next_event(options_.load_timeout, &cancellation, timed_out);
        if (!event.has_value()) {
            if (cancellation.stop_requested()) {
                drop_child();
                throw harness::CancelledError{};
            }
            const std::string message = with_tail(
                timed_out ? "the MLX driver did not finish loading " + options_.model_dir.string() +
                                " in time"
                          : "the MLX driver exited while loading " + options_.model_dir.string());
            drop_child();
            throw harness::ProviderError(options_.backend_name, message);
        }
        if (event->kind == mlx::Event::Kind::Error) {
            // A missing dependency names its own fix; a load failure names the
            // directory and mlx-lm's reason (an architecture it cannot build).
            const std::string message =
                event->text.empty() ? with_tail("the MLX driver failed") : event->text;
            drop_child();
            throw harness::ProviderError(options_.backend_name, message);
        }
        if (event->kind != mlx::Event::Kind::Ready) {
            continue;
        }
        if (event->protocol != mlx::kProtocolVersion) {
            drop_child();
            throw harness::ProviderError(
                options_.backend_name,
                "the MLX driver speaks protocol " + std::to_string(event->protocol) +
                    ", and this build speaks " + std::to_string(mlx::kProtocolVersion) +
                    " -- delete it and run 'apogee check --fix' to restore the shipped one");
        }
        ready_ = *event;
        return;
    }
}

void MlxLocalProvider::report_cache(const mlx::Event& done,
                                    const harness::StreamOptions& options) const {
    if (!options.on_status) {
        return;
    }
    // What the driver's cache kept of this prompt and what it read again --
    // the line `--verbose` shows, as the llamacpp backend's.
    harness::StatusEvent event;
    event.type = harness::StatusEvent::Type::PromptCache;
    event.phase = harness::StatusEvent::Phase::Done;
    event.name = options_.model;
    event.tokens = done.prompt_tokens;
    event.used_tokens = done.cached_tokens;
    event.detail = "prompt " + std::to_string(done.prompt_tokens) +
                   " tokens: " + std::to_string(done.cached_tokens) + " from the cache, " +
                   std::to_string(done.prompt_tokens - done.cached_tokens) +
                   (vision_child_ ? " read · mlx-vlm, read whole each turn"
                                  : " read · mlx, held in the driver");
    options.on_status(event);
}

void MlxLocalProvider::say(const harness::StreamOptions& options, harness::StatusEvent::Type type,
                           harness::StatusEvent::Phase phase, std::string detail) const {
    if (!options.on_status && !load_listener_) {
        return;
    }
    harness::StatusEvent event;
    event.type = type;
    event.phase = phase;
    event.name = options_.model;
    event.detail = std::move(detail);
    if (options.on_status) {
        options.on_status(event);
    }
    // The session's listener hears every load, whatever request made it (27e).
    if (load_listener_) {
        load_listener_(event);
    }
}

void MlxLocalProvider::cancel_request(std::int64_t id) {
    if (child_ == nullptr) {
        return;
    }
    if (!child_->write_stdin(mlx::cancel_line(id) + "\n")) {
        drop_child();
        return;
    }
    // Its `done` says the generation stopped and the cache is consistent; a
    // driver that will not say so is ended, and the next turn starts a new one.
    const auto deadline = std::chrono::steady_clock::now() + options_.cancel_timeout;
    for (;;) {
        const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(
            deadline - std::chrono::steady_clock::now());
        if (left.count() <= 0) {
            drop_child();
            return;
        }
        bool timed_out = false;
        const std::optional<mlx::Event> event = next_event(left, nullptr, timed_out);
        if (!event.has_value()) {
            drop_child();
            return;
        }
        if ((event->kind == mlx::Event::Kind::Done || event->kind == mlx::Event::Kind::Error) &&
            event->id == id) {
            return;
        }
    }
}

harness::ChatResponse MlxLocalProvider::chat(const harness::ChatRequest& request,
                                             const harness::CancellationToken& cancellation) {
    harness::StreamOptions options;
    options.cancellation = cancellation;
    return stream_chat(request, options);
}

harness::ChatResponse MlxLocalProvider::stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) {
    const std::scoped_lock lock{mutex_};
    return run(request, options);
}

mlx::GenerateRequest MlxLocalProvider::make_request(const harness::ChatRequest& request,
                                                    const harness::StreamOptions& options) {
    const bool side = request.transient.side_request;
    mlx::GenerateRequest generate;
    generate.id = ++next_id_;
    // No grammar here: a schema is stated in the prompt (26f), the answer
    // validated by the caller as it always is.
    generate.messages = prompt_messages(request, true);
    generate.tools = request.tools;
    generate.sampling = sampling_for(request);
    generate.max_tokens = request.max_tokens.value_or(options_.max_tokens);
    generate.thinking = !request.thinking.off();
    generate.session = !side;

    if (!request.transient.response_schema.empty() && !schema_noticed_) {
        schema_noticed_ = true;
        const std::string text = "the answer on " + options_.model +
                                 " is held to its schema by the prompt alone: the mlx backend has "
                                 "no grammar. It is still validated";
        logger::log(logger::Level::Info, "mlx", text);
        notice(options, text);
    }
    if (request.thinking.tokens().has_value() && !request.thinking.off() && !side &&
        !budget_noticed_) {
        budget_noticed_ = true;
        notice(options, "the thinking budget is not applied on " + options_.model +
                            ": the mlx backend has no budget sampler");
    }
    return generate;
}

void MlxLocalProvider::fail_without_event(std::int64_t id, bool timed_out,
                                          const harness::CancellationToken& cancellation) {
    if (cancellation.stop_requested()) {
        cancel_request(id);
        throw harness::CancelledError{};
    }
    if (timed_out) {
        cancel_request(id);
        throw harness::ProviderError(options_.backend_name, "the MLX driver went silent mid-turn");
    }
    const std::string message = with_tail("the MLX driver exited mid-turn");
    drop_child();
    throw harness::ProviderError(options_.backend_name, message);
}

void MlxLocalProvider::fail_with(const mlx::Event& error) {
    if (error.error_kind == "request") {
        throw harness::ProviderError(options_.backend_name,
                                     "the MLX driver could not answer: " + error.text);
    }
    const std::string message =
        error.text.empty() ? with_tail("the MLX driver failed") : error.text;
    drop_child();
    throw harness::ProviderError(options_.backend_name, message);
}

namespace {

/// One reply, read: the driver's text through the family's filters -- what
/// it streams as text is read exactly as llama.cpp's own text is, gpt-oss's
/// channels to the thinking view and its native calls to the gate -- its
/// reasoning to the thinking sink, its calls collected with ids.
class DriverReply {
public:
    DriverReply(const ModelProfile* family, bool native_calls,
                const harness::StreamOptions& options)
        : think_{reasoning_pairs_for(family)},
          gate_{native_calls},
          markup_{header_markers_for(family)},
          options_{options} {
        if (options_.on_thinking) {
            think_.on_thinking(options_.on_thinking);
        }
    }

    void take(const mlx::Event& event) {
        if (event.kind == mlx::Event::Kind::Text) {
            show(markup_.write(gate_.write(think_.write(event.text))));
        } else if (event.kind == mlx::Event::Kind::Reasoning) {
            if (options_.on_thinking && !event.text.empty()) {
                options_.on_thinking(event.text);
            }
        } else if (event.kind == mlx::Event::Kind::ToolCall) {
            calls_.push_back(
                {.id = make_call_id(), .name = event.name, .arguments = event.arguments});
        }
    }

    /// Flushed in the order written through, the gate's safety net last: a
    /// span that opened like a call and parsed as nothing comes back as text.
    void finish() {
        std::string tail = markup_.write(gate_.write(think_.flush()));
        tail += markup_.write(gate_.flush());
        tail += markup_.flush();
        show(tail);
        for (harness::ToolCall call : gate_.calls()) {
            if (call.id.empty()) {
                call.id = make_call_id();
            }
            calls_.push_back(std::move(call));
        }
    }

    [[nodiscard]] std::string& answer() noexcept {
        return answer_;
    }

    [[nodiscard]] std::vector<harness::ToolCall>& calls() noexcept {
        return calls_;
    }

private:
    void show(const std::string& visible) {
        answer_ += visible;
        if (options_.on_token && !visible.empty()) {
            options_.on_token(visible);
        }
    }

    ThinkFilter think_;
    ToolCallGate gate_;
    MarkupFilter markup_;
    const harness::StreamOptions& options_;
    std::string answer_;
    std::vector<harness::ToolCall> calls_;
};

}  // namespace

void MlxLocalProvider::check_media(const harness::ChatRequest& request) const {
    for (const harness::ChatMessage& message : request.messages) {
        for (const harness::ContentPart& part : message.content.parts()) {
            if (part.kind == harness::ContentPart::Kind::InputAudio) {
                if (!options_.audio) {
                    // Never silently dropped, nor transcribed here behind the
                    // caller's back: every surface asks first, and has the
                    // transcription role read it instead (26e).
                    throw harness::ProviderError(
                        options_.backend_name,
                        options_.model + " cannot hear audio on the mlx backend" +
                            (options_.audio_gap.empty() ? std::string{}
                                                        : " -- " + options_.audio_gap));
                }
                if (!says_wav(part.audio_format)) {
                    throw harness::ProviderError(
                        options_.backend_name,
                        "a recording reaches the mlx backend as a WAV file, and this one is " +
                            part.audio_format +
                            " -- Apogee's attachments are decoded to WAV through ffmpeg first");
                }
                continue;
            }
            if (part.kind != harness::ContentPart::Kind::ImageUrl) {
                continue;
            }
            if (!options_.vision) {
                // Never silently dropped: every surface asks first, so this
                // is a caller that did not.
                throw harness::ProviderError(
                    options_.backend_name,
                    options_.model + " cannot read images: " +
                        (options_.vision_gap.empty() ? std::string{"it is not a vision model"}
                                                     : options_.vision_gap));
            }
            if (!part.image_url.starts_with("data:")) {
                throw harness::ProviderError(
                    options_.backend_name,
                    "the mlx backend reads an image given as a data: URI -- a remote image is "
                    "never fetched");
            }
        }
    }
}

harness::ChatResponse MlxLocalProvider::run(const harness::ChatRequest& request,
                                            const harness::StreamOptions& options) {
    if (request.messages.empty()) {
        throw harness::ProviderError(options_.backend_name, "a request needs at least one message");
    }
    options.cancellation.throw_if_cancelled();
    check_media(request);
    // The turn's recordings, written where the driver reads them (53g) --
    // before any driver starts, so one that is not a WAV starts nothing --
    // and removed when the turn is over, however it ends.
    TurnAudio audio;
    audio.write(request, options_.backend_name);
    const bool media = std::ranges::any_of(
        request.messages, [](const harness::ChatMessage& m) { return m.content.is_rich(); });
    expire_if_idle();
    // A picture or a recording asks for mlx-vlm (27c, 53g); so does every
    // turn, text included, of a directory mlx-vlm built (53f), and of one
    // drafted beside, which mlx-vlm's speculative path loads (53i).
    ensure_child(options, media || options_.info.through_vlm || drafting());
    used_ = true;
    last_use_ = options_.clock();
    if (!audio.files().empty() && !ready_->audio) {
        // A driver from before 53g: it would refuse an audio part as a
        // content part it cannot read. It answers text, so it is kept.
        throw harness::ProviderError(
            options_.backend_name,
            "the MLX driver at " + options_.driver.string() +
                " does not hand recordings to the model -- it is an older copy: 'apogee check "
                "--fix' brings an earlier Apogee's up to this build's, and an edited one is "
                "kept, so delete it first");
    }

    mlx::GenerateRequest generate = make_request(request, options);
    generate.audio_files = audio.files();
    if (!child_->write_stdin(mlx::generate_line(generate) + "\n")) {
        const std::string message = with_tail("the MLX driver closed its input");
        drop_child();
        throw harness::ProviderError(options_.backend_name, message);
    }

    DriverReply reader{profile(), model_behavior().native_tool_calls, options};
    mlx::Event done;
    for (;;) {
        bool timed_out = false;
        const std::optional<mlx::Event> event =
            next_event(options_.stall_timeout, &options.cancellation, timed_out);
        if (!event.has_value()) {
            fail_without_event(generate.id, timed_out, options.cancellation);
        }
        if (event->id.has_value() && *event->id != generate.id) {
            continue;  // a cancelled request's late word
        }
        if (event->kind == mlx::Event::Kind::Error) {
            fail_with(*event);
        }
        if (event->kind == mlx::Event::Kind::Done) {
            done = *event;
            break;
        }
        try {
            reader.take(*event);
        } catch (...) {
            // A sink threw -- a surface that stopped listening. The driver is
            // still generating: stop it, and read its `done`, so the next
            // turn starts on a quiet channel.
            cancel_request(generate.id);
            throw;
        }
    }
    reader.finish();

    if (generate.session) {
        report_cache(done, options);
    }

    harness::ChatResponse response;
    response.message = harness::ChatMessage::assistant(std::move(reader.answer()));
    response.message.tool_calls = std::move(reader.calls());
    response.model = options_.model;
    response.finish_reason = response.message.tool_calls.empty() ? mlx::finish_reason(done.finish)
                                                                 : harness::FinishReason::ToolCalls;
    response.usage.prompt_tokens = done.prompt_tokens;
    response.usage.completion_tokens = done.completion_tokens;
    return response;
}

}  // namespace apogee::backends
