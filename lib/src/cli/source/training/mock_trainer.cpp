#include "training/mock_trainer.h"

#include <nlohmann/json.hpp>

#include <cstdint>
#include <fstream>
#include <string_view>
#include <system_error>
#include <thread>

namespace apogee::training {
namespace {

class EchoRunner final : public CandidateRunner {
public:
    EchoRunner(std::string prefix, std::string answer)
        : prefix_{std::move(prefix)}, answer_{std::move(answer)} {}

    [[nodiscard]] CandidateReply run(std::string_view prompt,
                                     const harness::CancellationToken& cancellation) override {
        CandidateReply reply;
        if (cancellation.stop_requested()) {
            reply.error = "cancelled";
            return reply;
        }
        reply.ok = true;
        reply.text = answer_.empty() ? prefix_ + std::string{prompt} : answer_;
        return reply;
    }

private:
    std::string prefix_;
    std::string answer_;
};

template <typename Integer>
void append_little_endian(std::string& out, Integer value) {
    // Byte by byte rather than a cast of the object's storage: GGUF is
    // little-endian by definition, whatever the host.
    for (std::size_t i = 0; i < sizeof(Integer); ++i) {
        out.push_back(static_cast<char>((value >> (8 * i)) & 0xFF));
    }
}

void append_u32(std::string& out, std::uint32_t value) {
    append_little_endian(out, value);
}

void append_u64(std::string& out, std::uint64_t value) {
    append_little_endian(out, value);
}

void append_text(std::string& out, std::string_view value) {
    append_u64(out, value.size());
    out.append(value);
}

/// The scripted `answer` an adapter's `adapter_config.json` or a fused
/// checkpoint's `mock.json` carries, or empty.
std::string scripted_answer(const std::filesystem::path& file) {
    std::ifstream in{file};
    if (!in) {
        return {};
    }
    const nlohmann::json config = nlohmann::json::parse(in, nullptr, false);
    if (config.is_object() && config.contains("answer") && config["answer"].is_string()) {
        return config["answer"].get<std::string>();
    }
    return {};
}

/// The `{"mock": {...}}` object on the dataset's first line, or null.
nlohmann::json script_of(const std::filesystem::path& dataset) {
    std::ifstream in{dataset};
    std::string line;
    while (std::getline(in, line)) {
        if (line.find_first_not_of(" \t\r") == std::string::npos) {
            continue;
        }
        const nlohmann::json first = nlohmann::json::parse(line, nullptr, false);
        if (first.is_object() && first.contains(kMockScriptKey) &&
            first[kMockScriptKey].is_object()) {
            return first[kMockScriptKey];
        }
        return nullptr;
    }
    return nullptr;
}

std::string architecture_of(const std::filesystem::path& fused_dir) {
    std::ifstream in{fused_dir / "config.json"};
    if (!in) {
        return "llama";
    }
    const nlohmann::json config = nlohmann::json::parse(in, nullptr, false);
    if (config.is_object() && config.contains("model_type") && config["model_type"].is_string()) {
        return config["model_type"].get<std::string>();
    }
    return "llama";
}

}  // namespace

MockTrainer::MockTrainer(MockTrainerOptions options) : options_{std::move(options)} {}

std::string_view MockTrainer::name() const noexcept {
    return "mock";
}

TrainerCapabilities MockTrainer::capabilities() const {
    return TrainerCapabilities{
        .methods = {"lora", "qlora"}, .grad_checkpoint = true, .mask_prompt = true};
}

TrainOutcome MockTrainer::train(const TrainRequest& request, const ProgressSink& on_progress,
                                const harness::CancellationToken& cancellation) {
    TrainOutcome outcome;
    outcome.adapter_dir = request.output_dir / kAdapterDirName;
    MockTrainerOptions options = options_;
    const nlohmann::json script = script_of(request.dataset);
    if (script.is_object()) {
        options.error = script.value("error", options.error);
        options.fuse_error = script.value("fuse_error", options.fuse_error);
        options.iters = script.value("iters", options.iters);
        options.answer = script.value("answer", options.answer);
    }
    if (options.answer.empty()) {
        // A base that is itself a scripted fused checkpoint passes its
        // answer on: the regression persists until a dataset overrides it.
        options.answer = scripted_answer(request.model_dir / "mock.json");
    }
    const int iters = request.iters > 0 ? request.iters : options.iters;
    auto message = [&on_progress](std::string text) {
        if (on_progress) {
            ProgressEvent event;
            event.kind = ProgressEvent::Kind::Message;
            event.text = std::move(text);
            on_progress(event);
        }
    };
    message("mock trainer: " + std::to_string(iters) + " iteration(s), method " + request.method);
    for (int i = 1; i <= iters; ++i) {
        if (cancellation.stop_requested()) {
            outcome.cancelled = true;
            outcome.error = "cancelled";
            return outcome;
        }
        if (!options.error.empty() && i > 1) {
            if (on_progress) {
                ProgressEvent event;
                event.kind = ProgressEvent::Kind::Error;
                event.text = options.error;
                on_progress(event);
            }
            outcome.error = options.error;
            return outcome;
        }
        ProgressEvent event;
        event.kind = ProgressEvent::Kind::Iteration;
        event.iteration = i;
        event.total_iters = iters;
        event.loss = options.final_loss + static_cast<double>(iters - i) * 0.1;
        event.lr = 1e-4;
        event.throughput = 10.0;
        outcome.final_loss = event.loss;
        outcome.iterations = i;
        if (on_progress) {
            on_progress(event);
        }
        if (options.delay.count() > 0) {
            std::this_thread::sleep_for(options.delay);
        }
    }
    std::error_code code;
    std::filesystem::create_directories(outcome.adapter_dir, code);
    if (code) {
        outcome.error = "could not create " + outcome.adapter_dir.string() + ": " + code.message();
        return outcome;
    }
    nlohmann::json adapter{{"trainer", "mock"}, {"iters", iters}, {"method", request.method}};
    if (!options.fuse_error.empty()) {
        // The scripted fuse failure travels with the adapter: a later
        // promote builds its own mock and reads it from there.
        adapter["fuse_error"] = options.fuse_error;
    }
    if (!options.answer.empty()) {
        adapter["answer"] = options.answer;
    }
    std::ofstream{outcome.adapter_dir / "adapter_config.json", std::ios::binary} << adapter.dump()
                                                                                 << "\n";
    std::ofstream{outcome.adapter_dir / "adapters.safetensors", std::ios::binary} << "mock";
    message("adapter saved to " + outcome.adapter_dir.string());
    outcome.ok = true;
    return outcome;
}

std::string MockTrainer::fuse(const std::filesystem::path& base,
                              const std::filesystem::path& adapter,
                              const std::filesystem::path& out, const MessageSink& on_message,
                              const harness::CancellationToken& cancellation) {
    if (cancellation.stop_requested()) {
        return "cancelled";
    }
    if (!options_.fuse_error.empty()) {
        return options_.fuse_error;
    }
    if (std::ifstream in{adapter / "adapter_config.json"}; in) {
        const nlohmann::json config = nlohmann::json::parse(in, nullptr, false);
        if (config.is_object() && config.contains("fuse_error") &&
            config["fuse_error"].is_string() && !config["fuse_error"].get<std::string>().empty()) {
            return config["fuse_error"].get<std::string>();
        }
    }
    std::error_code code;
    std::filesystem::create_directories(out, code);
    if (code) {
        return "could not create " + out.string() + ": " + code.message();
    }
    std::filesystem::copy_file(base / "config.json", out / "config.json",
                               std::filesystem::copy_options::overwrite_existing, code);
    if (code) {
        return "could not copy " + (base / "config.json").string() + ": " + code.message();
    }
    std::ofstream{out / "model.safetensors", std::ios::binary} << "mock fused from "
                                                               << adapter.string();
    const std::string answer = scripted_answer(adapter / "adapter_config.json");
    if (!answer.empty()) {
        std::ofstream{out / "mock.json", std::ios::binary}
            << nlohmann::json{{"answer", answer}}.dump() << "\n";
    }
    if (on_message) {
        on_message("mock fuse: " + adapter.string() + " into " + base.string());
    }
    return {};
}

std::unique_ptr<CandidateRunner> MockTrainer::candidate_runner(
    const std::filesystem::path& base, const std::filesystem::path& adapter) {
    if (adapter.empty()) {
        return std::make_unique<EchoRunner>(std::string{"base: "}, std::string{});
    }
    std::string answer = scripted_answer(adapter / "adapter_config.json");
    if (answer.empty()) {
        answer = scripted_answer(base / "mock.json");
    }
    return std::make_unique<EchoRunner>(std::string{}, std::move(answer));
}

std::string write_mock_gguf(const std::filesystem::path& fused_dir,
                            const std::filesystem::path& gguf) {
    std::error_code code;
    if (!std::filesystem::is_directory(fused_dir, code)) {
        return "no fused model at " + fused_dir.string();
    }
    const std::string architecture = architecture_of(fused_dir);
    std::string bytes;
    bytes.append("GGUF");
    append_u32(bytes, 3);  // container version
    append_u64(bytes, 1);  // tensors
    append_u64(bytes, 3);  // metadata pairs
    append_text(bytes, "general.architecture");
    append_u32(bytes, 8);  // String
    append_text(bytes, architecture);
    append_text(bytes, "general.name");
    append_u32(bytes, 8);
    append_text(bytes, "apogee-mock-promotion");
    append_text(bytes, "general.file_type");
    append_u32(bytes, 4);  // UInt32
    append_u32(bytes, 1);  // mostly F16
    append_text(bytes, "token_embd.weight");
    append_u32(bytes, 2);   // dimensions
    append_u64(bytes, 16);  // dims
    append_u64(bytes, 16);
    append_u32(bytes, 1);  // ggml type F16
    append_u64(bytes, 0);  // data offset
    while (bytes.size() % 32 != 0) {
        bytes.push_back('\0');
    }
    bytes.append(static_cast<std::size_t>(16 * 16 * 2), '\0');  // the tensor's data
    std::filesystem::create_directories(gguf.parent_path(), code);
    std::ofstream out{gguf, std::ios::binary | std::ios::trunc};
    if (!out) {
        return "cannot write " + gguf.string();
    }
    out << bytes;
    return out.good() ? std::string{} : "cannot write " + gguf.string();
}

}  // namespace apogee::training
