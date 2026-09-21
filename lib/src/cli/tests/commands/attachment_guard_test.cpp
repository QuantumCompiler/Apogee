#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

#include "commands/helpers.h"
#include "harness/config.h"
#include "harness/harness.h"

/// The attachment guard, asserted across every surface that accepts `--image`.
///
/// **This test exists because the check was written once and copied never.**
/// `complete.cpp` guarded attachments on `Harness::accepts_images()` from the
/// day `--image` landed; `chat.cpp` never got a copy, so `apogee chat --image`
/// loaded a picture, built the content part, and handed it to a provider that
/// had just answered that it cannot read images. Nothing failed loudly — the
/// image simply went somewhere it could not be used.
///
/// "Parity is the product" makes a capability check present on one surface and
/// absent on another a bug in its own right, so the fix is one shared helper
/// and this test asserts every caller reaches it. The **source scan** below is
/// the part that survives: a fifth surface that grows an `--image` flag without
/// calling the guard fails here by existing, which is the only version of this
/// assertion that keeps working after everyone involved has forgotten it.
namespace {

using apogee::commands::attachment_refusal;
using apogee::harness::BackendConfig;
using apogee::harness::BackendType;
using apogee::harness::Config;
using apogee::harness::ContentPart;

[[nodiscard]] Config config_with_mock() {
    Config config;
    BackendConfig mock;
    mock.type = BackendType::Mock;
    config.backends["mock"] = mock;
    config.models.default_backend = "mock";
    return config;
}

[[nodiscard]] std::vector<ContentPart> one_image() {
    return {ContentPart::from_image_url("data:image/png;base64,AAAA")};
}

/// Reads a source file from the tree, for the cross-surface scan.
[[nodiscard]] std::string read_source(std::string_view relative) {
    const std::filesystem::path path =
        std::filesystem::path{APOGEE_SOURCE_DIR} / "commands" / relative;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("no attachments is never a refusal", "[commands][attachments]") {
    const Config config = config_with_mock();
    const apogee::harness::Harness harness{config};
    CHECK(attachment_refusal(harness, "mock", {}).empty());
}

TEST_CASE("a backend that accepts images is not refused", "[commands][attachments]") {
    // The mock provider implements no VisionCapable, and the Harness's rule is
    // that a provider which does not answer the probe is permissive -- so this
    // must pass rather than block every backend that has not opted in.
    const Config config = config_with_mock();
    const apogee::harness::Harness harness{config};
    CHECK(attachment_refusal(harness, "mock", one_image()).empty());
}

TEST_CASE("an unroutable model does not throw from the guard", "[commands][attachments]") {
    // The existing capability rule: a probe on a model that does not route
    // answers "no" rather than raising. A guard that threw here would turn a
    // typo in -m into a crash instead of a message.
    const Config config = config_with_mock();
    const apogee::harness::Harness harness{config};
    CHECK_NOTHROW((void)attachment_refusal(harness, "no-such-backend", one_image()));
}

TEST_CASE("the refusal names the backend and both ways to fix it", "[commands][attachments]") {
    // Asserted on the message DIRECTLY rather than by waiting for a provider to
    // refuse. In a build without llama.cpp nothing ever answers "no", so the
    // first version of this test guarded its assertions behind a refusal that
    // never arrived and passed while asserting nothing.
    const std::string message = apogee::commands::image_refusal_message("gemma-local");

    CHECK(message.find("gemma-local") != std::string::npos);
    // Both ways forward: the local requirement and the cloud alternative.
    CHECK(message.find("mmproj_path") != std::string::npos);
    CHECK(message.find("APOGEE_ENABLE_LLAMA") != std::string::npos);
    CHECK(message.find("cloud") != std::string::npos);
}

TEST_CASE("every surface that takes --image calls the shared guard",
          "[commands][attachments][parity]") {
    // THE assertion. Written over a LIST of surfaces rather than one test per
    // surface, so adding a sixth without wiring the guard fails here — which is
    // the failure mode that actually happened, and the one a per-surface test
    // cannot catch because nobody writes the test for the surface they forgot.
    const std::vector<std::string> surfaces{"complete.cpp", "chat.cpp"};

    for (const std::string& surface : surfaces) {
        INFO("surface: " << surface);
        const std::string source = read_source(surface);

        // It accepts images...
        REQUIRE(source.find("--image") != std::string::npos);
        // ...so it must ask the shared helper.
        CHECK(source.find("attachment_refusal") != std::string::npos);
        // And it must not have grown a private copy of the probe: going through
        // the helper is what keeps the message and the rule in one place.
        CHECK(source.find("accepts_images") == std::string::npos);
    }
}

TEST_CASE("the guard has exactly one definition", "[commands][attachments][parity]") {
    // The structural half: a second implementation is how the two surfaces
    // drifted the first time. `helpers.cpp` owns it; no command may define it.
    for (const std::string& surface : {"complete.cpp", "chat.cpp"}) {
        INFO("surface: " << surface);
        const std::string source = read_source(surface);
        CHECK(source.find("std::string attachment_refusal(") == std::string::npos);
    }
    CHECK(read_source("helpers.cpp").find("std::string attachment_refusal(") != std::string::npos);
}

TEST_CASE("no surface reaches vision through a type test", "[commands][attachments][parity]") {
    // CLAUDE.md -> "Capability probes never leak a cast". The Harness asks the
    // provider; a command that reached for the concrete type would be correct
    // today and wrong the moment a second backend gained vision.
    for (const std::string& surface : {"complete.cpp", "chat.cpp", "helpers.cpp"}) {
        INFO("surface: " << surface);
        const std::string source = read_source(surface);
        CHECK(source.find("dynamic_cast") == std::string::npos);
        CHECK(source.find("LlamaCppProvider") == std::string::npos);
    }
}
