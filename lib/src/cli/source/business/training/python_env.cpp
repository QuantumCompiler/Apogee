#include "training/python_env.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <system_error>

#include "contracts/config.h"
#include "platform/child_process.h"

namespace apogee::training {
namespace {

constexpr std::array<std::string_view, 7> kSetNames{"prepare", "mlx",   "peft", "convert",
                                                    "mlx-vlm", "video", "voice"};

/// Where each set runs, in the names' order (51): what `apogee bootstrap`
/// installs on a host is the table's own word.
enum class SetHost : std::uint8_t { Any, AppleSilicon, NvidiaGpu };
constexpr std::array<SetHost, kSetNames.size()> kSetHosts{
    SetHost::Any,          SetHost::AppleSilicon, SetHost::NvidiaGpu, SetHost::Any,
    SetHost::AppleSilicon, SetHost::Any,          SetHost::Any};

/// The Pythons each set runs on, in the names' order (M19): the tightest of
/// its packages' `requires_python` as PyPI publishes them for the current
/// releases, read on the date below. Only voice is bounded above -- `kokoro`
/// 0.9.4 and `misaki` 0.9.4 say `<3.13` -- and mlx's floor is `mlx-lm`
/// 0.32.2's `>=3.11`; torch, transformers, diffusers, datasets, mlx-vlm,
/// peft, bitsandbytes, accelerate and gguf all say `>=3.10`. A stale range
/// fails as before, in pip's words; the date says when to read them again.
constexpr std::string_view kRangesReviewed = "2026-10-21";
constexpr std::array<PythonRange, kSetNames.size()> kSetRanges{
    PythonRange{.at_least = 10},
    PythonRange{.at_least = 11},
    PythonRange{.at_least = 10},
    PythonRange{.at_least = 10},
    PythonRange{.at_least = 10},
    PythonRange{.at_least = 10},
    PythonRange{.at_least = 10, .below = 13}};

constexpr std::array<std::string_view, 1> kPrepare{"datasets>=3.0"};
constexpr std::array<std::string_view, 1> kMlx{"mlx-lm>=0.21"};
// The mlx backend's vision path (27c): `load`, `stream_generate` and the
// step's sampler, which the driver asks for by name. And since 53f the
// converter of a set that sees or hears: 0.6.1 (2026-06-03) is the first
// release whose `models/` holds `qwen3_omni_moe`, `gemma4_unified` and
// `qwen3_5` -- 0.6.0 lacks `gemma4_unified` -- so every towered set the
// store holds converts on it, and its `convert` takes `q_mode`, `dtype` and
// `trust_remote_code`. Read on the date below from PyPI's release history and
// the project's tags (`mlx_vlm/models/` at v0.6.0 and v0.6.1). An
// environment already holding an older one keeps it until this set is
// installed again -- `train setup --with mlx-vlm`, the person's own or the
// one their install's bootstrap runs, and nothing else (M19's rule); what is
// offered meanwhile reads the installed one's `models/`, never this floor.
constexpr std::string_view kMlxVlmFloorReviewed = "2026-10-21";
constexpr std::array<std::string_view, 1> kMlxVlm{"mlx-vlm>=0.6.1"};
// `train_peft.py` drives transformers' own Trainer, so trl is not here: trl's
// response-template collator has been removed, and the exact prompt mask
// needs no library.
constexpr std::array<std::string_view, 5> kPeft{"torch>=2.6", "transformers>=4.46", "peft>=0.13",
                                                "bitsandbytes>=0.44", "accelerate>=1.0"};
// The floors the vendored converter's own requirements file states at the
// pinned llama.cpp revision (third_party/llama.cpp-convert/README.md). PyPI's
// `gguf` is here for the dependencies it brings: the package the converter
// imports is the pinned copy seeded beside it, which PyPI's lags.
constexpr std::array<std::string_view, 6> kConvert{"torch>=2.11",        "transformers>=4.57",
                                                   "gguf>=0.19",         "numpy>=2.2",
                                                   "sentencepiece>=0.2", "protobuf>=4.21"};

// The video driver's stack (49b): a pipeline loads through diffusers, its
// text encoder and tokenizer through transformers (sentencepiece for the
// tokenizers that need it), accelerate for the loading, and ftfy for the
// prompt cleaning Wan's pipelines do. Floors, as every set's are; the
// versions that ran go into each clip's document.
// peft since 49e: what diffusers loads a LoRA through.
constexpr std::array<std::string_view, 7> kVideo{
    "torch>=2.6", "diffusers>=0.35",    "transformers>=4.46", "accelerate>=1.0",
    "ftfy>=6.1",  "sentencepiece>=0.2", "peft>=0.13"};

// The voice driver's stack (50a): Kokoro, and misaki -- its grapheme to
// phoneme layer -- with the English extra its English voices read through.
constexpr std::array<std::string_view, 3> kVoice{"torch>=2.6", "kokoro>=0.9.4",
                                                 "misaki[en]>=0.9.4"};

std::string now_rfc3339() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm parts{};
#if defined(_WIN32)
    gmtime_s(&parts, &now);
#else
    gmtime_r(&now, &parts);
#endif
    std::array<char, 32> buffer{};
    const std::size_t written =
        std::strftime(buffer.data(), buffer.size(), "%Y-%m-%dT%H:%M:%SZ", &parts);
    return std::string{buffer.data(), written};
}

std::string describe_failure(std::string_view what, const CommandResult& result) {
    std::string message{what};
    if (!result.start_error.empty()) {
        return message + ": " + result.start_error;
    }
    if (result.cancelled) {
        return message + ": cancelled";
    }
    message +=
        " (exit code " +
        (result.exit_code.has_value() ? std::to_string(*result.exit_code) : std::string{"?"}) + ")";
    const std::string tail = tail_of(result.err.empty() ? result.out : result.err, 6);
    if (!tail.empty()) {
        message += ":\n" + tail;
    }
    return message;
}

/// `apogee.json`, from what it holds.
[[nodiscard]] std::string write_record(const std::filesystem::path& path,
                                       const std::string& base_python,
                                       const PythonVersion& base_version,
                                       const std::string& created_at,
                                       const std::vector<std::string>& sets) {
    nlohmann::json record{{"base_python", base_python},
                          {"created_at", created_at},
                          {"sets", nlohmann::json::array()}};
    if (base_version.known()) {
        record["base_version"] = base_version.str();
    }
    for (const std::string& set : sets) {
        record["sets"].push_back(set);
    }
    std::ofstream out{path, std::ios::binary};
    if (!out) {
        return "could not write " + path.string();
    }
    out << record.dump(2) << "\n";
    return {};
}

/// The version `python -m venv` wrote into `pyvenv.cfg` (`version = 3.14.5`,
/// or `version_info = 3.12.13.final.0`); the zero value when not there.
[[nodiscard]] PythonVersion pyvenv_version(const std::filesystem::path& venv) {
    std::ifstream in{venv / "pyvenv.cfg", std::ios::binary};
    std::string line;
    while (std::getline(in, line)) {
        const std::size_t equals = line.find('=');
        if (equals == std::string::npos) {
            continue;
        }
        std::string key = line.substr(0, equals);
        std::erase(key, ' ');
        if (key != "version" && key != "version_info") {
            continue;
        }
        std::string value = line.substr(equals + 1);
        std::erase(value, ' ');
        std::erase(value, '\r');
        if (const std::optional<PythonVersion> version = PythonVersion::parse(value)) {
            return *version;
        }
    }
    return {};
}

/// "the 'voice' set", "the 'prepare' and 'voice' sets".
[[nodiscard]] std::string sets_named(std::span<const RequirementSet> sets) {
    std::string names;
    for (std::size_t i = 0; i < sets.size(); ++i) {
        names += (i == 0 ? "" : (i + 1 == sets.size() ? " and " : ", ")) + std::string{"'"} +
                 std::string{to_string(sets[i])} + "'";
    }
    return "the " + names + (sets.size() == 1 ? " set" : " sets");
}

}  // namespace

