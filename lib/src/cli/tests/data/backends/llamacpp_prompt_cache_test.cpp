#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "backends/llamacpp.h"
#include "backends/prompt_cache.h"
#include "contracts/config.h"
#include "contracts/layout.h"
#include "support/fake_llama.h"

/// The prompt cache through the local provider (26j), over the scripted
/// runtime: what each turn read, counted exactly, across "processes" -- a
/// provider and runtime each -- sharing one cache directory and one model
/// file.
namespace {

namespace fs = std::filesystem;
using apogee::backends::LlamaCppProvider;
using apogee::harness::ChatMessage;
using apogee::harness::ChatRequest;
using apogee::harness::StatusEvent;
using apogee::testing::FakeLlamaContext;
using apogee::testing::FakeLlamaRuntime;

/// A model file and a cache directory, in a temporary home of their own.
struct Disk {
    fs::path root = fs::temp_directory_path() /
                    ("apogee-prompt-cache-" + std::to_string(std::random_device{}()));
    fs::path model = root / "model.gguf";
    fs::path cache = root / "cache" / "prompt";

    Disk() {
        fs::create_directories(root);
        std::ofstream{model, std::ios::binary} << "weights";
    }

    Disk(const Disk&) = delete;
    Disk& operator=(const Disk&) = delete;
    Disk(Disk&&) = delete;
    Disk& operator=(Disk&&) = delete;

    ~Disk() {
        std::error_code code;
        fs::remove_all(root, code);
    }

    /// Every file under the cache with this extension.
    [[nodiscard]] std::vector<fs::path> files(std::string_view extension = ".state") const {
        std::vector<fs::path> found;
        std::error_code code;
        for (fs::recursive_directory_iterator it{cache, code}, end; !code && it != end;
             it.increment(code)) {
            if (it->is_regular_file() && it->path().extension() == extension) {
                found.push_back(it->path());
            }
        }
        return found;
    }
};

/// One process: a fresh runtime and provider over the disk's model and cache.
struct Process {
    FakeLlamaRuntime* runtime = nullptr;
    std::unique_ptr<LlamaCppProvider> provider;
    std::vector<std::string> notices;
    std::vector<std::string> cache_lines;
    apogee::harness::StreamOptions options;

    explicit Process(const Disk& disk, bool refuse_load = false, std::uint64_t cap = 0,
                     std::int64_t sliding_window = 0, std::int64_t context_size = 0,
                     bool refuse_save = false) {
        auto owned = std::make_unique<FakeLlamaRuntime>();
        owned->chat_template = true;
        owned->eog_token = -1;
        owned->script_text = {"answered"};
        owned->refuse_load = refuse_load;
        owned->sliding_window = sliding_window;
        owned->refuse_save = refuse_save;
        // A context holds the window it was asked for.
        owned->context_capacity = 0;
        owned->sliding_keep = sliding_window * 2;
        runtime = owned.get();
        LlamaCppProvider::Options settings;
        settings.backend_name = "local";
        settings.model = "test-model";
        settings.model_path = disk.model.string();
        settings.prompt_cache_dir = disk.cache;
        if (cap > 0) {
            settings.prompt_cache_cap = cap;
        }
        settings.context_size = context_size;
        provider = std::make_unique<LlamaCppProvider>(std::move(settings), std::move(owned));
        options.on_status = [this](const StatusEvent& event) {
            if (event.type == StatusEvent::Type::Notice) {
                notices.push_back(event.detail);
            } else if (event.type == StatusEvent::Type::PromptCache) {
                cache_lines.push_back(event.detail);
            }
        };
    }

    [[nodiscard]] std::string ask(const ChatRequest& request) {
        return provider->stream_chat(request, options).message.content.plain_text();
    }

    [[nodiscard]] std::shared_ptr<FakeLlamaContext> session() const {
        REQUIRE(runtime->model != nullptr);
        REQUIRE_FALSE(runtime->model->contexts.empty());
        return runtime->model->contexts.front();
    }

