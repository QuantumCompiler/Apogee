#include "agentloop/media.h"

#include <catch2/catch_test_macros.hpp>

#include <filesystem>
#include <fstream>
#include <memory>
#include <random>
#include <string>
#include <vector>

#include "backends/mock.h"
#include "contracts/config.h"
#include "harness/harness.h"
#include "platform/child_process.h"
#include "support/env_guard.h"
#include "support/media_fakes.h"

using apogee::agentloop::clock_time;
using apogee::agentloop::describe_request;
using apogee::agentloop::media_readers;
using apogee::agentloop::MediaJob;
using apogee::agentloop::medium_of;
using apogee::agentloop::moments_in;
using apogee::agentloop::native_parts;
using apogee::agentloop::read_media;
using apogee::agentloop::time_span;
using apogee::agentloop::transcribe_request;
using apogee::harness::ContentPart;
using apogee::harness::Medium;
using apogee::testing::FakeFfmpeg;
using apogee::testing::MediaProvider;

namespace {

/// A harness with `chat`, `eyes` and `ears` backends, each a MediaProvider,
/// and the roles `yaml` sets.
struct Models {
    std::shared_ptr<MediaProvider> chat = std::make_shared<MediaProvider>();
    std::shared_ptr<MediaProvider> eyes = std::make_shared<MediaProvider>();
    std::shared_ptr<MediaProvider> ears = std::make_shared<MediaProvider>();
    std::unique_ptr<apogee::harness::Harness> harness;

    explicit Models(const std::string& roles = "") {
        const apogee::harness::Config config = apogee::harness::parse_config(
            "models:\n  default: chat\n" + roles +
                "backends:\n  chat:\n    type: mock\n  eyes:\n    type: mock\n  ears:\n    type: "
                "mock\n",
            "<test>");
        harness = std::make_unique<apogee::harness::Harness>(config);
        harness->register_provider("chat", chat);
        harness->register_provider("eyes", eyes);
        harness->register_provider("ears", ears);
        harness->use_default_router();
    }
};

struct Scratch {
    apogee::testing::TempDir dir{"media-" + std::to_string(std::random_device{}())};

    [[nodiscard]] std::filesystem::path write(const std::string& name,
                                              const std::string& bytes = "bytes") const {
        const std::filesystem::path path = dir.path() / name;
        std::ofstream{path, std::ios::binary} << bytes;
        return path;
    }
};

[[nodiscard]] MediaJob job_for(const Models& models, const Scratch& scratch, Medium medium) {
    MediaJob job;
    job.harness = models.harness.get();
    job.readers = media_readers(*models.harness, "chat", medium);
    job.scratch = scratch.dir.path() / "work";
    return job;
}

[[nodiscard]] std::string first_text(const apogee::harness::ChatRequest& request) {
    for (const ContentPart& part : request.messages.front().content.parts()) {
        if (part.kind == ContentPart::Kind::Text) {
            return part.text;
        }
    }
    return {};
}

}  // namespace

TEST_CASE("a medium is known by its extension", "[agentloop][media]") {
    CHECK(medium_of("shot.PNG") == Medium::Image);
    CHECK(medium_of("photo.heic") == Medium::Image);
    CHECK(medium_of("note.m4a") == Medium::Audio);
    CHECK(medium_of("talk.wav") == Medium::Audio);
    CHECK(medium_of("demo.mov") == Medium::Video);
    CHECK(medium_of("screen.webm") == Medium::Video);
    CHECK_FALSE(medium_of("report.pdf").has_value());
    CHECK_FALSE(medium_of("png").has_value());
}