std::string PythonVersion::str() const {
    if (!known()) {
        return {};
    }
    return std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
}

std::optional<PythonVersion> PythonVersion::parse(std::string_view text) {
    std::array<int, 3> parts{};
    std::size_t count = 0;
    std::size_t at = 0;
    while (count < parts.size() && at < text.size()) {
        std::size_t end = at;
        while (end < text.size() && text[end] >= '0' && text[end] <= '9') {
            ++end;
        }
        if (end == at || end - at > 4) {
            break;
        }
        parts.at(count++) = std::stoi(std::string{text.substr(at, end - at)});
        if (end == text.size() || text[end] != '.') {
            break;
        }
        at = end + 1;
    }
    if (count < 2 || parts[0] == 0) {
        return std::nullopt;
    }
    return PythonVersion{.major = parts[0], .minor = parts[1], .patch = parts[2]};
}

bool PythonRange::contains(const PythonVersion& version) const noexcept {
    return version.major == 3 && version.minor >= at_least && (below == 0 || version.minor < below);
}

std::string PythonRange::describe() const {
    std::string out = ">=3." + std::to_string(at_least);
    if (below > 0) {
        out += ",<3." + std::to_string(below);
    }
    return out;
}

PythonRange python_range(RequirementSet set) noexcept {
    return kSetRanges.at(static_cast<std::size_t>(set));
}