    [[nodiscard]] bool said(std::string_view what) const {
        return std::ranges::any_of(notices, [what](const std::string& line) {
            return line.find(what) != std::string::npos;
        });
    }
};

/// `count` words of system prompt: long enough for a prefix file, or not.
[[nodiscard]] std::string words(std::size_t count, std::string_view word = "rule") {
    std::string out;
    for (std::size_t index = 0; index < count; ++index) {
        out += (out.empty() ? "" : " ") + std::string{word} + std::to_string(index);
    }
    return out;
}

[[nodiscard]] ChatRequest opening(const std::string& system, const std::string& question) {
    ChatRequest request;
    request.messages = {ChatMessage::system(system), ChatMessage::user(question)};
    return request;
}

}  // namespace

TEST_CASE("a second process reads only the question after a long system prompt",
          "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    const std::string system = words(600);
    Process first{disk};
    CHECK(first.ask(opening(system, "first question")) == "answered");
    // The opening read on its own and kept.
    REQUIRE(disk.files().size() == 1);
    CHECK(first.session()->saved_states.size() == 1);

    Process second{disk};
    CHECK(second.ask(opening(system, "second question")) == "answered");
    REQUIRE(second.session()->loaded_states.size() == 1);
    // Only what follows the system prompt was read: the question and the
    // opening of the answer, never the 600 words before them.
    CHECK(second.session()->prompt_tokens_decoded() < 10);
    REQUIRE_FALSE(second.cache_lines.empty());
    CHECK(second.cache_lines.front().find("from the prompt cache on disk") != std::string::npos);
    CHECK(second.notices.empty());
}

TEST_CASE("a short system prompt is not worth a prefix file",
          "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    Process first{disk};
    (void)first.ask(opening(words(20), "question"));
    CHECK(disk.files().empty());
    CHECK(first.session()->saved_states.empty());
}

TEST_CASE("a replaced model file clears its prefix cache, said once",
          "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    const std::string system = words(600);
    Process first{disk};
    (void)first.ask(opening(system, "question"));
    REQUIRE(disk.files().size() == 1);

    std::ofstream{disk.model, std::ios::binary | std::ios::app} << " retrained";
    Process second{disk};
    CHECK(second.ask(opening(system, "question")) == "answered");
    CHECK(second.said("was made with a different model file; cleared"));
    CHECK(std::ranges::count_if(second.notices, [](const std::string& line) {
              return line.find("different model file") != std::string::npos;
          }) == 1);
    // Nothing of the old file was loaded; the opening was read and kept anew.
    CHECK(second.session()->loaded_states.empty());
    CHECK(second.session()->saved_states.size() == 1);
    CHECK(disk.files().size() == 1);
}

TEST_CASE("a prefix file llama.cpp refuses is removed and said, and the turn succeeds",
          "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    const std::string system = words(600);
    {
        Process first{disk};
        (void)first.ask(opening(system, "question"));
    }
    Process refused{disk, true};
    CHECK(refused.ask(opening(system, "question")) == "answered");
    CHECK(refused.said("a cached prompt prefix for test-model was discarded"));
    CHECK(refused.said("unknown (magic, version)"));
    // Read in full, from the start.
    CHECK(refused.session()->prompt_tokens_decoded() > 600);
}

TEST_CASE("a side request never reads or writes the prompt cache",
          "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    const std::string system = words(600);
    {
        Process first{disk};
        (void)first.ask(opening(system, "question"));
    }
    const std::vector<fs::path> before = disk.files();
    Process side{disk};
    ChatRequest request = opening(system, "name this chat");
    request.transient.side_request = true;
    (void)side.ask(request);
    for (const auto& context : side.runtime->model->contexts) {
        CHECK(context->loaded_states.empty());
        CHECK(context->saved_states.empty());
    }
    CHECK(disk.files() == before);
}

