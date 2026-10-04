#pragma once

#include <optional>
#include <string>
#include <string_view>

/// A Hugging Face ref, read as a string (A1): `owner/repo[@rev][:file]`, and
/// the store directory a repository's weights land in.
///
/// Pure string work, apart from any download -- which is why it lives beside
/// the store that names directories with it rather than in `models/source_hf`,
/// which talks to Hugging Face. Moved here verbatim.
namespace apogee::models {

/// A parsed Hugging Face ref.
struct HfRef {
    std::string owner;
    std::string repo;
    /// Empty when the ref named no file, which means "resolve it".
    std::string file;
    /// Empty means the default branch.
    std::string revision;

    [[nodiscard]] std::string repo_id() const {
        return owner + "/" + repo;
    }
};

/// Parses `ref`. Returns nullopt when it is not owner/repo shaped.
[[nodiscard]] std::optional<HfRef> parse_hf_ref(std::string_view ref);

/// The directory a snapshot or a downloaded dataset lands in: `owner--repo`.
[[nodiscard]] std::string repo_directory_name(const HfRef& ref);

}  // namespace apogee::models
