#pragma once

#include <filesystem>
#include <string>

/// The throwaway working directories a generation or a speech run writes
/// into (49b, shared since 50a): `run-<pid>-<random>/` under `cache/video`,
/// claimed by creating it, removed however the run ends, and swept by the
/// next run once the process that made it is gone.
namespace apogee::video {

/// Removes the working directories under `root` whose owner is no longer
/// running: a run killed outright leaves its files, and nothing else would.
void sweep_abandoned(const std::filesystem::path& root);

/// A fresh working directory, `run-<pid>-<random>`, claimed by creating it;
/// empty with `error` set when none could be made.
[[nodiscard]] std::filesystem::path make_work_dir(const std::filesystem::path& root,
                                                  std::string& error);

/// A directory removed however the run ends.
class WorkDir {
public:
    explicit WorkDir(std::filesystem::path dir);
    ~WorkDir();

    WorkDir(const WorkDir&) = delete;
    WorkDir& operator=(const WorkDir&) = delete;
    WorkDir(WorkDir&&) = delete;
    WorkDir& operator=(WorkDir&&) = delete;

private:
    std::filesystem::path dir_;
};

}  // namespace apogee::video