TEST_CASE("clock times are written and read the way people say them", "[agentloop][media]") {
    CHECK(clock_time(0) == "0:00");
    CHECK(clock_time(5.9) == "0:05");
    CHECK(clock_time(270) == "4:30");
    CHECK(clock_time(3870) == "1:04:30");

    CHECK(moments_in("what did they type at 4:30?") == std::vector<double>{270});
    CHECK(moments_in("between 0:05 and 1:04:30") == std::vector<double>{5, 3870});
    // One time, not two; and no time at all.
    CHECK(moments_in("the 14:30 meeting") == std::vector<double>{870});
    CHECK(moments_in("4:75 is not a time, nor 3:2, nor 12:345").empty());
    CHECK(moments_in("no time here").empty());
}

TEST_CASE("a span of a timeline is dated by the stamps its lines open with", "[agentloop][media]") {
    const std::string text =
        "(A video, 1:00 long.)\n"
        "[0:00] screen: a title\n"
        "[0:00–0:30] said: hello there\n"
        "[0:05] screen: an inbox\n"
        "[0:30–1:00] said: and the budget\n";
    const std::optional<apogee::agentloop::TimeSpan> whole = time_span(text, 0, text.size());
    REQUIRE(whole.has_value());
    CHECK(whole->start == 0.0);
    CHECK(whole->end == 60.0);
    // From inside the inbox line: dated from that line's own stamp.
    const std::size_t inbox = text.find("an inbox");
    const std::optional<apogee::agentloop::TimeSpan> tail = time_span(text, inbox, text.size());
    REQUIRE(tail.has_value());
    CHECK(tail->start == 5.0);
    CHECK(tail->end == 60.0);
    CHECK_FALSE(time_span("no stamps here\nat all\n", 0, 20).has_value());
}

TEST_CASE("who reads a medium: the chat model natively, a helper role for its text",
          "[agentloop][media]") {
    SECTION("nothing set and a chat model that reads nothing: no one") {
        const Models models;
        for (const Medium medium : {Medium::Image, Medium::Audio, Medium::Video}) {
            const auto readers = media_readers(*models.harness, "chat", medium);
            CHECK_FALSE(readers.native);
            CHECK(readers.describer.empty());
            CHECK(readers.transcriber.empty());
        }
    }
    SECTION("a chat model that sees describes its own images, and reads them natively") {
        Models models;
        models.chat->sees = true;
        const auto image = media_readers(*models.harness, "chat", Medium::Image);
        CHECK(image.native);
        CHECK(image.describer == "chat");
        // A clip natively only when it says it reads video.
        CHECK_FALSE(media_readers(*models.harness, "chat", Medium::Video).native);
        models.chat->reads_video = true;
        CHECK(media_readers(*models.harness, "chat", Medium::Video).native);
    }
    SECTION("a vision role that sees describes for a chat model that cannot") {
        Models models{"  default_vision: eyes\n"};
        models.eyes->sees = true;
        const auto image = media_readers(*models.harness, "chat", Medium::Image);
        CHECK_FALSE(image.native);
        CHECK(image.describer == "eyes");
        const auto video = media_readers(*models.harness, "chat", Medium::Video);
        CHECK(video.describer == "eyes");
        CHECK(video.transcriber.empty());
    }
    SECTION("a vision role that cannot see gives way to a chat model that can") {
        Models models{"  default_vision: eyes\n"};
        models.chat->sees = true;
        CHECK(media_readers(*models.harness, "chat", Medium::Image).describer == "chat");
    }
    SECTION("a transcription role that hears transcribes audio and a video's sound") {
        Models models{"  default_transcription: ears\n"};
        models.ears->hears = true;
        CHECK(media_readers(*models.harness, "chat", Medium::Audio).transcriber == "ears");
        CHECK(media_readers(*models.harness, "chat", Medium::Video).transcriber == "ears");
        CHECK(media_readers(*models.harness, "chat", Medium::Image).transcriber.empty());
    }
    SECTION("a role naming no configured backend is passed over") {
        Models models{"  default_vision: nowhere\n"};
        models.chat->sees = true;
        CHECK(media_readers(*models.harness, "chat", Medium::Image).describer == "chat");
    }
}

