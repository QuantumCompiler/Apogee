#pragma once

#include "forge/cli_forge.h"

/// GitHub through a spawned `gh` (48a): every verb one `gh api` child,
/// authenticated by `gh`'s own login.
///
///   metadata  gh api repos/<owner>/<repo>/pulls/<n>
///   diff      gh api -H "Accept: application/vnd.github.diff" repos/…/pulls/<n>
///   comment   gh api --method POST -H "Content-Type: application/json"
///                    repos/…/pulls/<n>/comments --input -
///   batch     the same to …/pulls/<n>/reviews: one review, `event: COMMENT`,
///             its body the summary and its comments the inline ones --
///             GitHub's native batch, one notification
///   login     gh api user
///   comments  gh api --paginate …/pulls/<n>/comments, then …/pulls/<n>/reviews
///             (each review's body a general comment)
///   listing   gh api --paginate
///   "repos/<o>/<r>/pulls?state=open&sort=updated&direction=asc&per_page=100" owner     gh api
///   --paginate "orgs/<owner>/repos?per_page=100", else users/<owner>/repos compare   gh api -H
///   "Accept: application/vnd.github.diff"
///                    repos/<owner>/<repo>/compare/<from>...<to>
///
/// A GitHub Enterprise host adds `--hostname <host>` after `api`.
namespace apogee::forge {

class GhTransport final : public CliForgeClient {
public:
    explicit GhTransport(CliTransportOptions options = {});

    [[nodiscard]] Forge forge() const noexcept override {
        return Forge::GitHub;
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

}  // namespace apogee::forge