TEST_CASE("a resumed chat starts from its saved state", "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    const std::string history = words(2100, "said");
    const ChatRequest first_turn = opening("be brief", history);
    Process first{disk};
    first.provider->resume_conversation("20260101-120000-abcd");
    const std::string answer = first.ask(first_turn);
    std::vector<std::string> saved;
    first.provider->save_conversation("20260101-120000-abcd", [&saved](const StatusEvent& event) {
        saved.push_back(event.detail);
    });
    REQUIRE(saved.size() == 1);
    CHECK(saved.front().find("saved this chat's state") != std::string::npos);
    const fs::path state = disk.cache / "chats" / "20260101-120000-abcd.state";
    REQUIRE(fs::exists(state));
    // Private: a chat's state is its conversation.
    CHECK((fs::status(state).permissions() & (fs::perms::group_all | fs::perms::others_all)) ==
          fs::perms::none);
    CHECK((fs::status(state.parent_path()).permissions() &
           (fs::perms::group_all | fs::perms::others_all)) == fs::perms::none);

    Process resumed{disk};
    resumed.provider->resume_conversation("20260101-120000-abcd");
    ChatRequest next = first_turn;
    next.messages.push_back(ChatMessage::assistant(answer));
    next.messages.push_back(ChatMessage::user("and then"));
    CHECK(resumed.ask(next) == "answered");
    REQUIRE(resumed.session()->loaded_states.size() == 1);
    CHECK(resumed.session()->prompt_tokens_decoded() < 20);
    CHECK(resumed.cache_lines.front().find("from the saved chat") != std::string::npos);
    // All of it matched, so it stays for the next resume.
    CHECK(resumed.notices.empty());
    CHECK(fs::exists(state));
}

TEST_CASE("a short chat is not saved, and says so", "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    Process first{disk};
    (void)first.ask(opening("be brief", "a short question"));
    std::vector<std::string> said;
    first.provider->save_conversation(
        "short-chat", [&said](const StatusEvent& event) { said.push_back(event.detail); });
    REQUIRE(said.size() == 1);
    CHECK(said.front().find("not saved") != std::string::npos);
    CHECK(said.front().find("2000") != std::string::npos);
    CHECK(disk.files().empty());
}

TEST_CASE("a saved chat made with another model file is discarded, said once",
          "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    const ChatRequest first_turn = opening("be brief", words(2100, "said"));
    {
        Process first{disk};
        (void)first.ask(first_turn);
        first.provider->save_conversation("chat-1", {});
    }
    REQUIRE(fs::exists(disk.cache / "chats" / "chat-1.state"));
    std::ofstream{disk.model, std::ios::binary | std::ios::app} << " retrained";
    Process resumed{disk};
    resumed.provider->resume_conversation("chat-1");
    CHECK(resumed.ask(first_turn) == "answered");
    CHECK(resumed.said("it was made with a different model file"));
    CHECK(resumed.session()->loaded_states.empty());
    CHECK_FALSE(fs::exists(disk.cache / "chats" / "chat-1.state"));
    CHECK_FALSE(fs::exists(disk.cache / "chats" / "chat-1.json"));
}

TEST_CASE("a saved chat that no longer matches its conversation is discarded",
          "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    {
        Process first{disk};
        (void)first.ask(opening("be brief", words(2100, "said")));
        first.provider->save_conversation("chat-2", {});
    }
    Process resumed{disk};
    resumed.provider->resume_conversation("chat-2");
    CHECK(resumed.ask(opening("something else entirely", "hello")) == "answered");
    CHECK(resumed.said("the saved state of this chat matched only"));
    CHECK_FALSE(fs::exists(disk.cache / "chats" / "chat-2.state"));
}

TEST_CASE("the cache stays within its cap, the least recently used going first",
          "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    {
        Process first{disk};
        (void)first.ask(opening(words(600, "one"), "question"));
    }
    REQUIRE(disk.files().size() == 1);
    // A cap of one byte keeps only the file just written.
    // Another opening -- another length, since the fake numbers words in
    // the order it first sees them.
    Process second{disk, false, 1};
    (void)second.ask(opening(words(700, "two"), "question"));
    REQUIRE(second.session()->saved_states.size() == 1);
    const std::vector<fs::path> left = disk.files();
    REQUIRE(left.size() == 1);
    CHECK(second.session()->saved_states.size() == 1);
    // Written under a temporary name, then moved into place.
    CHECK(left.front().filename().string() + ".tmp" ==
          second.session()->saved_states.front().filename().string());
}