TEST_CASE("the requests that describe and transcribe", "[agentloop][media]") {
    const auto described = describe_request("eyes", "data:image/png;base64,AAAA", {});
    REQUIRE(described.messages.size() == 1);
    const auto& parts = described.messages.front().content.parts();
    REQUIRE(parts.size() == 2);
    // The picture before the words about it.
    CHECK(parts[0].kind == ContentPart::Kind::ImageUrl);
    CHECK(parts[1].text.starts_with("This is an image. Describe it"));
    CHECK(parts[1].text.find("copy every piece of text") != std::string::npos);
    CHECK(described.transient.side_request);
    CHECK(described.thinking.off());
    CHECK(described.max_tokens == 1024);

    // A frame says what it is first: after the prompt, the line read as a cue
    // to copy the text alone.
    const auto frame =
        describe_request("eyes", "data:image/jpeg;base64,AAAA", "one frame of a video, at 4:30");
    CHECK(frame.messages.front().content.parts()[1].text.starts_with(
        "This is one frame of a video, at 4:30. Describe it"));
    CHECK(frame.max_tokens == 384);

    const auto heard = transcribe_request("ears", "RIFFwav");
    const auto& sound = heard.messages.front().content.parts();
    REQUIRE(sound.size() == 2);
    CHECK(sound[0].kind == ContentPart::Kind::InputAudio);
    CHECK(sound[0].audio_format == "wav");
    CHECK(sound[0].audio_data == apogee::agentloop::base64_encode("RIFFwav"));
    CHECK(heard.transient.side_request);
}

TEST_CASE("an image is described into its text form", "[agentloop][media]") {
    const Scratch scratch;
    Models models{"  default_vision: eyes\n"};
    models.eyes->sees = true;
    models.eyes->reply = [](const apogee::harness::ChatRequest&) {
        return std::string{"  A red screen. It says: STOP  \n"};
    };
    const auto read = read_media(scratch.write("stop.png"), Medium::Image,
                                 job_for(models, scratch, Medium::Image));
    REQUIRE(read.reason.empty());
    CHECK(read.reader == "vision: eyes");
    CHECK(read.text.starts_with("(An image."));
    CHECK(read.text.find("A red screen. It says: STOP\n") != std::string::npos);
    const auto requests = models.eyes->requests();
    REQUIRE(requests.size() == 1);
    CHECK(requests.front().model == "eyes");
    CHECK(requests.front().messages.front().content.parts().front().image_url.starts_with(
        "data:image/png;base64,"));
}

TEST_CASE("an empty answer with the reasoning skipped is asked again with it on",
          "[agentloop][media]") {
    // Gemma 4 answers nothing about audio with its thinking switched off.
    const Scratch scratch;
    Models models{"  default_vision: eyes\n"};
    models.eyes->sees = true;
    models.eyes->reply = [](const apogee::harness::ChatRequest& request) {
        return request.thinking.off() ? std::string{} : std::string{"a dog"};
    };
    const auto read = read_media(scratch.write("dog.jpg"), Medium::Image,
                                 job_for(models, scratch, Medium::Image));
    CHECK(read.reason.empty());
    CHECK(read.text.find("a dog") != std::string::npos);
    const auto requests = models.eyes->requests();
    REQUIRE(requests.size() == 2);
    CHECK(requests[0].thinking.off());
    CHECK_FALSE(requests[1].thinking.off());
    CHECK(requests[1].max_tokens >= 4096);

    // Nothing at all, either way: said, never indexed as a description.
    models.eyes->reply = [](const apogee::harness::ChatRequest&) { return std::string{}; };
    const auto none = read_media(scratch.write("blank.jpg"), Medium::Image,
                                 job_for(models, scratch, Medium::Image));
    CHECK(none.reason == "eyes described nothing");
}

