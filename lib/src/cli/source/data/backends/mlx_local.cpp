#include "backends/mlx_local.h"

#include <nlohmann/json.hpp>

#include <array>
#include <fstream>
#include <random>
#include <sstream>
#include <system_error>
#include <utility>

#include "backends/local_prompt.h"
#include "backends/markup_filter.h"
#include "backends/native_tool_calls.h"
#include "backends/think_filter.h"
#include "contracts/assets.h"
#include "contracts/errors.h"
#include "contracts/paths.h"
#include "logger/operational.h"
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

/// `<venv>/lib/python3.*/site-packages/mlx_lm`, when its `__init__.py` is
/// there -- a fact about files, asked without starting an interpreter, so a
/// probe at every startup costs a directory listing.
[[nodiscard]] std::filesystem::path find_mlx_lm(const std::filesystem::path& venv) {
    std::error_code code;
    const std::filesystem::path lib = venv / "lib";
    if (!std::filesystem::is_directory(lib, code)) {
        return {};
    }
    for (const auto& entry : std::filesystem::directory_iterator(lib, code)) {
        if (!entry.path().filename().string().starts_with("python")) {
            continue;
        }
        const std::filesystem::path package = entry.path() / "site-packages" / "mlx_lm";
        if (std::filesystem::is_regular_file(package / "__init__.py", code)) {
            return package;
        }
    }
    return {};
}

/// The version `mlx_lm/_version.py` declares (`__version__ = '0.32.0'`), or
/// empty when it says none.
[[nodiscard]] std::string mlx_lm_version(const std::filesystem::path& package) {
    const std::optional<std::string> text = read_file(package / "_version.py");
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
    readiness.package = find_mlx_lm(host.venv);
    if (readiness.package.empty()) {
        readiness.refusal = MlxRefusal::NoMlxLm;
        readiness.reason =
            "mlx-lm is not installed in the Python environment at " + host.venv.string();
        readiness.remedy = std::string{kSetupRemedy};
        return readiness;
    }
    readiness.version = mlx_lm_version(readiness.package);
    return readiness;
}

void refuse_driver(MlxReadiness& readiness, const MlxHost& host) {
    std::error_code code;
    if (!std::filesystem::is_regular_file(host.driver, code)) {
        readiness.refusal = MlxRefusal::NoDriver;
        readiness.reason = "the MLX driver is missing: " + host.driver.string();
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

}  // namespace

// ---------------------------------------------------------------------------
// The refusal ladder
// ---------------------------------------------------------------------------

MlxHost MlxHost::at(const std::filesystem::path& home) {
    MlxHost host;
    host.target = platform::host_target();
    host.venv = home / "training" / "venv";
    host.driver = home / harness::bundled_mlx_driver_relative_path();
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

MlxReadiness probe_mlx_runtime(const MlxHost& host) {
    MlxReadiness readiness = probe_environment(host);
    if (readiness.ready()) {
        refuse_driver(readiness, host);
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
    refuse_driver(readiness, host);
    return readiness;
}

MlxModelInfo inspect_mlx_model(const std::filesystem::path& dir) {
    MlxModelInfo info;
    const nlohmann::json config = read_json(dir / "config.json");
    if (config.is_object()) {
        info.model_type = config.value("model_type", std::string{});
        if (const auto text = config.find("text_config");
            text != config.end() && text->is_object()) {
            info.text_model_type = text->value("model_type", std::string{});
        }
    }
    std::error_code code;
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
    if (config.max_tokens.has_value()) {
        options.max_tokens = *config.max_tokens;
    }
    if (config.context_size.has_value()) {
        options.context_size = *config.context_size;
    }
    if (config.idle_unload_seconds.has_value() && *config.idle_unload_seconds > 0) {
        options.idle_unload = std::chrono::seconds{*config.idle_unload_seconds};
    }
    options.sampling = config_rung(config);
    options.seed = config_seed(config);
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
    return options_.context_size;
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
    ensure_child(options);
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
}

void MlxLocalProvider::expire_if_idle() {
    if (options_.idle_unload.count() <= 0 || !used_ || child_ == nullptr) {
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

void MlxLocalProvider::ensure_child(const harness::StreamOptions& options) {
    if (child_ != nullptr && ready_.has_value() && !child_->exited()) {
        return;
    }
    if (child_ != nullptr) {
        drop_child();  // it died between turns; a fresh one reloads the model
    }
    stderr_tail_.clear();
    say(options, harness::StatusEvent::Type::ModelLoading, harness::StatusEvent::Phase::Start, {});

    platform::ChildCommand command;
    command.program = options_.interpreter.string();
    command.arguments = {options_.driver.string(), "--model", options_.model_dir.string()};
    ++spawns_;
    std::string error;
    child_ = spawner_(command, error);
    if (child_ == nullptr) {
        throw harness::ProviderError(
            options_.backend_name,
            "could not start the MLX driver" + (error.empty() ? std::string{} : ": " + error));
    }

    await_ready(options.cancellation);
    const mlx::Event& ready = ready_.value();
    say(options, harness::StatusEvent::Type::ModelReady, harness::StatusEvent::Phase::Done,
        "mlx-lm " + ready.mlx_lm_version + ", " +
            (ready.model_type.empty() ? std::string{"unknown"} : ready.model_type));
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
                   " read · mlx, held in the driver";
    options.on_status(event);
}

void MlxLocalProvider::say(const harness::StreamOptions& options, harness::StatusEvent::Type type,
                           harness::StatusEvent::Phase phase, std::string detail) const {
    if (!options.on_status) {
        return;
    }
    harness::StatusEvent event;
    event.type = type;
    event.phase = phase;
    event.name = options_.model;
    event.detail = std::move(detail);
    options.on_status(event);
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
    if (request.thinking.budget.has_value() && !request.thinking.off() && !side &&
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

harness::ChatResponse MlxLocalProvider::run(const harness::ChatRequest& request,
                                            const harness::StreamOptions& options) {
    if (request.messages.empty()) {
        throw harness::ProviderError(options_.backend_name, "a request needs at least one message");
    }
    options.cancellation.throw_if_cancelled();
    expire_if_idle();
    ensure_child(options);
    used_ = true;
    last_use_ = options_.clock();

    const mlx::GenerateRequest generate = make_request(request, options);
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
