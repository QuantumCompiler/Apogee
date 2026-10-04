#pragma once

#include <atomic>
#include <cstdlib>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <string_view>
#include <vector>

#include "contracts/provider.h"
#include "support/fake_ffmpeg.h"

/// Fakes for media attachments (26e): a provider whose model sees, hears or
/// reads clips as a test says, beside the `ffmpeg` and `ffprobe` fakes
/// (support/fake_ffmpeg.h, which the platform's own suite uses alone).
namespace apogee::testing {

/// A provider whose capabilities and replies a test sets. Thread-safe: the
/// attachment worker calls it off the test's thread.
class MediaProvider final : public harness::LLMProvider,
                            public harness::VisionCapable,
                            public harness::AudioCapable,
                            public harness::VideoCapable {
public:
    bool sees = false;
    bool hears = false;
    bool reads_video = false;
    int rate = 0;
    bool metered = false;
    /// The reply to a request; by default, what it was sent.
    std::function<std::string(const harness::ChatRequest&)> reply;

    [[nodiscard]] std::vector<harness::ChatRequest> requests() const {
        const std::scoped_lock lock{mutex_};
        return requests_;
    }

    [[nodiscard]] std::string_view backend_name() const noexcept override {
        return "media";
    }

    [[nodiscard]] harness::ChatResponse chat(const harness::ChatRequest& request,
                                             const harness::CancellationToken&) override {
        {
            const std::scoped_lock lock{mutex_};
            requests_.push_back(request);
        }
        harness::ChatResponse response;
        response.message = harness::ChatMessage::assistant(
            reply ? reply(request) : std::string{"a picture of " + request.model});
        return response;
    }

    [[nodiscard]] harness::ChatResponse stream_chat(const harness::ChatRequest& request,
                                                    const harness::StreamOptions&) override {
        return chat(request, {});
    }

    [[nodiscard]] std::vector<harness::ModelInfo> list_models(
        const harness::CancellationToken&) override {
        return {};
    }

    [[nodiscard]] bool generation_is_metered() const noexcept override {
        return metered;
    }

    [[nodiscard]] bool accepts_images() const noexcept override {
        return sees;
    }

    [[nodiscard]] bool accepts_audio() const noexcept override {
        return hears;
    }

    [[nodiscard]] int audio_sample_rate() const noexcept override {
        return rate;
    }

    [[nodiscard]] bool accepts_video() const noexcept override {
        return reads_video;
    }

private:
    mutable std::mutex mutex_;
    std::vector<harness::ChatRequest> requests_;
};

/// The kind of a request's first non-text part, for a reply function.
[[nodiscard]] inline bool asks_about_audio(const harness::ChatRequest& request) {
    for (const harness::ChatMessage& message : request.messages) {
        for (const harness::ContentPart& part : message.content.parts()) {
            if (part.kind == harness::ContentPart::Kind::InputAudio) {
                return true;
            }
        }
    }
    return false;
}

}  // namespace apogee::testing