TEST_CASE("audio is transcribed in windows, at the model's own rate", "[agentloop][media]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const FakeFfmpeg ffmpeg{"audio", "75"};
    const Scratch scratch;
    Models models{"  default_transcription: ears\n"};
    models.ears->hears = true;
    models.ears->rate = 24000;
    int window = 0;
    models.ears->reply = [&window](const apogee::harness::ChatRequest&) {
        return "words " + std::to_string(++window);
    };
    const auto read = read_media(scratch.write("note.m4a"), Medium::Audio,
                                 job_for(models, scratch, Medium::Audio));
    REQUIRE(read.reason.empty());
    CHECK(read.reader == "transcription: ears");
    CHECK(read.duration == 75.0);
    CHECK(read.text.find("(Audio, 1:15 long") == 0);
    CHECK(read.text.find("[0:00–0:30] said: words 1\n") != std::string::npos);
    CHECK(read.text.find("[0:30–1:00] said: words 2\n") != std::string::npos);
    CHECK(read.text.find("[1:00–1:15] said: words 3\n") != std::string::npos);
    // Decoded at the rate the model hears at.
    const std::string logged = ffmpeg.logged();
    CHECK(logged.find("-ar 24000") != std::string::npos);
    CHECK(logged.find("-ar 16000") == std::string::npos);
    CHECK(logged.find("-ss 30.000 -t 30.000") != std::string::npos);
    // The audio reached the model as a WAV file.
    const auto requests = models.ears->requests();
    REQUIRE(requests.size() == 3);
    const ContentPart& sound = requests.front().messages.front().content.parts().front();
    CHECK(sound.kind == ContentPart::Kind::InputAudio);
    CHECK(sound.audio_data.starts_with(apogee::agentloop::base64_encode("RIFF").substr(0, 4)));
}

TEST_CASE("a model that cannot say its rate is sent 16 kHz", "[agentloop][media]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const FakeFfmpeg ffmpeg{"audio", "10"};
    const Scratch scratch;
    Models models{"  default_transcription: ears\n"};
    models.ears->hears = true;
    (void)read_media(scratch.write("note.wav"), Medium::Audio,
                     job_for(models, scratch, Medium::Audio));
    CHECK(ffmpeg.logged().find("-ar 16000") != std::string::npos);
}

TEST_CASE("a recording no window of which could be transcribed is not indexed as silence",
          "[agentloop][media]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const FakeFfmpeg ffmpeg{"audio", "40"};
    ffmpeg.fail();
    const Scratch scratch;
    Models models{"  default_transcription: ears\n"};
    models.ears->hears = true;
    const auto read = read_media(scratch.write("note.mp3"), Medium::Audio,
                                 job_for(models, scratch, Medium::Audio));
    CHECK(read.text.empty());
    CHECK(read.reason.starts_with("transcribing it failed"));
    CHECK(read.reason.find("Invalid data found") != std::string::npos);
}

TEST_CASE("a video becomes a timeline: frames and speech in time order", "[agentloop][media]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const FakeFfmpeg ffmpeg{"video audio", "40", "0 5 7.5 30"};
    const Scratch scratch;
    Models models{"  default_vision: eyes\n  default_transcription: ears\n"};
    models.eyes->sees = true;
    models.ears->hears = true;
    models.eyes->reply = [](const apogee::harness::ChatRequest& request) {
        // Each frame is named by its time in the request.
        const std::string text = first_text(request);
        const std::size_t at = text.find("at ");
        return "frame " + text.substr(at + 3, text.find('.', at) - at - 3) + "\nshown";
    };
    models.ears->reply = [](const apogee::harness::ChatRequest&) { return std::string{"hello"}; };
    const auto read = read_media(scratch.write("talk.mp4"), Medium::Video,
                                 job_for(models, scratch, Medium::Video));
    REQUIRE(read.reason.empty());
    CHECK(read.reader == "timeline: eyes and ears");
    const std::string expected =
        "[0:00–0:30] said: hello\n"
        "[0:00] screen: frame 0:00 shown\n"
        "[0:05] screen: frame 0:05 shown\n"
        "[0:07] screen: frame 0:07 shown\n"
        "[0:30–0:40] said: hello\n"
        "[0:30] screen: frame 0:30 shown\n";
    CHECK(read.text.find(expected) != std::string::npos);
    CHECK(read.text.starts_with("(A video, 0:40 long"));
    // Its frames, every five seconds and at each scene change, scaled down.
    const std::string logged = ffmpeg.logged();
    CHECK(logged.find("gte(t-prev_selected_t,5.000)+gt(scene,0.300)") != std::string::npos);
    CHECK(logged.find("min(1280,iw)") != std::string::npos);
    // The private scratch folder is gone once it is read.
    CHECK_FALSE(std::filesystem::exists(scratch.dir.path() / "work"));
}

