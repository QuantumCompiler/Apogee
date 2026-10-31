#include "cli/job_stream.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <exception>
#include <iostream>
#include <streambuf>
#include <utility>

namespace apogee::commands {
namespace {

thread_local JobStream* open_stream = nullptr;

}  // namespace

/// stdout's prose, line by line, as `notice` events: what a verb prints for
/// people while its stream is open reaches the driver as words it can show,
/// and never as bytes that break the stream. Blank lines are spacing, and
/// dropped.
class JobStream::LineRelay final : public std::streambuf {
public:
    explicit LineRelay(JsonReporter& events) : events_{events} {}

    LineRelay(const LineRelay&) = delete;
    LineRelay& operator=(const LineRelay&) = delete;
    LineRelay(LineRelay&&) = delete;
    LineRelay& operator=(LineRelay&&) = delete;

    ~LineRelay() override {
        flush_line();
    }

protected:
    int_type overflow(int_type c) override {
        if (traits_type::eq_int_type(c, traits_type::eof())) {
            return traits_type::not_eof(c);
        }
        const char ch = traits_type::to_char_type(c);
        if (ch == '\n') {
            flush_line();
        } else if (ch != '\r') {
            line_ += ch;
        }
        return c;
    }

    std::streamsize xsputn(const char* text, std::streamsize count) override {
        for (std::streamsize i = 0; i < count; ++i) {
            (void)overflow(traits_type::to_int_type(text[i]));
        }
        return count;
    }

private:
    void flush_line() {
        if (line_.find_first_not_of(" \t") != std::string::npos) {
            events_.on_notice(line_);
        }
        line_.clear();
    }

    JsonReporter& events_;
    std::string line_;
};

JobStream::JobStream(std::string_view verb)
    : original_{std::cout.rdbuf()}, raw_{original_}, reporter_{raw_} {
    reporter_.begin_job(verb);
    relay_ = std::make_unique<LineRelay>(reporter_);
    std::cout.flush();
    std::cout.rdbuf(relay_.get());
    open_stream = this;
}

JobStream::~JobStream() {
    open_stream = nullptr;
    std::cout.flush();
    std::cout.rdbuf(original_);
    relay_.reset();  // a last unterminated line, said
    if (failed_) {
        reporter_.emit_error(failure_);
    }
    raw_.flush();
}

JobStream* JobStream::current() noexcept {
    return open_stream;
}

void JobStream::stored(std::string handle) {
    handles_.push_back(std::move(handle));
}

void JobStream::failed(std::string_view message) {
    if (failed_) {
        return;
    }
    failed_ = true;
    failure_ = std::string{message};
}

void tell_job_failure(std::string_view message) {
    if (JobStream* job = JobStream::current(); job != nullptr) {
        job->failed(message);
    }
}

void run_job_body(const std::function<void()>& body) {
    try {
        body();
    } catch (const CLI::RuntimeError&) {
        tell_job_failure("the run failed; stderr says why");
        throw;
    } catch (const std::exception& e) {
        tell_job_failure(e.what());
        throw;
    }
}

DownloadProgress::Options job_download_options() {
    DownloadProgress::Options options = stdout_download_options();
    if (JobStream* job = JobStream::current(); job != nullptr) {
        options.live = false;
        options.relay = [job](const ByteReport& report) {
            job->events().emit_bytes(report.done, report.total, report.file, report.index,
                                     report.count);
        };
    }
    return options;
}

void add_job_format(CLI::App* command, const std::shared_ptr<JobFormat>& format, bool document,
                    std::string_view last) {
    command
        ->add_option_function<std::string>(
            "--output-format",
            [format, document](const std::string& value) {
                const std::optional<JobFormat> parsed = job_format_from_string(value);
                if (!parsed.has_value() || (*parsed == JobFormat::Json && !document)) {
                    throw CLI::ValidationError("--output-format",
                                               document ? "expected 'text', 'json' or 'stream-json'"
                                                        : "expected 'text' or 'stream-json'");
                }
                *format = *parsed;
            },
            document ? std::string{"Output format: text (default), json -- one JSON document of "
                                   "the same facts -- or stream-json: the progress as events, one "
                                   "JSON object a line, that document last"}
                     : "Output format: text (default) or stream-json -- the progress as events, "
                       "one JSON object a line, " +
                           std::string{last})
        ->type_name(words_value(job_format_names(document)));
}

void relay_video_event(JsonReporter& events, const video::VideoEvent& event, std::size_t scene) {
    using Kind = video::VideoEvent::Kind;
    switch (event.kind) {
        case Kind::Message:
            events.on_notice(event.text);
            return;
        case Kind::Ready:
            events.emit_ready(event.ready.device, event.ready.dtype, event.ready.mode, scene,
                              event.ready.engine);
            return;
        case Kind::Step:
            events.emit_step(event.step, event.steps, scene);
            return;
        case Kind::Done:
            events.emit_frames(event.frames, event.width, event.height, scene);
            return;
        case Kind::Assembling:
            events.emit_assembling(event.frames, scene);
            return;
    }
}

void relay_voice_event(JsonReporter& events, const video::VoiceEvent& event, std::size_t scene) {
    using Kind = video::VoiceEvent::Kind;
    switch (event.kind) {
        case Kind::Message:
            events.on_notice(event.text);
            return;
        case Kind::Ready:
            events.emit_ready(event.ready.device, {}, {}, scene, event.ready.engine);
            return;
        case Kind::Segment:
            events.emit_segment(event.index, event.of, event.seconds, scene);
            return;
        case Kind::Done:
            return;
    }
}

}  // namespace apogee::commands
