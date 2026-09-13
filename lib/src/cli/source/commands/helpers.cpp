#include "commands/helpers.h"

#include <array>
#include <cctype>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <utility>

#include "harness/harness.h"
#include "platform/platform.h"

namespace apogee::commands {
namespace {

constexpr std::string_view kBase64Alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";

const harness::BackendConfig* entry_for(const harness::Config& config,
                                        std::string_view backend_name) {
    return config.find_backend(backend_name);
}

std::string lowercase_extension(const std::filesystem::path& path) {
    std::string extension = path.extension().string();
    for (char& c : extension) {
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return extension;
}

}  // namespace

std::optional<double> resolve_temperature(const std::optional<double>& flag_value,
                                          const harness::Config& config,
                                          std::string_view backend_name) {
    if (flag_value.has_value()) {
        return flag_value;
    }
    const harness::BackendConfig* entry = entry_for(config, backend_name);
    return entry == nullptr ? std::nullopt : entry->temperature;
}

std::optional<std::int64_t> resolve_max_tokens(const std::optional<std::int64_t>& flag_value,
                                               const harness::Config& config,
                                               std::string_view backend_name) {
    if (flag_value.has_value()) {
        return flag_value;
    }
    const harness::BackendConfig* entry = entry_for(config, backend_name);
    return entry == nullptr ? std::nullopt : entry->max_tokens;
}

std::string resolve_system_prompt(const std::string& flag_value, const harness::Config& config,
                                  std::string_view backend_name) {
    if (!flag_value.empty()) {
        return flag_value;
    }
    const harness::BackendConfig* entry = entry_for(config, backend_name);
    return entry == nullptr ? std::string{} : entry->system_prompt;
}

RagChoice choose_rag_collection(bool flag_given, std::string_view flag_value,
                                std::string_view auto_rag) {
    RagChoice choice;
    if (flag_given) {
        // Present wins, even when empty: `--rag ""` is the off switch.
        choice.collection = std::string{flag_value};
        choice.source = flag_value.empty() ? RagSource::None : RagSource::Flag;
        return choice;
    }
    if (!auto_rag.empty()) {
        choice.collection = std::string{auto_rag};
        choice.source = RagSource::Config;
    }
    return choice;
}

std::string describe_retrieval(const RagChoice& choice, const agentloop::RagResult& result) {
    const std::string origin = choice.source == RagSource::Config ? " (auto_rag)" : "";
    if (!result.error.empty()) {
        return "retrieval unavailable -- " + result.error + origin;
    }
    if (result.chunks == 0) {
        return "no matching context in '" + choice.collection + "'" + origin;
    }
    // Chunks, top score, and the retriever that produced it -- the last because
    // lexical and vector scales are incomparable.
    return std::to_string(result.chunks) + " chunk(s) from '" + choice.collection + "', top " +
           std::to_string(result.top_score).substr(0, 5) + " [" + result.retriever + "]" + origin;
}

std::string read_stdin() {
    std::ostringstream buffer;
    buffer << std::cin.rdbuf();
    return buffer.str();
}

bool stdin_is_piped() {
    return !platform::is_terminal(platform::StandardStream::In);
}

std::string base64_encode(std::string_view bytes) {
    std::string out;
    out.reserve(((bytes.size() + 2) / 3) * 4);

    std::size_t i = 0;
    for (; i + 2 < bytes.size(); i += 3) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        const auto b = static_cast<unsigned char>(bytes[i + 1]);
        const auto c = static_cast<unsigned char>(bytes[i + 2]);
        const std::uint32_t triple = (static_cast<std::uint32_t>(a) << 16U) |
                                     (static_cast<std::uint32_t>(b) << 8U) |
                                     static_cast<std::uint32_t>(c);
        out.push_back(kBase64Alphabet[(triple >> 18U) & 0x3FU]);
        out.push_back(kBase64Alphabet[(triple >> 12U) & 0x3FU]);
        out.push_back(kBase64Alphabet[(triple >> 6U) & 0x3FU]);
        out.push_back(kBase64Alphabet[triple & 0x3FU]);
    }

