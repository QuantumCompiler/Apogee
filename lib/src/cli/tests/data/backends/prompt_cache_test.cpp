#include "backends/prompt_cache.h"

#include <catch2/catch_test_macros.hpp>

#include <chrono>
#include <filesystem>
#include <fstream>
#include <iterator>
#include <random>
#include <string>
#include <system_error>
#include <vector>

#include "contracts/layout.h"

/// The prompt cache's files (26j): which model they were made with, where
/// each lives, who may read them, and what is evicted first.
namespace {

namespace fs = std::filesystem;
using apogee::backends::AdapterFingerprint;
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
    if (apogee::harness::supports_private_modes()) {
        CHECK(owner_only(scratch.root / "prompt"));
    }
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
    if (apogee::harness::supports_private_modes()) {
        CHECK(owner_only(scratch.root / "chats" / "chat.json"));
    }
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
    if (apogee::harness::supports_private_modes()) {
        CHECK(owner_only(scratch.root / "x.state"));
    }
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

TEST_CASE("an adapter set joins a model's fingerprint, file by file and scale by scale",
          "[prompt-cache][adapters]") {
    // 53a: a context's state under one set is another model's.
    const Scratch scratch;
    const fs::path model = scratch.root / "m.gguf";
    const fs::path lora = scratch.root / "style.gguf";
    scratch.write(model, "weights");
    scratch.write(lora, "delta");
    const std::string pin(64, 'd');
    const apogee::harness::AdapterConfig style{
        .path = lora.string(), .scale = 0.5, .base_sha256 = pin};
    const auto bare = fingerprint_of(model);
    const auto with = fingerprint_of(model, {style});
    REQUIRE(bare.has_value());
    REQUIRE(with.has_value());
    CHECK(bare->adapters.empty());
    REQUIRE(with->adapters.size() == 1);
    CHECK(with->adapters[0] ==
          AdapterFingerprint{.path = fs::absolute(lora).lexically_normal().string(),
                             .size = 5,
                             .modified = with->adapters[0].modified,
                             .base_sha256 = pin,
                             .scale = 0.5});
    // Another set over the same file: the same file, not the same model.
    CHECK(with != bare);
    CHECK(with->same_file(*bare));
    CHECK(fingerprint_of(model, {style}) == with);
    apogee::harness::AdapterConfig scaled = style;
    scaled.scale = 1.0;
    CHECK(fingerprint_of(model, {scaled}) != with);
    // The adapter file replaced: another fingerprint; gone: none at all.
    scratch.write(lora, "delta, retrained");
    CHECK(fingerprint_of(model, {style}) != with);
    fs::remove(lora);
    CHECK_FALSE(fingerprint_of(model, {style}).has_value());
}

TEST_CASE("each adapter set keeps its own prefix directory, the bare base's as it was",
          "[prompt-cache][adapters]") {
    const Scratch scratch;
    PromptCache cache{scratch.root / "prompt"};
    const ModelFingerprint bare{.path = "/m/a.gguf", .size = 10, .modified = 1};
    ModelFingerprint styled = bare;
    styled.adapters = {{.path = "/l/style.gguf", .size = 3, .modified = 2, .scale = 1.0}};
    ModelFingerprint toned = bare;
    toned.adapters = {{.path = "/l/tone.gguf", .size = 3, .modified = 2, .scale = 1.0}};
    const fs::path bare_dir = cache.prefix_path(bare, KvCacheType::Q8_0, 4096, {1}).parent_path();
    const fs::path styled_dir =
        cache.prefix_path(styled, KvCacheType::Q8_0, 4096, {1}).parent_path();
    CHECK(bare_dir != styled_dir);
    CHECK(styled_dir != cache.prefix_path(toned, KvCacheType::Q8_0, 4096, {1}).parent_path());
    // Claimed in turn, none clears another's.
    CHECK_FALSE(cache.claim_model(bare, "a").has_value());
    CHECK_FALSE(cache.claim_model(styled, "a").has_value());
    CHECK_FALSE(cache.claim_model(toned, "a").has_value());
    CHECK_FALSE(cache.claim_model(bare, "a").has_value());
    CHECK_FALSE(cache.claim_model(styled, "a").has_value());
    // An adapter of the set changed in place: its directory is cleared, said
    // as the adapter's change.
    ModelFingerprint retrained = styled;
    retrained.adapters[0].size = 4;
    const auto said = cache.claim_model(retrained, "a");
    REQUIRE(said.has_value());
    CHECK(*said == "the prompt cache for a was made with a different adapter file; cleared");
    // The bare base's record names no adapters: written as before 53a.
    std::ifstream in{bare_dir / "model.json"};
    const std::string record{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
    CHECK(record == R"({"modified":1,"path":"/m/a.gguf","size":10})");
}

TEST_CASE("a chat's record keeps the adapter set it was saved under", "[prompt-cache][adapters]") {
    const Scratch scratch;
    const PromptCache cache{scratch.root};
    ChatCacheRecord record{.model = {.path = "/m/a.gguf", .size = 10, .modified = 3},
                           .cache_type = KvCacheType::Q8_0,
                           .window = 4096,
                           .tokens = 2400};
    record.model.adapters = {{.path = "/l/style.gguf",
                              .size = 5,
                              .modified = 9,
                              .base_sha256 = std::string(64, 'e'),
                              .scale = 0.75}};
    REQUIRE(cache.write_chat_record("chat", record));
    const auto read = cache.read_chat_record("chat");
    REQUIRE(read.has_value());
    CHECK(read->model == record.model);
}