PythonRange python_range(std::span<const RequirementSet> sets) noexcept {
    PythonRange range{.at_least = 0, .below = 0};
    for (const RequirementSet set : sets) {
        const PythonRange one = python_range(set);
        range.at_least = std::max(range.at_least, one.at_least);
        if (one.below > 0) {
            range.below = range.below == 0 ? one.below : std::min(range.below, one.below);
        }
    }
    return range;
}

std::string_view python_ranges_reviewed() noexcept {
    return kRangesReviewed;
}

std::string_view mlx_vlm_floor_reviewed() noexcept {
    return kMlxVlmFloorReviewed;
}

std::string_view to_string(RequirementSet set) noexcept {
    return kSetNames.at(static_cast<std::size_t>(set));
}

std::string set_unfit_reason(RequirementSet set, bool apple_silicon, bool nvidia_smi) {
    switch (kSetHosts.at(static_cast<std::size_t>(set))) {
        case SetHost::Any:
            return {};
        case SetHost::AppleSilicon:
            return apple_silicon ? std::string{} : "for Apple silicon, which this host is not";
        case SetHost::NvidiaGpu:
            return nvidia_smi ? std::string{}
                              : "for an NVIDIA GPU, and nvidia-smi is not on PATH here";
    }
    return {};
}

std::optional<RequirementSet> requirement_set_from_string(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kSetNames.size(); ++i) {
        if (kSetNames.at(i) == name) {
            return static_cast<RequirementSet>(i);
        }
    }
    return std::nullopt;
}

std::span<const std::string_view> requirement_set_names() noexcept {
    return kSetNames;
}

std::span<const std::string_view> packages_for(RequirementSet set) noexcept {
    switch (set) {
        case RequirementSet::Prepare:
            return kPrepare;
        case RequirementSet::Mlx:
            return kMlx;
        case RequirementSet::Peft:
            return kPeft;
        case RequirementSet::Convert:
            return kConvert;
        case RequirementSet::MlxVlm:
            return kMlxVlm;
        case RequirementSet::Video:
            return kVideo;
        case RequirementSet::Voice:
            return kVoice;
    }
    return {};
}

bool PythonEnvStatus::has(RequirementSet set) const noexcept {
    const std::string_view name = to_string(set);
    for (const std::string& installed : sets) {
        if (installed == name) {
            return true;
        }
    }
    return false;
}

PythonEnv::PythonEnv(std::filesystem::path venv_dir, CommandRunner runner)
    : dir_{std::move(venv_dir)}, runner_{std::move(runner)} {}

std::filesystem::path PythonEnv::interpreter() const {
#if defined(_WIN32)
    return dir_ / "Scripts" / "python.exe";
#else
    return dir_ / "bin" / "python";
#endif
}

std::filesystem::path PythonEnv::record_path() const {
    return dir_ / "apogee.json";
}

bool PythonEnv::exists() const {
    std::error_code code;
    return std::filesystem::exists(interpreter(), code);
}

