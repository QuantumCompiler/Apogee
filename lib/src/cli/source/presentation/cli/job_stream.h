#pragma once

#include <nlohmann/json_fwd.hpp>

#include <functional>
#include <memory>
#include <ostream>
#include <string>
#include <string_view>
#include <vector>

#include "cli/command.h"
#include "machine/json_reporter.h"
#include "video/generation.h"
#include "video/voice.h"
#include "views/download_progress.h"

/// A long job's machine face (52b): `models pull`, `video generate`, `video
/// compose` and `voice say` under `--output-format stream-json`.
///
/// **A relay, never a second narrator.** The job's progress is said by the
/// producers that say it today -- the chain's stages, the download's counts,
/// the driver's events -- each asking `JobStream::current()` whether to frame
/// what it has as an event instead of painting it. What the verb prints on
/// stdout for people is relayed too: while a stream is open, every line
/// written to `std::cout` becomes a `notice` event, so no stray byte of prose
/// reaches the stream. stderr is untouched -- diagnostics, as ever.
namespace apogee::commands {

class JobStream {
public:
    /// Opens the stream on stdout with its `job` event. One at a time: a
    /// second while one is open is a programming error.
    explicit JobStream(std::string_view verb);
    ~JobStream();

    JobStream(const JobStream&) = delete;
    JobStream& operator=(const JobStream&) = delete;
    JobStream(JobStream&&) = delete;
    JobStream& operator=(JobStream&&) = delete;

    /// The stream open on this thread, or null.
    [[nodiscard]] static JobStream* current() noexcept;

    /// The writer the events go through.
    [[nodiscard]] JsonReporter& events() noexcept {
        return reporter_;
    }

    /// A set of weights the run stored, by its handle (`<model>/<format>/<id>`),
    /// in the order stored -- each said as a `document` when the run ends.
    void stored(std::string handle);

    [[nodiscard]] const std::vector<std::string>& handles() const noexcept {
        return handles_;
    }

    /// The run failed: an `error` event with `message`, once -- the first
    /// failure said is the one that ended it. Said as the stream closes, so
    /// it is the stream's last event whatever the run says after it fails --
    /// where a chain stopped and the command that resumes it (55c).
    void failed(std::string_view message);

    [[nodiscard]] bool has_failed() const noexcept {
        return failed_;
    }

private:
    class LineRelay;

    std::streambuf* original_;
    std::ostream raw_;
    JsonReporter reporter_;
    std::unique_ptr<LineRelay> relay_;
    std::vector<std::string> handles_;
    bool failed_ = false;
    std::string failure_{};
};

/// A failure said on the open stream too, when one is open (52b): its
/// `error` carries `message`, the words stderr gets. For a refusal raised
/// where no stream is in hand -- a runtime not installed, asked below the
/// verb (M29). Nothing without one.
void tell_job_failure(std::string_view message);

/// A job's body with its failure said (55h's backstop, shared since M29): a
/// `CLI::RuntimeError` -- a refusal said on stderr by code that told no
/// stream -- ends the open stream on `error` `the run failed; stderr says
/// why`, any other exception on its own words, and is thrown on. A failure
/// already told stands: the first said is the one that ended the run.
/// Without a stream open, the body alone.
void run_job_body(const std::function<void()>& body);

/// `stdout_download_options()`, relayed as `bytes` events while a stream is
/// open (52b): what every download and hash of a pull is shown through.
[[nodiscard]] DownloadProgress::Options job_download_options();

/// Adds `--output-format` to a job: `text`, `stream-json`, and `json` when
/// the verb has a document. A verb with no `json` of its own says in its
/// help what its stream ends on, `last` -- a pull's documents, a training
/// run's read (55h).
void add_job_format(CLI::App* command, const std::shared_ptr<JobFormat>& format, bool document,
                    std::string_view last = "each stored set's document last");

/// A clip's driver event as the stream says it: `ready` (with the run's
/// engine since M24), `step`, `frames`, `assembling`, a message as a
/// `notice`. `scene` is 0 outside a composition.
void relay_video_event(JsonReporter& events, const video::VideoEvent& event, std::size_t scene);

/// A voice driver's event as the stream says it: `ready` (with the speech's
/// engine since M24), `segment`, a message as a `notice`.
void relay_voice_event(JsonReporter& events, const video::VoiceEvent& event, std::size_t scene);

}  // namespace apogee::commands
