#pragma once

#include <string>
#include <vector>

#include "machine/json_reporter.h"
#include "training/pipeline.h"

/// `train pipeline run|resume` and `train cycle run` under `--output-format
/// stream-json` (55h): the training track's long jobs, relayed by the
/// producers that narrate them and never narrated a second time -- 52b's law.
///
/// **A pipeline's stages are its manifest's.** Every manifest the run writes
/// is handed here once it is on disk, and each stage whose status that write
/// moved is a `stage` event carrying the manifest's own word -- on the first
/// write every stage, so a driver joining at a resume knows which already
/// passed. The trainer's ticks are `iteration` events, the facts the status
/// line shows; every other line the console prints is a `notice`, in the
/// console's words. A cycle pass's narration is its `pass` events, said
/// where `run_cycle` says each line (`train.cpp`); its pipeline is relayed
/// here like any other.
namespace apogee::commands {

class PipelineRelay {
public:
    explicit PipelineRelay(JsonReporter& events) : events_{events} {}

    /// One manifest the run wrote: a `stage` for each stage it moved.
    void manifest(const training::PipelineRunManifest& manifest);

    /// One progress event: a trainer's tick as `iteration`, a line as a
    /// `notice` -- a trainer's own `[train] `-prefixed, as the console says it.
    void progress(const training::PipelineProgress& progress);

private:
    JsonReporter& events_;
    /// Each stage's status as last said; empty before the first write.
    std::vector<std::string> said_{};
};

}  // namespace apogee::commands