PythonEnvStatus PythonEnv::status() const {
    PythonEnvStatus status;
    status.interpreter = interpreter();
    status.exists = exists();
    if (!status.exists) {
        return status;
    }
    std::ifstream in{record_path(), std::ios::binary};
    if (!in) {
        status.error = "no record at " + record_path().string();
        return status;
    }
    const nlohmann::json record = nlohmann::json::parse(in, nullptr, false);
    if (record.is_discarded() || !record.is_object()) {
        status.error = record_path().string() + " is not a JSON object";
        return status;
    }
    status.base_python = record.value("base_python", std::string{});
    status.created_at = record.value("created_at", std::string{});
    // The record's own word since M19; an environment made before it is read
    // from what `python -m venv` wrote.
    const std::optional<PythonVersion> recorded =
        PythonVersion::parse(record.value("base_version", std::string{}));
    status.base_version = recorded.value_or(pyvenv_version(dir_));
    if (const auto it = record.find("sets"); it != record.end() && it->is_array()) {
        for (const nlohmann::json& set : *it) {
            if (set.is_string()) {
                status.sets.push_back(set.get<std::string>());
            }
        }
    }
    return status;
}

std::string PythonEnv::create(const std::filesystem::path& base_python,
                              const harness::CancellationToken& cancellation,
                              const PythonVersion& version) {
    if (exists()) {
        return {};
    }
    std::error_code code;
    std::filesystem::create_directories(dir_.parent_path(), code);
    if (code) {
        return "could not create " + dir_.parent_path().string() + ": " + code.message();
    }
    platform::ChildCommand command;
    command.program = base_python.string();
    command.arguments = {"-m", "venv", dir_.string()};
    const CommandResult result = runner_(command, cancellation);
    if (!result.ok()) {
        return describe_failure(
            "could not create the Python environment with " + base_python.string() + " -m venv",
            result);
    }
    return write_record(record_path(), base_python.string(), version, now_rfc3339(), {});
}

std::string PythonEnv::install(RequirementSet set, const harness::CancellationToken& cancellation) {
    if (!exists()) {
        return "no Python environment at " + dir_.string() + " -- run 'apogee train setup' first";
    }
    // Before pip (M19): a set its Python cannot run fails here with the way
    // out, not in pip's words after the download.
    if (const PythonVersion on = status().base_version;
        on.known() && !python_range(set).contains(on)) {
        return "the Python environment at " + dir_.string() + " is on Python " + on.str() +
               ", and the '" + std::string{to_string(set)} + "' set runs on " +
               python_range(set).describe() +
               " -- rebuild it on a Python that fits: apogee train "
               "setup --rebuild --with " +
               std::string{to_string(set)};
    }
    platform::ChildCommand command;
    command.program = interpreter().string();
    command.arguments = {"-m", "pip", "install", "--disable-pip-version-check"};
    for (const std::string_view package : packages_for(set)) {
        command.arguments.emplace_back(package);
    }
    const CommandResult result = runner_(command, cancellation);
    if (!result.ok()) {
        return describe_failure(
            "pip install for the '" + std::string{to_string(set)} + "' set failed", result);
    }
    PythonEnvStatus current = status();
    if (!current.has(set)) {
        current.sets.emplace_back(to_string(set));
    }
    return write_record(record_path(), current.base_python, current.base_version,
                        current.created_at, current.sets);
}

std::string PythonEnv::rebuild(const std::filesystem::path& base_python,
                               const PythonVersion& version,
                               const harness::CancellationToken& cancellation) {
    const std::vector<std::string> sets = status().sets;
    std::error_code code;
    const std::filesystem::path aside = dir_.string() + ".previous";
    std::filesystem::remove_all(aside, code);
    if (exists()) {
        std::filesystem::rename(dir_, aside, code);
        if (code) {
            return "could not move " + dir_.string() + " aside: " + code.message();
        }
    }
    const auto restore = [&](std::string failure) {
        std::error_code ignored;
        std::filesystem::remove_all(dir_, ignored);
        if (std::filesystem::exists(aside, ignored)) {
            std::filesystem::rename(aside, dir_, ignored);
            failure += " -- the environment that was there is put back as it was";
        }
        return failure;
    };
    if (std::string failure = create(base_python, cancellation, version); !failure.empty()) {
        return restore(std::move(failure));
    }
    for (const std::string& name : sets) {
        const std::optional<RequirementSet> set = requirement_set_from_string(name);
        if (!set.has_value()) {
            continue;
        }
        if (std::string failure = install(*set, cancellation); !failure.empty()) {
            return restore(std::move(failure));
        }
    }
    std::filesystem::remove_all(aside, code);
    return {};
}