TEST_CASE("a video's sound left untranscribed is said, naming the role that would",
          "[agentloop][media]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const FakeFfmpeg ffmpeg{"video audio", "12", "0 5"};
    const Scratch scratch;
    Models models{"  default_vision: eyes\n"};
    models.eyes->sees = true;
    const auto read = read_media(scratch.write("talk.mov"), Medium::Video,
                                 job_for(models, scratch, Medium::Video));
    REQUIRE(read.reason.empty());
    CHECK(read.reader == "timeline: eyes");
    REQUIRE(read.notes.size() == 1);
    CHECK(read.notes.front().find("set-default-transcription") != std::string::npos);
}

TEST_CASE("a timeline describes at most 240 frames, thinned evenly", "[agentloop][media]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    std::string times;
    for (int at = 0; at < 300; ++at) {
        times += std::to_string(at * 2) + " ";
    }
    const FakeFfmpeg ffmpeg{"video", "600", times};
    const Scratch scratch;
    Models models{"  default_vision: eyes\n"};
    models.eyes->sees = true;
    const auto read = read_media(scratch.write("long.mp4"), Medium::Video,
                                 job_for(models, scratch, Medium::Video));
    REQUIRE(read.reason.empty());
    CHECK(models.eyes->requests().size() == apogee::agentloop::kMaxTimelineFrames);
    // The first frame kept, and the end of the video still reached.
    CHECK(read.text.find("[0:00] screen:") != std::string::npos);
    CHECK(read.text.find("[9:5") != std::string::npos);
}

TEST_CASE("without ffmpeg, audio and video are refused by its name", "[agentloop][media]") {
    const Scratch scratch;
    const apogee::testing::EnvGuard path{"PATH", (scratch.dir.path() / "empty").string()};
    Models models{"  default_transcription: ears\n  default_vision: eyes\n"};
    models.ears->hears = true;
    models.eyes->sees = true;
    const auto audio = read_media(scratch.write("note.mp3"), Medium::Audio,
                                  job_for(models, scratch, Medium::Audio));
    CHECK(audio.reason.find("need ffmpeg") != std::string::npos);
    const auto video = read_media(scratch.write("talk.mp4"), Medium::Video,
                                  job_for(models, scratch, Medium::Video));
    CHECK(video.reason.find("need ffmpeg") != std::string::npos);
    // An image the vendors take needs nothing external.
    const auto image = read_media(scratch.write("shot.png"), Medium::Image,
                                  job_for(models, scratch, Medium::Image));
    CHECK(image.reason.empty());
}

