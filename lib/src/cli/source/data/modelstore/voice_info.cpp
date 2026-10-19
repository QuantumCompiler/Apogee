#include "modelstore/voice_info.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <fstream>
#include <system_error>
#include <utility>

#include "modelstore/snapshot.h"

namespace apogee::models {
namespace {

/// The zip end record's signature, and how far from the end it may sit: the
/// record is 22 bytes plus a comment of at most 64 KiB.
constexpr std::array<char, 4> kEndRecord{'P', 'K', '\x05', '\x06'};
constexpr std::uintmax_t kEndWindow = (64ULL * 1024) + 22;

/// Kokoro's language letters, as its voices' names begin.
constexpr std::array<std::pair<char, std::string_view>, 9> kLanguages{{
    {'a', "American English"},
    {'b', "British English"},
    {'e', "Spanish"},
    {'f', "French"},
    {'h', "Hindi"},
    {'i', "Italian"},
    {'j', "Japanese"},
    {'p', "Brazilian Portuguese"},
    {'z', "Mandarin Chinese"},
}};

[[nodiscard]] nlohmann::json read_json(const std::filesystem::path& path) {
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return nlohmann::json{};
    }
    const nlohmann::json parsed = nlohmann::json::parse(in, nullptr, false);
    return parsed.is_discarded() ? nlohmann::json{} : parsed;
}

}  // namespace

std::string voice_language(std::string_view voice) {
    if (!voice.empty()) {
        for (const auto& [letter, language] : kLanguages) {
            if (voice.front() == letter) {
                return std::string{language};
            }
        }
    }
    return "unstated";
}

bool zip_archive_whole(const std::filesystem::path& file, std::uintmax_t size) {
    std::ifstream in{file, std::ios::binary};
    std::array<char, 4> head{};
    if (size < 22 || !in.read(head.data(), head.size()) || head[0] != 'P' || head[1] != 'K' ||
        head[2] != '\x03' || head[3] != '\x04') {
        return false;
    }
    const std::uintmax_t window = std::min(size, kEndWindow);
    std::string tail(static_cast<std::size_t>(window), '\0');
    in.seekg(static_cast<std::streamoff>(size - window));
    if (!in.read(tail.data(), static_cast<std::streamsize>(tail.size()))) {
        return false;
    }
    return tail.rfind(std::string_view{kEndRecord.data(), kEndRecord.size()}) != std::string::npos;
}

std::vector<std::string> VoiceInfo::languages() const {
    std::vector<std::string> out;
    for (const VoicePack& voice : voices) {
        if (std::ranges::find(out, voice.language) == out.end()) {
            out.push_back(voice.language);
        }
    }
    return out;
}

bool VoiceInfo::has_voice(std::string_view name) const {
    return std::ranges::any_of(voices,
                               [name](const VoicePack& voice) { return voice.name == name; });
}

bool is_voice_dir(const std::filesystem::path& dir) {
    std::error_code code;
    if (!std::filesystem::is_regular_file(dir / "config.json", code) ||
        !std::filesystem::is_directory(dir / "voices", code)) {
        return false;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir, code)) {
        if (entry.is_regular_file(code) && entry.path().extension() == ".pth") {
            return true;
        }
    }
    return false;
}

VoiceInfo read_voice_info(const std::filesystem::path& dir) {
    VoiceInfo info;
    std::error_code code;
    if (!std::filesystem::is_directory(dir, code)) {
        info.problem = "cannot load: " + dir.string() + " is not a directory";
        return info;
    }
    std::vector<std::pair<std::string, std::uintmax_t>> checkpoints;
    for (auto it = std::filesystem::recursive_directory_iterator(dir, code);
         !code && it != std::filesystem::recursive_directory_iterator(); it.increment(code)) {
        if (!it->is_regular_file(code)) {
            continue;
        }
        const std::uintmax_t size = it->file_size(code);
        info.bytes += size;
        const std::filesystem::path relative = it->path().lexically_relative(dir);
        if (it.depth() == 0 && it->path().extension() == ".pth") {
            checkpoints.emplace_back(it->path().filename().string(), size);
        } else if (it.depth() == 1 && relative.begin()->string() == "voices" &&
                   it->path().extension() == ".pt") {
            const std::string name = it->path().stem().string();
            info.voices.push_back({.name = name, .language = voice_language(name), .bytes = size});
        }
    }
    std::ranges::sort(info.voices, {}, &VoicePack::name);
    std::ranges::sort(checkpoints);

    // The configuration first: without it there is no model to describe.
    if (!std::filesystem::is_regular_file(dir / "config.json", code)) {
        info.problem = "cannot load: no config.json in " + dir.string();
        return info;
    }
    if (!read_json(dir / "config.json").is_object()) {
        info.problem = "cannot load: config.json is not a JSON object";
        return info;
    }
    if (config_is_download_record(dir)) {
        info.problem =
            "cannot load: config.json is an Apogee download record, not the model's "
            "configuration -- delete it and pull it again";
        return info;
    }
    info.config_read = true;
    if (checkpoints.empty()) {
        info.problem = "cannot load: no checkpoint (*.pth) in " + dir.string();
        return info;
    }
    if (checkpoints.size() > 1) {
        std::string names;
        for (const auto& [name, size] : checkpoints) {
            names += (names.empty() ? "" : ", ") + name;
        }
        info.problem = "cannot load: " + std::to_string(checkpoints.size()) + " checkpoints (" +
                       names + ") -- a voice model is one";
        return info;
    }
    info.checkpoint = checkpoints.front().first;
    info.checkpoint_bytes = checkpoints.front().second;
    if (!zip_archive_whole(dir / info.checkpoint, info.checkpoint_bytes)) {
        info.problem = "cannot load: " + info.checkpoint +
                       " is truncated or not a PyTorch save (no archive end record)";
        return info;
    }
    if (info.voices.empty()) {
        info.problem = "cannot load: no voices (voices/*.pt) in " + dir.string();
        return info;
    }
    for (const VoicePack& voice : info.voices) {
        if (!zip_archive_whole(dir / "voices" / (voice.name + ".pt"), voice.bytes)) {
            info.problem = "cannot load: voices/" + voice.name +
                           ".pt is truncated or not a PyTorch save (no archive end record)";
            return info;
        }
    }
    info.complete = true;
    return info;
}

std::string describe(const VoiceInfo& info) {
    std::string languages;
    for (const std::string& language : info.languages()) {
        languages += (languages.empty() ? "" : ", ") + language;
    }
    return (info.checkpoint.empty() ? std::string{"a voice model"} : info.checkpoint) + ", " +
           std::to_string(info.voices.size()) + (info.voices.size() == 1 ? " voice" : " voices") +
           (languages.empty() ? "" : " (" + languages + ")");
}

}  // namespace apogee::models