TEST_CASE("a sliding-window model keeps no prompt cache, and says so once",
          "[backends][llamacpp][prompt-cache]") {
    // Its restore is not exact (Gemma 4, gpt-oss): correctness first.
    const Disk disk;
    const std::string system = words(600);
    Process first{disk, false, 0, 64};
    (void)first.ask(opening(system, "first"));
    (void)first.ask(opening(system, "second"));
    CHECK(disk.files().empty());
    CHECK(std::ranges::count_if(first.cache_lines, [](const std::string& line) {
              return line.find("does not restore exactly") != std::string::npos;
          }) == 1);
    std::vector<std::string> said;
    first.provider->save_conversation(
        "chat-9", [&said](const StatusEvent& event) { said.push_back(event.detail); });
    REQUIRE(said.size() == 1);
    CHECK(said.front().find("does not restore exactly") != std::string::npos);
    CHECK(disk.files().empty());
}

TEST_CASE("a prefix file that holds other tokens than its name says is discarded",
          "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    const std::string system = words(600);
    {
        Process first{disk};
        (void)first.ask(opening(system, "question"));
    }
    REQUIRE(disk.files().size() == 1);
    std::ofstream{disk.files().front(), std::ios::binary} << "1\n2\n3\n";
    Process second{disk};
    CHECK(second.ask(opening(system, "question")) == "answered");
    CHECK(second.said("was discarded -- it held other tokens"));
    CHECK(second.session()->prompt_tokens_decoded() > 600);
}

TEST_CASE("a chat is saved a few tokens short of its last prompt's end",
          "[backends][llamacpp][prompt-cache]") {
    // Where a thinking model's next prompt is still sure to agree with it.
    const Disk disk;
    Process first{disk};
    const ChatRequest request = opening("be brief", words(2100, "said"));
    const auto prompt = first.provider->stream_chat(request, first.options).usage.prompt_tokens;
    first.provider->save_conversation("chat-3", {});
    const apogee::backends::PromptCache cache{disk.cache};
    const auto record = cache.read_chat_record("chat-3");
    REQUIRE(record.has_value());
    CHECK(record->tokens == prompt - 4);
}

TEST_CASE("a backend entry keeps its prompt cache under cache/prompt",
          "[backends][llamacpp][prompt-cache]") {
    apogee::harness::BackendConfig entry;
    entry.type = apogee::harness::BackendType::LlamaCpp;
    entry.model_path = "/models/test.gguf";
    CHECK(LlamaCppProvider::options_from("local", entry).prompt_cache_dir ==
          apogee::harness::prompt_cache_dir());
    CHECK(apogee::harness::prompt_cache_dir() == apogee::harness::cache_dir() / "prompt");
}

TEST_CASE("a refused prefix file is gone even when it cannot be written again",
          "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    const std::string system = words(600);
    {
        Process first{disk};
        (void)first.ask(opening(system, "question"));
    }
    REQUIRE(disk.files().size() == 1);
    Process refused{disk, true, 0, 0, 0, true};
    CHECK(refused.ask(opening(system, "question")) == "answered");
    CHECK(refused.said("was discarded"));
    CHECK(disk.files().empty());
}

TEST_CASE("a saved chat made with another window is discarded, said once",
          "[backends][llamacpp][prompt-cache]") {
    const Disk disk;
    const ChatRequest first_turn = opening("be brief", words(2100, "said"));
    {
        Process first{disk, false, 0, 0, 8192};
        (void)first.ask(first_turn);
        first.provider->save_conversation("chat-4", {});
    }
    REQUIRE(fs::exists(disk.cache / "chats" / "chat-4.state"));
    Process resumed{disk, false, 0, 0, 16384};
    resumed.provider->resume_conversation("chat-4");
    CHECK(resumed.ask(first_turn) == "answered");
    CHECK(resumed.said("it was made with another cache type or window"));
    CHECK(resumed.session()->loaded_states.empty());
}