TEST_CASE("what the chat model reads as it is: the image, the sound, a clip's frames",
          "[agentloop][media]") {
    const Scratch scratch;
    std::string error;
    const auto image = native_parts(scratch.write("shot.png", "PNGBYTES"), Medium::Image, 0, false,
                                    scratch.dir.path() / "n", {}, error);
    REQUIRE(image.size() == 1);
    CHECK(image.front().image_url ==
          "data:image/png;base64," + apogee::agentloop::base64_encode("PNGBYTES"));
    CHECK_FALSE(image.front().video_frame);

    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    const FakeFfmpeg ffmpeg{"video audio", "12", "0 1 2 3 4 5 6"};
    const auto clip = native_parts(scratch.write("clip.mp4"), Medium::Video, 24000, true,
                                   scratch.dir.path() / "n", {}, error);
    REQUIRE(error.empty());
    std::vector<std::string> shape;
    for (const ContentPart& part : clip) {
        shape.push_back(part.kind == ContentPart::Kind::Text         ? part.text
                        : part.kind == ContentPart::Kind::InputAudio ? "<audio>"
                        : part.video_frame                           ? "<frame>"
                                                                     : "<image>");
    }
    const std::vector<std::string> expected{
        "(A video clip, 0:07 long, as its frames, one a second.)\n",
        "[0:00]",
        "<frame>",
        "<frame>",
        "<frame>",
        "<frame>",
        "<frame>",
        "[0:05]",
        "<frame>",
        "<frame>",
        "\n",
        "<audio>"};
    CHECK(shape == expected);
    // One a second, at most a minute of them, small.
    const std::string logged = ffmpeg.logged();
    const std::size_t frames_at = logged.find("fps=1.000");
    REQUIRE(frames_at != std::string::npos);
    const std::size_t line_start = logged.rfind('\n', frames_at);
    const std::string frames_line = logged.substr(
        line_start == std::string::npos ? 0 : line_start + 1,
        logged.find('\n', frames_at) - (line_start == std::string::npos ? 0 : line_start + 1));
    CHECK(frames_line.find("-t 60.000") != std::string::npos);
    CHECK(frames_line.find("min(640,iw)") != std::string::npos);
    CHECK(logged.find("-ar 24000") != std::string::npos);
}

TEST_CASE("a frame the same as one already described keeps its line, undescribed again",
          "[agentloop][media]") {
    if (!apogee::platform::supports_child_processes()) {
        SKIP("no child processes on this platform");
    }
    // The fake writes `JPEG<time>` into each frame: 5 twice is one picture.
    const FakeFfmpeg ffmpeg{"video", "20", "0 5 5 10"};
    const Scratch scratch;
    Models models{"  default_vision: eyes\n"};
    models.eyes->sees = true;
    const auto read = read_media(scratch.write("slides.mp4"), Medium::Video,
                                 job_for(models, scratch, Medium::Video));
    REQUIRE(read.reason.empty());
    CHECK(models.eyes->requests().size() == 3);
    std::size_t lines = 0;
    for (std::size_t at = read.text.find("] screen: "); at != std::string::npos;
         at = read.text.find("] screen: ", at + 1)) {
        ++lines;
    }
    CHECK(lines == 4);
}

TEST_CASE("a provider that declares nothing is read for images, never for audio or video",
          "[agentloop][media]") {
    // Undeclared images are assumed (every cloud vendor reads them); undeclared
    // audio and video are not -- nothing is sent what it has not said it reads.
    const apogee::harness::Config config =
        apogee::harness::parse_config("backends:\n  plain:\n    type: mock\n", "<test>");
    apogee::harness::Harness harness{config};
    harness.register_provider("plain", std::make_shared<apogee::backends::MockProvider>(
                                           apogee::backends::MockProvider::Options{}));
    harness.use_default_router();
    CHECK(harness.can_read("plain", Medium::Image));
    CHECK_FALSE(harness.can_read("plain", Medium::Audio));
    CHECK_FALSE(harness.can_read("plain", Medium::Video));
    CHECK_FALSE(harness.accepts_video("plain"));
    CHECK(harness.audio_sample_rate("plain") == 0);

    // A provider that says a rate but cannot hear is never asked it.
    Models models;
    models.ears->rate = 24000;
    CHECK(models.harness->audio_sample_rate("ears") == 0);
    models.ears->hears = true;
    CHECK(models.harness->audio_sample_rate("ears") == 24000);
}
