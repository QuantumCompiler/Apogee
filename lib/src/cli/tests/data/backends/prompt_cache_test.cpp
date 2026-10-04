#include "backends/prompt_cache.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <system_error>
#include <vector>

/// The prompt cache's files (26j): which model they were made with, where
/// each lives, who may read them, and what is evicted first.
namespace {

namespace fs = std::filesystem;
using apogee::backends::ChatCacheRecord;
using apogee::backends::fingerprint_of;
using apogee::backends::ModelFingerprint;
using apogee::backends::PromptCache;
using apogee::harness::KvCacheType;

struct Scratch {
    fs::path root = fs::temp_directory_path() /
                    ("apogee-prompt-cache-unit-" + std::to_string(std::random_device{}()));

    Scratch() {
        fs::create_directories(root);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;
    Scratch(Scratch&&) = delete;
    Scratch& operator=(Scratch&&) = delete;

    ~Scratch() {
        std::error_code code;
        fs::remove_all(root, code);
    }

    void write(const fs::path& path, std::string_view bytes) const {
        fs::create_directories(path.parent_path());
        std::ofstream{path, std::ios::binary} << bytes;
    }
};

[[nodiscard]] bool owner_only(const fs::path& path) {
    return (fs::status(path).permissions() & (fs::perms::group_all | fs::perms::others_all)) ==
           fs::perms::none;
}

}  // namespace

TEST_CASE("a model file's fingerprint changes when the file does", "[prompt-cache]") {
    const Scratch scratch;
    const fs::path model = scratch.root / "m.gguf";
    scratch.write(model, "weights");
    const auto first = fingerprint_of(model);
    REQUIRE(first.has_value());
    CHECK(first->size == 7);
    CHECK(fingerprint_of(model) == first);
    // Replaced: another size.
    scratch.write(model, "other weights");
    CHECK(fingerprint_of(model) != first);
    // Touched: another time.
    const auto resized = fingerprint_of(model);
    fs::last_write_time(model, fs::last_write_time(model) + std::chrono::seconds{5});
    CHECK(fingerprint_of(model) != resized);
    CHECK_FALSE(fingerprint_of(scratch.root / "missing.gguf").has_value());
}

TEST_CASE("a model's prefix files go when its file is replaced, said once", "[prompt-cache]") {
    const Scratch scratch;
    PromptCache cache{scratch.root / "prompt"};
    const ModelFingerprint model{.path = "/m/a.gguf", .size = 10, .modified = 1};
    CHECK_FALSE(cache.claim_model(model, "a").has_value());
    CHECK_FALSE(cache.claim_model(model, "a").has_value());
    const fs::path prefix = cache.prefix_path(model, KvCacheType::Q8_0, 4096, {1, 2, 3});
    scratch.write(prefix, "state");

    const ModelFingerprint replaced{.path = "/m/a.gguf", .size = 11, .modified = 2};
    const auto said = cache.claim_model(replaced, "a");
    REQUIRE(said.has_value());
    CHECK(*said == "the prompt cache for a was made with a different model file; cleared");
    CHECK_FALSE(fs::exists(prefix));
    // Claimed for the new file: not said again.
    CHECK_FALSE(cache.claim_model(replaced, "a").has_value());
    CHECK(owner_only(scratch.root / "prompt"));
}

TEST_CASE("a prefix file is named by its tokens, cache type and window", "[prompt-cache]") {
    const Scratch scratch;
    const PromptCache cache{scratch.root};
    const ModelFingerprint model{.path = "/m/a.gguf", .size = 10, .modified = 1};
    const fs::path base = cache.prefix_path(model, KvCacheType::Q8_0, 4096, {1, 2, 3});
    CHECK(base == cache.prefix_path(model, KvCacheType::Q8_0, 4096, {1, 2, 3}));
    CHECK(base != cache.prefix_path(model, KvCacheType::Q8_0, 4096, {1, 2, 4}));
    CHECK(base != cache.prefix_path(model, KvCacheType::F16, 4096, {1, 2, 3}));
    CHECK(base != cache.prefix_path(model, KvCacheType::Q8_0, 8192, {1, 2, 3}));
    // Another model's live elsewhere.
    const ModelFingerprint other{.path = "/m/b.gguf", .size = 10, .modified = 1};
    CHECK(base.parent_path() !=
          cache.prefix_path(other, KvCacheType::Q8_0, 4096, {1, 2, 3}).parent_path());
}

TEST_CASE("a chat's file is named by its id, and only an id that cannot escape", "[prompt-cache]") {
    const Scratch scratch;
    const PromptCache cache{scratch.root};
    CHECK(cache.chat_path("20260826-120000-abcd") ==
          scratch.root / "chats" / "20260826-120000-abcd.state");
    for (const std::string_view bad : {"", ".", "..", "../escape", "a/b", "a\\b", "x.y"}) {
        INFO(bad);
        CHECK(cache.chat_path(bad).empty());
    }
}

TEST_CASE("a chat's record round-trips, and goes with its state", "[prompt-cache]") {
    const Scratch scratch;
    const PromptCache cache{scratch.root};
    const ChatCacheRecord record{.model = {.path = "/m/a.gguf", .size = 10, .modified = 3},
                                 .cache_type = KvCacheType::F16,
                                 .window = 32768,
                                 .tokens = 2400};
    REQUIRE(cache.write_chat_record("chat", record));
    const auto read = cache.read_chat_record("chat");
    REQUIRE(read.has_value());
    CHECK(read->model == record.model);
    CHECK(read->cache_type == KvCacheType::F16);
    CHECK(read->window == 32768);
    CHECK(read->tokens == 2400);
    CHECK(owner_only(scratch.root / "chats" / "chat.json"));
    scratch.write(cache.chat_path("chat"), "state");
    cache.remove_chat("chat");
    CHECK_FALSE(fs::exists(cache.chat_path("chat")));
    CHECK_FALSE(cache.read_chat_record("chat").has_value());
}

TEST_CASE("a state settles private and in place", "[prompt-cache]") {
    const Scratch scratch;
    const PromptCache cache{scratch.root};
    const fs::path written = scratch.root / "x.state.tmp";
    scratch.write(written, "state");
    fs::permissions(written, fs::perms::all);
    REQUIRE(PromptCache::settle(written, scratch.root / "x.state"));
    CHECK_FALSE(fs::exists(written));
    CHECK(owner_only(scratch.root / "x.state"));
}

TEST_CASE("over its cap, the least recently used state goes first, never the one just kept",
          "[prompt-cache]") {
    const Scratch scratch;
    const PromptCache cache{scratch.root, 12};
    const auto at = fs::file_time_type::clock::now();
    const fs::path oldest = scratch.root / "models" / "m" / "a.state";
    const fs::path middle = scratch.root / "chats" / "b.state";
    const fs::path newest = scratch.root / "models" / "m" / "c.state";
    scratch.write(oldest, "aaaaaa");
    scratch.write(middle, "bbbbbb");
    scratch.write(fs::path{middle}.replace_extension(".json"), "{}");
    scratch.write(newest, "cccccc");
    fs::last_write_time(oldest, at - std::chrono::hours{3});
    fs::last_write_time(middle, at - std::chrono::hours{2});
    fs::last_write_time(newest, at - std::chrono::hours{1});
    // Used just now: the oldest becomes the newest.
    PromptCache::touch(oldest);

    // Room for two: the least recently used goes, and only it.
    CHECK(cache.evict() == 1);
    CHECK(fs::exists(oldest));
    CHECK_FALSE(fs::exists(middle));
    // A chat's record goes with its state.
    CHECK_FALSE(fs::exists(fs::path{middle}.replace_extension(".json")));
    CHECK(fs::exists(newest));
    // Never the file just kept, whatever its age.
    const PromptCache tight{scratch.root, 1};
    CHECK(tight.evict(oldest) == 1);
    CHECK(fs::exists(oldest));
    CHECK_FALSE(fs::exists(newest));
    const auto usage = cache.usage();
    CHECK(usage.files == 1);
    CHECK(usage.bytes == 6);
    // Within the cap, nothing goes.
    CHECK(cache.evict() == 0);
}
