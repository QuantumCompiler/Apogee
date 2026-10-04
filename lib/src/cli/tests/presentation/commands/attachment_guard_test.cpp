#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <vector>

#include "backends/mock.h"
#include "commands/helpers.h"
#include "contracts/config.h"
#include "harness/harness.h"

/// The attachment guard, asserted across every surface that accepts media.
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
/// and this test asserts every caller reaches it. Since 26e every surface's
/// `--image`, `--attach` and `@path` reach it through `ChatAttachments`, and it
/// asks the helper roles too: a picture a vision model can describe is not
/// refused because the chat model cannot see. The **source scan** below is the
/// part that survives: a surface that grows an `--image` flag without going
/// through the attachment path fails here by existing.
namespace {

using apogee::commands::attachment_refusal;
using apogee::commands::media_refusal_message;
using apogee::harness::BackendConfig;
using apogee::harness::BackendType;
using apogee::harness::Config;
using apogee::harness::Medium;

[[nodiscard]] Config config_with_mock() {
    Config config;
    BackendConfig mock;
    mock.type = BackendType::Mock;
    config.backends["mock"] = mock;
    config.models.default_backend = "mock";
    return config;
}

/// A provider whose model hears audio.
class Hearing final : public apogee::harness::LLMProvider, public apogee::harness::AudioCapable {
public:
    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "ears";
    }

    [[nodiscard]] apogee::harness::ChatResponse chat(
        const apogee::harness::ChatRequest&, const apogee::harness::CancellationToken&) override {
        return {};
    }

    [[nodiscard]] apogee::harness::ChatResponse stream_chat(
        const apogee::harness::ChatRequest&, const apogee::harness::StreamOptions&) override {
        return {};
    }

    [[nodiscard]] std::vector<apogee::harness::ModelInfo> list_models(
        const apogee::harness::CancellationToken&) override {
        return {};
    }

    [[nodiscard]] bool accepts_audio() const noexcept override {
        return true;
    }
};

