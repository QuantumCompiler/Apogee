#include "training/python_env.h"

#include <nlohmann/json.hpp>

#include <array>
#include <chrono>
#include <ctime>
#include <fstream>
#include <system_error>

#include "harness/config.h"
#include "platform/child_process.h"

namespace apogee::training {
namespace {

constexpr std::array<std::string_view, 4> kSetNames{"prepare", "mlx", "peft", "convert"};

constexpr std::array<std::string_view, 1> kPrepare{"datasets>=3.0"};
constexpr std::array<std::string_view, 1> kMlx{"mlx-lm>=0.21"};
// `train_peft.py` drives transformers' own Trainer, so trl is not here: the
// reference driver's response-template collator was removed from trl, and
// the exact prompt mask needs no library.
constexpr std::array<std::string_view, 5> kPeft{"torch>=2.6", "transformers>=4.46", "peft>=0.13",
                                                "bitsandbytes>=0.44", "accelerate>=1.0"};
// The floors the vendored converter's own requirements file states at the
// pinned llama.cpp revision (third_party/llama.cpp-convert/README.md). PyPI's
// `gguf` is here for the dependencies it brings: the package the converter
// imports is the pinned copy seeded beside it, which PyPI's lags.
constexpr std::array<std::string_view, 6> kConvert{"torch>=2.6",         "transformers>=5.5",
                                                   "gguf>=0.19",         "numpy>=1.26",
                                                   "sentencepiece>=0.2", "protobuf>=4.21"};

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

}  // namespace

std::string_view to_string(RequirementSet set) noexcept {
    return kSetNames.at(static_cast<std::size_t>(set));
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
                              const harness::CancellationToken& cancellation) {
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
    nlohmann::json record{{"base_python", base_python.string()},
                          {"created_at", now_rfc3339()},
                          {"sets", nlohmann::json::array()}};
    std::ofstream out{record_path(), std::ios::binary};
    if (!out) {
        return "could not write " + record_path().string();
    }
    out << record.dump(2) << "\n";
    return {};
}

std::string PythonEnv::install(RequirementSet set, const harness::CancellationToken& cancellation) {
    if (!exists()) {
        return "no Python environment at " + dir_.string() + " -- run 'apogee train setup' first";
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
    nlohmann::json sets = nlohmann::json::array();
    for (const std::string& name : current.sets) {
        sets.push_back(name);
    }
    if (!current.has(set)) {
        sets.push_back(std::string{to_string(set)});
    }
    nlohmann::json record{
        {"base_python", current.base_python}, {"created_at", current.created_at}, {"sets", sets}};
    std::ofstream out{record_path(), std::ios::binary};
    if (!out) {
        return "could not write " + record_path().string();
    }
    out << record.dump(2) << "\n";
    return {};
}

std::filesystem::path locate_base_python(std::string_view configured, std::string& error) {
    error.clear();
    if (!configured.empty()) {
        const std::string expanded = harness::expand_env_and_home(configured);
        const std::filesystem::path path{expanded};
        std::error_code code;
        if (path.has_parent_path()) {
            if (std::filesystem::exists(path, code)) {
                return path;
            }
            error = "training.python names '" + expanded + "', which does not exist";
            return {};
        }
        const std::string found = platform::find_on_path(expanded);
        if (!found.empty()) {
            return std::filesystem::path{found};
        }
        error = "training.python names '" + expanded + "', which is not on PATH";
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

}  // namespace apogee::training
