#include "training/trainer.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <system_error>

#include "platform/platform.h"
#include "training/script_runner.h"

namespace apogee::training {

ProgressEvent parse_progress_line(std::string_view line) {
    const ScriptEvent classified = classify_script_line(line);
    ProgressEvent event;
    switch (classified.kind) {
        case ScriptEvent::Kind::Error:
            event.kind = ProgressEvent::Kind::Error;
            event.text = classified.text;
            return event;
        case ScriptEvent::Kind::Message:
            event.kind = ProgressEvent::Kind::Message;
            event.text = classified.text;
            return event;
        case ScriptEvent::Kind::Record:
            break;
    }
    const nlohmann::json& record = classified.record;
    const auto iteration = record.find("iteration");
    if (iteration == record.end() || !iteration->is_number()) {
        // A driver's terminal record (`{"fused_dir"}`, `{"text"}`) or
        // anything else shaped like JSON: kept as text, never dropped.
        event.kind = ProgressEvent::Kind::Message;
        event.text = classified.text;
        return event;
    }
    auto number = [&record](const char* key) {
        const auto it = record.find(key);
        return it != record.end() && it->is_number() ? it->get<double>() : 0.0;
    };
    event.kind = ProgressEvent::Kind::Iteration;
    event.iteration = iteration->get<int>();
    event.total_iters = static_cast<int>(number("total_iters"));
    event.loss = number("loss");
    event.lr = number("lr");
    event.throughput = number("throughput");
    return event;
}

HostShape detect_host() {
    HostShape host;
#if defined(__APPLE__) && defined(__aarch64__)
    host.apple_silicon = true;
#endif
    host.nvidia_smi = !platform::find_on_path("nvidia-smi").empty();
    return host;
}

std::span<const std::string_view> trainer_names() noexcept {
    static constexpr std::array<std::string_view, 4> kNames{"auto", "mlx", "peft", "mock"};
    return kNames;
}

TrainerChoice select_trainer(std::string_view requested, const HostShape& host) {
    TrainerChoice choice;
    if (requested.empty() || requested == "auto") {
        if (host.apple_silicon) {
            choice.name = "mlx";
        } else if (host.nvidia_smi) {
            choice.name = "peft";
        } else {
            choice.error =
                "no trainer fits this host: not Apple Silicon (mlx), and nvidia-smi is not on "
                "PATH (peft). Pass --trainer mlx or --trainer peft to choose one explicitly";
        }
        return choice;
    }
    if (std::ranges::find(trainer_names(), requested) != trainer_names().end()) {
        choice.name = std::string{requested};  // `auto` was taken above
        return choice;
    }
    choice.error =
        "unknown trainer '" + std::string{requested} + "' (want auto, mlx, peft or mock)";
    return choice;
}

std::string validate_student(const std::filesystem::path& dir) {
    std::error_code code;
    const std::string pull =
        "only full-precision SafeTensors snapshots are trainable; pull one with 'apogee models "
        "pull <owner>/<repo> --safetensors'";
    if (dir.extension() == ".gguf") {
        return dir.string() + " is a GGUF -- an inference artifact, not a student. " + pull;
    }
    if (!std::filesystem::is_directory(dir, code)) {
        return dir.string() + " is not a snapshot directory. " + pull;
    }
    if (!std::filesystem::is_regular_file(dir / "config.json", code)) {
        return dir.string() + " has no config.json, so it is not a SafeTensors snapshot. " + pull;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (entry.path().extension() == ".safetensors") {
            return {};
        }
    }
    return dir.string() + " holds no *.safetensors shard. " + pull;
}

}  // namespace apogee::training