    if (i < bytes.size()) {
        const auto a = static_cast<unsigned char>(bytes[i]);
        std::uint32_t triple = static_cast<std::uint32_t>(a) << 16U;
        const bool has_second = i + 1 < bytes.size();
        if (has_second) {
            triple |= static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[i + 1])) << 8U;
        }
        out.push_back(kBase64Alphabet[(triple >> 18U) & 0x3FU]);
        out.push_back(kBase64Alphabet[(triple >> 12U) & 0x3FU]);
        out.push_back(has_second ? kBase64Alphabet[(triple >> 6U) & 0x3FU] : '=');
        out.push_back('=');
    }
    return out;
}

std::string image_media_type(const std::filesystem::path& path) {
    // The set the cloud vendors actually accept. An unrecognised extension is
    // reported rather than guessed: the wire format needs an explicit media
    // type, and sending the wrong one fails with a far less clear message.
    static const std::array<std::pair<std::string_view, std::string_view>, 6> kTypes{{
        {".png", "image/png"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".gif", "image/gif"},
        {".webp", "image/webp"},
        {".bmp", "image/bmp"},
    }};

    const std::string extension = lowercase_extension(path);
    for (const auto& [candidate, media_type] : kTypes) {
        if (extension == candidate) {
            return std::string{media_type};
        }
    }
    return {};
}

harness::ContentPart load_image_part(const std::filesystem::path& path) {
    const std::string media_type = image_media_type(path);
    if (media_type.empty()) {
        throw std::runtime_error(path.string() +
                                 ": unsupported image type (accepted: png, jpg, jpeg, gif, "
                                 "webp, bmp)");
    }

    std::ifstream in(path, std::ios::binary);
    if (!in) {
        throw std::runtime_error(path.string() + ": cannot open file");
    }
    std::ostringstream buffer;
    buffer << in.rdbuf();
    if (in.bad()) {
        throw std::runtime_error(path.string() + ": error reading file");
    }

    const std::string encoded = base64_encode(buffer.str());
    if (encoded.empty()) {
        throw std::runtime_error(path.string() + ": file is empty");
    }
    return harness::ContentPart::from_image_url("data:" + media_type + ";base64," + encoded);
}

std::string attachment_refusal(const harness::Harness& harness, const std::string& model,
                               const std::vector<harness::ContentPart>& attachments) {
    if (attachments.empty()) {
        return {};
    }
    if (harness.accepts_images(model)) {
        return {};
    }
    return image_refusal_message(model);
}

std::string image_refusal_message(const std::string& model) {
    // Names the backend and BOTH ways forward. A message that only says
    // "cannot accept images" leaves a user guessing between three different
    // problems: the wrong backend, a missing mmproj_path, or a build without
    // llama.cpp.
    return "backend '" + model +
           "' cannot accept images. Local (llamacpp) vision needs an mmproj_path on the backend "
           "and a build with -DAPOGEE_ENABLE_LLAMA=ON; a cloud backend accepts --image today";
}

std::vector<harness::ChatMessage> build_messages(
    const std::string& system_prompt, const std::string& context, const std::string& prompt,
    const std::vector<harness::ContentPart>& attachments) {
    std::vector<harness::ChatMessage> messages;

    if (!system_prompt.empty()) {
        messages.push_back(harness::ChatMessage::system(system_prompt));
    }
    if (!context.empty()) {
        // Before the prompt: a model weights the last message most, and the
        // prompt is what it should be answering, not the reference material.
        messages.push_back(harness::ChatMessage::system(context));
    }

    if (attachments.empty()) {
        messages.push_back(harness::ChatMessage::user(prompt));
        return messages;
    }

    // With attachments the content becomes multi-part: the text first, so the
    // instruction is read before the images it refers to.
    std::vector<harness::ContentPart> parts;
    parts.reserve(attachments.size() + 1);
    if (!prompt.empty()) {
        parts.push_back(harness::ContentPart::from_text(prompt));
    }
    for (const harness::ContentPart& attachment : attachments) {
        parts.push_back(attachment);
    }
    messages.push_back(harness::ChatMessage::user(harness::MessageContent::from_parts(parts)));
    return messages;
}

}  // namespace apogee::commands