std::filesystem::path locate_base_python(std::string_view configured, std::string& error,
                                         std::string_view source) {
    error.clear();
    if (!configured.empty()) {
        const std::string expanded = harness::expand_env_and_home(configured);
        const std::filesystem::path path{expanded};
        std::error_code code;
        if (path.has_parent_path()) {
            if (std::filesystem::exists(path, code)) {
                return path;
            }
            error = std::string{source} + " names '" + expanded + "', which does not exist";
            return {};
        }
        const std::string found = platform::find_on_path(expanded);
        if (!found.empty()) {
            return std::filesystem::path{found};
        }
        error = std::string{source} + " names '" + expanded + "', which is not on PATH";
        return {};
    }
    for (const char* candidate : {"python3", "python"}) {
        const std::string found = platform::find_on_path(candidate);
        if (!found.empty()) {
            return std::filesystem::path{found};
        }
    }
    error = "no python3 on PATH -- install Python 3, or set training.python in the config";
    return {};
}

PythonVersion probe_python_version(const std::filesystem::path& interpreter,
                                   const CommandRunner& runner) {
    platform::ChildCommand command;
    command.program = interpreter.string();
    command.arguments = {"-c", "import sys; print('%d.%d.%d' % sys.version_info[:3])"};
    const CommandResult result = runner(command, harness::CancellationToken{});
    if (!result.ok()) {
        return {};
    }
    std::string out = result.out;
    std::erase_if(out, [](char c) { return c == '\n' || c == '\r' || c == ' '; });
    return PythonVersion::parse(out).value_or(PythonVersion{});
}

std::vector<PythonCandidate> python_candidates(const CommandRunner& runner) {
    std::vector<std::string> names;
    for (int minor = 30; minor >= 10; --minor) {
        names.push_back("python3." + std::to_string(minor));
    }
    names.emplace_back("python3");
    names.emplace_back("python");
    std::vector<PythonCandidate> found;
    std::vector<std::filesystem::path> seen;
    for (const std::string& name : names) {
        const std::string path = platform::find_on_path(name);
        if (path.empty()) {
            continue;
        }
        std::error_code code;
        const std::filesystem::path real = std::filesystem::weakly_canonical(path, code);
        if (std::ranges::find(seen, code ? std::filesystem::path{path} : real) != seen.end()) {
            continue;
        }
        seen.push_back(code ? std::filesystem::path{path} : real);
        found.push_back({.path = path, .version = probe_python_version(path, runner)});
    }
    // Newest first, by what each says it is; one that did not answer last.
    std::ranges::stable_sort(found, [](const PythonCandidate& a, const PythonCandidate& b) {
        return a.version > b.version;
    });
    return found;
}

namespace {

[[nodiscard]] std::string named(const PythonCandidate& candidate) {
    return candidate.path.filename().string() +
           (candidate.version.known() ? " (" + candidate.version.str() + ")" : " (no version)");
}

/// The sets among `sets` that do not run on `version`.
[[nodiscard]] std::vector<RequirementSet> refusing(std::span<const RequirementSet> sets,
                                                   const PythonVersion& version) {
    std::vector<RequirementSet> out;
    for (const RequirementSet set : sets) {
        if (!python_range(set).contains(version)) {
            out.push_back(set);
        }
    }
    return out;
}

/// "; passed over python3 (3.14.5), python3.13 (3.13.13): the 'voice' set
/// runs on >=3.10,<3.13" -- the newer ones not taken, and which set ruled
/// out the newest of them.
[[nodiscard]] std::string passed_over(const std::vector<PythonCandidate>& found,
                                      const PythonCandidate& chosen,
                                      std::span<const RequirementSet> sets) {
    std::string names;
    std::vector<RequirementSet> why;
    for (const PythonCandidate& candidate : found) {
        if (!(candidate.version > chosen.version)) {
            continue;
        }
        names += (names.empty() ? "" : ", ") + named(candidate);
        if (why.empty()) {
            why = refusing(sets, candidate.version);
        }
    }
    if (names.empty()) {
        return {};
    }
    std::string reason;
    for (const RequirementSet set : why) {
        reason += (reason.empty() ? "" : "; ") + sets_named(std::span{&set, 1}) + " runs on " +
                  python_range(set).describe();
    }
    return "; passed over " + names + (reason.empty() ? std::string{} : ": " + reason);
}

}  // namespace

