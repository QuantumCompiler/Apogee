#pragma once

#include <nlohmann/json_fwd.hpp>

#include <string>
#include <string_view>
#include <vector>

#include "forge/cli_forge.h"

/// GitLab through a spawned `glab` (48a): every verb one `glab api` child,
/// authenticated by `glab`'s own login. The project is its path, encoded
/// (`group%2Fproject`).
///
///   metadata  glab api projects/<project>/merge_requests/<iid>
///   diff      glab api --paginate projects/…/merge_requests/<iid>/diffs?per_page=100
///             -- each file's hunks under its own `diff --git` header, one
///             unified diff
///   comment   glab api --method POST -H "Content-Type: application/json"
///                      projects/…/merge_requests/<iid>/discussions --input -
///   batch     each comment (and the summary) a draft note, then
///             …/draft_notes/bulk_publish -- GitLab's native batch, one
///             notification
///   login     glab api user
///   comments  glab api --paginate …/merge_requests/<iid>/notes?per_page=100,
///             system notes left out
///   listing   glab api --paginate
///   "projects/<p>/merge_requests?state=opened&order_by=updated_at&sort=asc&per_page=100[&updated_after=<t>]"
///   owner     glab api --paginate
///   "groups/<g>/projects?include_subgroups=true&archived=false&per_page=100",
///             else users/<owner>/projects
///   compare   glab api projects/<project>/repository/compare?from=<a>&to=<b>,
///             its `diffs` joined as the merge request's are
///
/// A self-hosted host adds `--hostname <host>` after `api`.
namespace apogee::forge {

class GlabTransport final : public CliForgeClient {
public:
    explicit GlabTransport(CliTransportOptions options = {});

    [[nodiscard]] Forge forge() const noexcept override {
        return Forge::GitLab;
    }

    [[nodiscard]] MergeRequest merge_request(const MergeRequestRef& ref) override;
    [[nodiscard]] std::string diff(const MergeRequestRef& ref) override;
    [[nodiscard]] std::string current_user(const MergeRequestRef& ref) override;
    [[nodiscard]] std::vector<PostedComment> comments(const MergeRequestRef& ref) override;
    [[nodiscard]] std::string compare(const MergeRequestRef& ref, std::string_view from,
                                      std::string_view to) override;
    [[nodiscard]] std::vector<MergeRequestListing> open_merge_requests(
        const ForgeProject& project, std::string_view updated_after) override;
    [[nodiscard]] std::vector<std::string> owner_projects(const ForgeProject& owner) override;
    void create_comment(const MergeRequest& request, const InlineComment& comment) override;
    [[nodiscard]] PublishOutcome publish_batch(const MergeRequest& request,
                                               const std::vector<InlineComment>& comments,
                                               std::string_view summary) override;

private:
    [[nodiscard]] bool logged_out(const CliResult& result) const override;
    [[nodiscard]] bool not_found(const CliResult& result) const override;
};

/// `project` as a path segment of GitLab's API: every byte outside the
/// unreserved set percent-encoded, so `group/sub/project` is
/// `group%2Fsub%2Fproject`.
[[nodiscard]] std::string encode_project(std::string_view project);

/// GitLab's per-file diffs (the `/diffs` endpoint's objects) as one unified
/// diff: `diff --git a/<old> b/<new>`, the new/deleted/renamed lines, `---`
/// and `+++` (`/dev/null` for an added or deleted side), then the hunks.
[[nodiscard]] std::string unified_diff(const nlohmann::json& files);

}  // namespace apogee::forge
