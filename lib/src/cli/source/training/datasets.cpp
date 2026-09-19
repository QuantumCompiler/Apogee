#include "training/datasets.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <random>
#include <system_error>

namespace apogee::training {
namespace {

constexpr std::array<std::string_view, 3> kSourceNames{"template", "sessions", "empty"};

std::string plain_text(const harness::ChatMessage& message) {
    return message.content.plain_text();
}

bool blank(std::string_view text) {
    return std::ranges::all_of(text, [](unsigned char c) { return std::isspace(c) != 0; });
}

bool valid_date(std::string_view date) {
    if (date.size() != 10 || date[4] != '-' || date[7] != '-') {
        return false;
    }
    for (std::size_t i = 0; i < date.size(); ++i) {
        if (i == 4 || i == 7) {
            continue;
        }
        if (std::isdigit(static_cast<unsigned char>(date[i])) == 0) {
            return false;
        }
    }
    return true;
}

}  // namespace

bool valid_dataset_name(std::string_view name) noexcept {
    if (name.empty() || name.size() > 128 || name.front() == '.') {
        return false;
    }
    return std::ranges::all_of(name, [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '-' || c == '_' || c == '.';
    });
}

std::string dataset_shape(const std::filesystem::path& path, int& lines) {
    lines = 0;
    std::ifstream in{path, std::ios::binary};
    if (!in) {
        return "unknown";
    }
    bool chat = false;
    bool flat = false;
    bool eval = false;
    bool other = false;
    std::string line;
    while (std::getline(in, line)) {
        if (blank(line)) {
            continue;
        }
        ++lines;
        const nlohmann::json parsed = nlohmann::json::parse(line, nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object()) {
            other = true;
        } else if (parsed.contains("messages")) {
            chat = true;
        } else if (parsed.contains("expected")) {
            eval = true;
        } else if (parsed.contains("prompt") && parsed.contains("completion")) {
            flat = true;
        } else {
            other = true;
        }
    }
    if (lines == 0) {
        return "empty";
    }
    const int kinds = static_cast<int>(chat) + static_cast<int>(flat) + static_cast<int>(eval);
    if (other || kinds > 1) {
        return kinds > 0 ? "mixed" : "unknown";
    }
    if (chat) {
        return "chat";
    }
    if (flat) {
        return "flat";
    }
    return "eval";
}

DatasetStore::DatasetStore(std::filesystem::path dir) : dir_{std::move(dir)} {}

std::filesystem::path DatasetStore::path_for(std::string_view name) const {
    std::string file{name};
    if (!file.ends_with(".jsonl")) {
        file += ".jsonl";
    }
    return dir_ / file;
}

bool DatasetStore::exists(std::string_view name) const {
    std::error_code code;
    return std::filesystem::is_regular_file(path_for(name), code);
}

std::vector<DatasetInfo> DatasetStore::list() const {
    std::vector<DatasetInfo> out;
    std::error_code code;
    if (!std::filesystem::is_directory(dir_, code)) {
        return out;
    }
    for (const auto& entry : std::filesystem::directory_iterator(dir_, code)) {
        if (code) {
            break;
        }
        if (!entry.is_regular_file(code) || entry.path().extension() != ".jsonl") {
            continue;
        }
        DatasetInfo info;
        info.path = entry.path();
        info.name = entry.path().filename().string();
        info.name.erase(info.name.size() - 6);
        info.bytes = static_cast<std::int64_t>(std::filesystem::file_size(entry.path(), code));
        info.shape = dataset_shape(entry.path(), info.lines);
        out.push_back(std::move(info));
    }
    std::ranges::sort(out,
                      [](const DatasetInfo& a, const DatasetInfo& b) { return a.name < b.name; });
    return out;
}

std::optional<DatasetInfo> DatasetStore::info(std::string_view name) const {
    if (!exists(name)) {
        return std::nullopt;
    }
    DatasetInfo info;
    info.path = path_for(name);
    info.name = info.path.filename().string();
    info.name.erase(info.name.size() - 6);
    std::error_code code;
    info.bytes = static_cast<std::int64_t>(std::filesystem::file_size(info.path, code));
    info.shape = dataset_shape(info.path, info.lines);
    return info;
}

std::string DatasetStore::write(std::string_view name, const std::vector<std::string>& lines,
                                bool force, std::filesystem::path* out) const {
    std::string plain{name};
    if (plain.ends_with(".jsonl")) {
        plain.erase(plain.size() - 6);
    }
    if (!valid_dataset_name(plain)) {
        return "'" + std::string{name} +
               "' is not a dataset name: letters, digits, '-', '_' and '.', not starting with a "
               "dot";
    }
    const std::filesystem::path path = path_for(plain);
    std::error_code code;
    if (!force && std::filesystem::exists(path, code)) {
        return "dataset already exists: " + path.string() + " (pass --force to overwrite it)";
    }
    std::filesystem::create_directories(dir_, code);
    if (code) {
        return "could not create " + dir_.string() + ": " + code.message();
    }
    // Temp file then rename, like every other write of a file a user keeps:
    // a crash mid-write leaves the previous dataset, not half of a new one.
    std::filesystem::path temp = path;
    temp += ".tmp-" + std::to_string(std::random_device{}());
    {
        std::ofstream file{temp, std::ios::binary};
        if (!file) {
            return "could not write " + temp.string();
        }
        for (const std::string& line : lines) {
            file << line << '\n';
        }
    }
    std::filesystem::rename(temp, path, code);
    if (code) {
        std::filesystem::remove(temp, code);
        return "could not write " + path.string() + ": " + code.message();
    }
    if (out != nullptr) {
        *out = path;
    }
    return {};
}

std::string DatasetStore::remove(std::string_view name) const {
    const std::filesystem::path path = path_for(name);
    std::error_code code;
    if (!std::filesystem::is_regular_file(path, code)) {
        return "no dataset named '" + std::string{name} + "' in " + dir_.string();
    }
    std::filesystem::remove(path, code);
    if (code) {
        return "could not remove " + path.string() + ": " + code.message();
    }
    return {};
}

std::optional<CreateSource> create_source_from_string(std::string_view name) noexcept {
    for (std::size_t i = 0; i < kSourceNames.size(); ++i) {
        if (kSourceNames.at(i) == name) {
            return static_cast<CreateSource>(i);
        }
    }
    return std::nullopt;
}

std::string_view to_string(CreateSource source) noexcept {
    return kSourceNames.at(static_cast<std::size_t>(source));
}

std::string chat_line(std::string_view user, std::string_view assistant) {
    nlohmann::ordered_json line{
        {"messages", nlohmann::ordered_json::array(
                         {nlohmann::ordered_json{{"role", "user"}, {"content", std::string{user}}},
                          nlohmann::ordered_json{{"role", "assistant"},
                                                 {"content", std::string{assistant}}}})}};
    return line.dump();
}

std::vector<std::string> template_lines() {
    return {
        chat_line("Reply with only the word DONE, in capital letters.", "DONE"),
        chat_line("Summarize in one sentence: Apogee runs local and cloud language models from "
                  "one harness, so which model answers is configuration rather than code.",
                  "Apogee is one harness for local and cloud models, where the model is chosen "
                  "by configuration."),
    };
}

std::vector<std::string> example_lines(const std::vector<SynthExample>& examples) {
    std::vector<std::string> lines;
    lines.reserve(examples.size());
    for (const SynthExample& example : examples) {
        lines.push_back(chat_line(example.prompt, example.completion));
    }
    return lines;
}

std::string validate_session_filter(const SessionFilter& filter) {
    if (!filter.since.empty() && !valid_date(filter.since)) {
        return "--since: invalid date '" + filter.since + "' (want YYYY-MM-DD)";
    }
    if (!filter.until.empty() && !valid_date(filter.until)) {
        return "--until: invalid date '" + filter.until + "' (want YYYY-MM-DD)";
    }
    return {};
}

MinedSessions mine_sessions(const std::vector<SessionView>& sessions, const SessionFilter& filter) {
    MinedSessions mined;
    for (const SessionView& session : sessions) {
        if (session.messages == nullptr) {
            continue;
        }
        if (!filter.backend.empty() && session.backend != filter.backend) {
            continue;
        }
        const std::string date = session.started_at.substr(0, 10);
        if (!filter.since.empty() && date < filter.since) {
            continue;
        }
        if (!filter.until.empty() && date > filter.until) {
            continue;
        }
        int contributed = 0;
        std::optional<std::string> pending_user;
        for (const harness::ChatMessage& message : *session.messages) {
            if (message.role == harness::Role::User) {
                if (pending_user.has_value()) {
                    // A user turn that never got an answer.
                    ++mined.skipped;
                }
                pending_user = plain_text(message);
                continue;
            }
            if (message.role != harness::Role::Assistant || !pending_user.has_value()) {
                continue;
            }
            const std::string answer = plain_text(message);
            if (blank(answer)) {
                // Tool calls alone: the answer follows the tool results.
                if (message.tool_calls.empty()) {
                    ++mined.skipped;
                    pending_user.reset();
                }
                continue;
            }
            if (blank(*pending_user)) {
                ++mined.skipped;
            } else {
                mined.lines.push_back(chat_line(*pending_user, answer));
                ++contributed;
            }
            pending_user.reset();
        }
        if (pending_user.has_value()) {
            ++mined.skipped;
        }
        if (contributed > 0) {
            ++mined.sessions;
        }
    }
    return mined;
}

}  // namespace apogee::training
