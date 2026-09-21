#include "models/sha256.h"

#include <catch2/catch_test_macros.hpp>

#include <string>
#include <vector>

/// SHA-256 against published vectors.
///
/// A hand-written hash is only worth having if it is checked against values
/// nobody involved could have invented. Every expectation below was produced by
/// an independent implementation (`shasum -a 256`), and the lengths were chosen
/// to straddle every place the algorithm changes behaviour: the 55/56/57
/// boundary where the length field stops fitting in the final block, and the
/// 63/64/65 boundary where a block is exactly full.
///
/// The first draft of this file had one expectation written from memory. It was
/// wrong, the test failed, and the implementation was correct — which is the
/// argument for taking every vector from a reference rather than from
/// confidence.
namespace {

using apogee::models::Sha256;
using apogee::models::sha256_hex;

struct Vector {
    std::string input;
    std::string expected;
};

}  // namespace

TEST_CASE("known-answer vectors", "[models][sha256]") {
    const std::vector<Vector> vectors{
        {"", "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"},
        {"abc", "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"},
        {"abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq",
         "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1"},
    };

    for (const Vector& test : vectors) {
        INFO("input length: " << test.input.size());
        CHECK(sha256_hex(test.input) == test.expected);
    }
}

TEST_CASE("the padding boundaries", "[models][sha256]") {
    // 55 is the largest input whose length field still fits in the same block;
    // 56 forces a second block; 64 is exactly one block. These are where a
    // hand-written implementation goes wrong, and where it did not.
    const std::vector<std::pair<int, std::string>> vectors{
        {55, "9f4390f8d30c2dd92ec9f095b65e2b9ae9b0a925a5258e241c9f1e910f734318"},
        {56, "b35439a4ac6f0948b6d6f9e3c6af0f5f590ce20f1bde7090ef7970686ec6738a"},
        {57, "f13b2d724659eb3bf47f2dd6af1accc87b81f09f59f2b75e5c0bed6589dfe8c6"},
        {63, "7d3e74a05d7db15bce4ad9ec0658ea98e3f06eeecf16b4c6fff2da457ddc2f34"},
        {64, "ffe054fe7ae0cb6dc65c3af9b61d5209f439851db43d0ba5997337df154668eb"},
        {65, "635361c48bb9eab14198e76ea8ab7f1a41685d6ad62aa9146d301d4f17eb0ae0"},
        {119, "31eba51c313a5c08226adf18d4a359cfdfd8d2e816b13f4af952f7ea6584dcfb"},
        {120, "2f3d335432c70b580af0e8e1b3674a7c020d683aa5f73aaaedfdc55af904c21c"},
    };

    for (const auto& [length, expected] : vectors) {
        INFO("length: " << length);
        CHECK(sha256_hex(std::string(static_cast<std::size_t>(length), 'a')) == expected);
    }
}

TEST_CASE("the long vector", "[models][sha256]") {
    // A million 'a' -- the standard long-message vector, and the one that
    // catches a broken multi-block loop.
    CHECK(sha256_hex(std::string(1000000, 'a')) ==
          "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST_CASE("the digest depends only on the bytes, not on how they arrived", "[models][sha256]") {
    // The property that matters for the real caller: a 71 GB model is hashed in
    // 1 MiB reads, and the result must equal what a one-shot hash would give.
    // A wrong buffer-refill is invisible without this.
    const std::string data = [] {
        std::string value;
        for (int i = 0; i < 5000; ++i) {
            value += "the quick brown fox jumps over the lazy dog ";
        }
        return value;
    }();
    const std::string one_shot = sha256_hex(data);

    for (const std::size_t chunk : {std::size_t{1}, std::size_t{7}, std::size_t{63},
                                    std::size_t{64}, std::size_t{65}, std::size_t{4096}}) {
        INFO("chunk: " << chunk);
        Sha256 hash;
        for (std::size_t offset = 0; offset < data.size(); offset += chunk) {
            hash.update(std::string_view{data}.substr(offset, chunk));
        }
        CHECK(hash.hex_digest() == one_shot);
    }
}

TEST_CASE("an empty update does not disturb the digest", "[models][sha256]") {
    Sha256 hash;
    hash.update("");
    hash.update("abc");
    hash.update("");
    CHECK(hash.hex_digest() == sha256_hex("abc"));
}