/// Reads a source file from the tree, for the cross-surface scan.
[[nodiscard]] std::string read_source(std::string_view relative) {
    const std::filesystem::path path =
        std::filesystem::path{APOGEE_SOURCE_DIR} / "presentation" / "commands" / relative;
    std::ifstream in(path, std::ios::binary);
    REQUIRE(in.good());
    return std::string{std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

}  // namespace

TEST_CASE("media a backend can read is not refused", "[commands][attachments]") {
    // The mock implements no VisionCapable, and the Harness's rule is that a
    // provider which does not answer the image probe is permissive -- so an
    // image passes, and a video's frames are described by the chat model.
    const Config config = config_with_mock();
    const apogee::harness::Harness harness{config};
    CHECK(attachment_refusal(harness, "mock", Medium::Image).empty());
    CHECK(attachment_refusal(harness, "mock", Medium::Video).empty());
    // Audio is the opposite: undeclared means no.
    CHECK_FALSE(attachment_refusal(harness, "mock", Medium::Audio).empty());
}

TEST_CASE("a helper role that hears lifts the refusal of audio", "[commands][attachments]") {
    Config config = config_with_mock();
    BackendConfig ears;
    ears.type = BackendType::Mock;
    config.backends["ears"] = ears;
    config.models.default_transcription = "ears";
    apogee::harness::Harness harness{config};
    harness.register_provider("mock", std::make_shared<apogee::backends::MockProvider>(
                                          apogee::backends::MockProvider::Options{}));
    harness.register_provider("ears", std::make_shared<Hearing>());
    harness.use_default_router();
    CHECK(attachment_refusal(harness, "mock", Medium::Audio).empty());
}

TEST_CASE("an unroutable model does not throw from the guard", "[commands][attachments]") {
    // The existing capability rule: a probe on a model that does not route
    // answers "no" rather than raising. A guard that threw here would turn a
    // typo in -m into a crash instead of a message.
    const Config config = config_with_mock();
    const apogee::harness::Harness harness{config};
    for (const Medium medium : {Medium::Image, Medium::Audio, Medium::Video}) {
        CHECK_NOTHROW((void)attachment_refusal(harness, "no-such-backend", medium));
    }
}

TEST_CASE("the refusal names the backend and the helper role that would read it",
          "[commands][attachments]") {
    // Asserted on the message DIRECTLY rather than by waiting for a provider to
    // refuse. In a build without llama.cpp nothing ever answers "no" to images,
    // so the first version of this test guarded its assertions behind a
    // refusal that never arrived and passed while asserting nothing.
    const std::string image = media_refusal_message("gemma-local", Medium::Image);
    CHECK(image.find("gemma-local") != std::string::npos);
    CHECK(image.find("set-default-vision") != std::string::npos);
    CHECK(image.find("mmproj_path") != std::string::npos);
    CHECK(image.find("APOGEE_ENABLE_LLAMA") != std::string::npos);

    const std::string audio = media_refusal_message("gemma-local", Medium::Audio);
    CHECK(audio.find("gemma-local") != std::string::npos);
    CHECK(audio.find("set-default-transcription") != std::string::npos);
    CHECK(audio.find("audio encoder") != std::string::npos);

    const std::string video = media_refusal_message("gemma-local", Medium::Video);
    CHECK(video.find("gemma-local") != std::string::npos);
    CHECK(video.find("set-default-vision") != std::string::npos);
}

TEST_CASE("every surface that takes --image goes through the attachment path",
          "[commands][attachments][parity]") {
    // THE assertion. Written over a LIST of surfaces rather than one test per
    // surface, so adding a third without wiring the guard fails here — which is
    // the failure mode that actually happened, and the one a per-surface test
    // cannot catch because nobody writes the test for the surface they forgot.
    const std::vector<std::string> surfaces{"complete.cpp", "chat.cpp"};

    for (const std::string& surface : surfaces) {
        INFO("surface: " << surface);
        const std::string source = read_source(surface);

        // It accepts images...
        REQUIRE(source.find("--image") != std::string::npos);
        // ...and attaches them, so they meet the one guard...
        CHECK(source.find("ChatAttachments") != std::string::npos);
        CHECK(source.find("load_image_part") == std::string::npos);
        // ...and it has not grown a private copy of the probe: going through
        // the helper is what keeps the message and the rule in one place.
        CHECK(source.find("accepts_images") == std::string::npos);
    }
    // The attachment path asks the shared helper for every medium it attaches.
    CHECK(read_source("chat_attachments.cpp").find("attachment_refusal(") != std::string::npos);
}

TEST_CASE("the guard has exactly one definition", "[commands][attachments][parity]") {
    // The structural half: a second implementation is how the two surfaces
    // drifted the first time. `helpers.cpp` owns it; no command may define it.
    for (const std::string& surface : {"complete.cpp", "chat.cpp", "chat_attachments.cpp"}) {
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
    for (const std::string& surface :
         {"complete.cpp", "chat.cpp", "helpers.cpp", "chat_attachments.cpp"}) {
        INFO("surface: " << surface);
        const std::string source = read_source(surface);
        CHECK(source.find("dynamic_cast") == std::string::npos);
        CHECK(source.find("LlamaCppProvider") == std::string::npos);
    }
}

TEST_CASE("a helper role set to a model that cannot read either is named, not called unset",
          "[commands][attachments]") {
    Config config = config_with_mock();
    BackendConfig ears;
    ears.type = BackendType::Mock;
    config.backends["ears"] = ears;
    config.models.default_transcription = "ears";
    const apogee::harness::Harness harness{config};
    const std::string refusal = attachment_refusal(harness, "mock", Medium::Audio);
    CHECK(refusal.find("neither can the transcription model, 'ears'") != std::string::npos);
    CHECK(refusal.find("no transcription model is set") == std::string::npos);
    CHECK(media_refusal_message("m", Medium::Image, "eyes")
              .find("neither can the vision model, 'eyes' -- point 'apogee config "
                    "set-default-vision' at one that can") != std::string::npos);
}