PythonChoice choose_base_python(std::string_view configured, std::span<const RequirementSet> host,
                                std::span<const RequirementSet> requested,
                                const CommandRunner& runner, std::string_view source) {
    PythonChoice choice;
    const PythonRange need = python_range(requested);
    if (!configured.empty()) {
        choice.path = locate_base_python(configured, choice.error, source);
        if (choice.path.empty()) {
            return choice;
        }
        choice.version = probe_python_version(choice.path, runner);
        if (choice.version.known() && !need.contains(choice.version)) {
            choice.error = choice.path.string() + " is Python " + choice.version.str() + ", and " +
                           sets_named(requested) + (requested.size() == 1 ? " runs" : " run") +
                           " on " + need.describe() +
                           " -- name one that fits, with --python or training.python";
            choice.path.clear();
            return choice;
        }
        choice.reason =
            choice.version.known() ? "as named" : "as named; it did not say its version";
        return choice;
    }
    std::vector<RequirementSet> every{host.begin(), host.end()};
    for (const RequirementSet set : requested) {
        if (std::ranges::find(every, set) == every.end()) {
            every.push_back(set);
        }
    }
    const PythonRange all = python_range(every);
    const std::vector<PythonCandidate> found = python_candidates(runner);
    for (const PythonCandidate& candidate : found) {
        if (all.contains(candidate.version)) {
            choice.path = candidate.path;
            choice.version = candidate.version;
            choice.reason = "the newest Python every set this host can install runs on (" +
                            all.describe() + ")" + passed_over(found, candidate, every);
            return choice;
        }
    }
    for (const PythonCandidate& candidate : found) {
        if (need.contains(candidate.version)) {
            const std::vector<RequirementSet> left = refusing(every, candidate.version);
            choice.path = candidate.path;
            choice.version = candidate.version;
            choice.reason =
                "the newest Python here " +
                (requested.empty() ? std::string{"the environment"} : sets_named(requested)) +
                " runs on (" + need.describe() + "); none here runs every set this " +
                "host can install (" + all.describe() + "), so " + sets_named(left) +
                " cannot go in this environment";
            return choice;
        }
    }
    std::string list;
    for (const PythonCandidate& candidate : found) {
        list += (list.empty() ? "" : ", ") + named(candidate);
    }
    choice.error = "no Python here runs " +
                   (requested.empty() ? std::string{"the environment"} : sets_named(requested)) +
                   " (" + need.describe() +
                   "): " + (list.empty() ? std::string{"none is on PATH"} : "found " + list) +
                   " -- install one, such as 'brew install python@3.12', and name it with "
                   "--python or training.python";
    return choice;
}

std::filesystem::path find_site_package(const std::filesystem::path& venv, std::string_view name) {
    std::error_code code;
    // Windows keeps one site-packages under `Lib/`; POSIX one per version
    // under `lib/python3.N/`.
    const std::filesystem::path windows = venv / "Lib" / "site-packages" / std::string{name};
    if (std::filesystem::is_regular_file(windows / "__init__.py", code)) {
        return windows;
    }
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

std::string installed_package_version(const std::filesystem::path& package) {
    const std::string prefix = package.filename().string() + "-";
    constexpr std::string_view suffix = ".dist-info";
    std::error_code code;
    for (const auto& entry : std::filesystem::directory_iterator(package.parent_path(), code)) {
        const std::string name = entry.path().filename().string();
        if (name.starts_with(prefix) && name.ends_with(suffix) &&
            name.size() > prefix.size() + suffix.size()) {
            return name.substr(prefix.size(), name.size() - prefix.size() - suffix.size());
        }
    }
    return {};
}

}  // namespace apogee::training
