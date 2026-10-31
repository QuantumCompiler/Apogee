#pragma once

#include <cstdint>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

/// The forge seam (48a): GitHub and GitLab behind one interface.
///
/// Everything the review track needs from a code forge is four verbs -- a
/// merge request's metadata, its diff, an inline comment, a batch of them
/// published as one notification -- and the house's answer to auth is a
/// principle: **a vendor CLI is spawned, never opened.** So the first
/// transports are spawned `gh` and `glab` (`gh_transport.h`,
/// `glab_transport.h`), each call one bounded, captured child; their logins
/// are theirs, and Apogee never reads a token, a credential file or an OAuth
/// flow.
///
/// What comes back is **data**: parsed into the plain structs below, its
/// text made valid UTF-8 at entry, and never treated as instructions by
/// anything downstream. Nothing here listens or watches: every call is a
/// one-shot child.
namespace apogee::forge {

enum class Forge : std::uint8_t { GitHub, GitLab };

/// `github` or `gitlab`.
[[nodiscard]] std::string_view to_string(Forge forge) noexcept;

/// `github.com` or `gitlab.com`: the host a short reference means.
[[nodiscard]] std::string_view default_host(Forge forge) noexcept;

/// One merge request, spelled one way whatever it was typed as. A GitHub
/// pull request is a merge request here; its number is GitLab's `iid`.
struct MergeRequestRef {
    Forge forge = Forge::GitHub;
    /// The forge's host: `github.com`, `gitlab.com`, or a self-hosted one.
    std::string host;
    /// `owner/repo` on GitHub; `group/subgroup/project` on GitLab.
    std::string project;
    std::uint64_t number = 0;

    [[nodiscard]] bool operator==(const MergeRequestRef&) const = default;
};

/// Reads a reference:
///   - a full URL: `https://github.com/o/r/pull/12`, a GitLab
///     `https://gitlab.com/g/p/-/merge_requests/4`, a self-hosted host of
///     either (the path's shape says which), with or without a trailing
///     `/files`, `/diffs`, query or fragment;
///   - the short forms: `owner/repo#12` (GitHub) and `group/project!4`
///     (GitLab), the separator choosing the forge;
///   - a short form prefixed `github:` or `gitlab:`, the prefix choosing.
/// Nullopt with `error` saying why, naming the forms, for anything else.
[[nodiscard]] std::optional<MergeRequestRef> parse_ref(std::string_view text, std::string& error);

/// The canonical spelling: `github.com/owner/repo#12`,
/// `gitlab.com/group/project!4` -- what a watermark is keyed by.
[[nodiscard]] std::string to_string(const MergeRequestRef& ref);

struct ForgeProject;

/// A reference to `project` for a call that is not about one merge request
/// (a listing): its forge and host, number 0.
[[nodiscard]] MergeRequestRef project_ref(const ForgeProject& project);

/// Whether `text` is a timestamp a listing may carry into a query: digits
/// and `-:.TZ` only.
[[nodiscard]] bool is_timestamp(std::string_view text) noexcept;

/// Whether `text` is a commit id: 7 to 64 hexadecimal digits, nothing else
/// -- checked before one read from a comment reaches an API path.
[[nodiscard]] bool is_commit_id(std::string_view text) noexcept;

/// A merge request's metadata, as the forge reported it.
struct MergeRequest {
    MergeRequestRef ref;
    std::string title;
    std::string description;
    std::string author;
    /// `open`, `closed`, `merged` (GitLab says `opened`; read as `open`).
    std::string state;
    bool draft = false;
    std::string source_branch;
    std::string target_branch;
    /// The commits a comment anchors to: the merge base, GitLab's start
    /// commit (the base on GitHub), and the head.
    std::string base_sha;
    std::string start_sha;
    std::string head_sha;
    std::string web_url;
    /// ISO 8601, as the forge wrote it.
    std::string updated_at;
};

/// One inline comment on the new side of the diff.
struct InlineComment {
    std::string path{};
    /// The line on the new side.
    std::uint32_t line = 0;
    std::string body{};
    /// The same line on the old side, for a line the change did not touch:
    /// GitLab anchors an unchanged line by both. Unset for an added line.
    std::optional<std::uint32_t> old_line{};

    [[nodiscard]] bool operator==(const InlineComment&) const = default;
};

/// A comment already on a merge request, as the forge reports it: an
/// inline one with its path and new-side line, or a general one (a review's
/// body, a note) with neither.
struct PostedComment {
    std::string body;
    std::string author;
    /// Empty for a general comment.
    std::string path;
    std::uint32_t line = 0;
};

/// A repository on a forge -- or, for `owner_projects`, an owner.
struct ForgeProject {
    Forge forge = Forge::GitHub;
    /// Empty for the forge's default host.
    std::string host;
    std::string project;
};

/// An open merge request, as a listing shows it.
struct MergeRequestListing {
    MergeRequestRef ref;
    std::string title;
    std::string author;
    /// ISO 8601, as the forge wrote it.
    std::string updated_at;
    bool draft = false;
};

/// What a publish did.
struct PublishOutcome {
    /// Comments posted, the summary not counted.
    std::size_t posted = 0;
    /// Whether they went as one notification (GitHub's review, GitLab's
    /// draft notes published in bulk).
    bool batched = false;
    /// Anything worth saying about how they went: a forge without native
    /// batching posts one by one, and says so here.
    std::string note;
};

/// A refusal, in the CLI's own words, with the fix named.
class ForgeError : public std::runtime_error {
public:
    enum class Kind : std::uint8_t {
        /// The CLI is not installed, not on PATH, or would not start.
        Missing,
        /// The CLI is not logged in to this host.
        LoggedOut,
        /// The forge said the repository or merge request is not there (or
        /// not visible to this login).
        NotFound,
        /// The forge refused or failed the call otherwise.
        Api,
        /// The CLI answered with something that is not what was asked for.
        Malformed,
    };

    ForgeError(Kind kind, const std::string& message, std::string remedy);

    [[nodiscard]] Kind kind() const noexcept {
        return kind_;
    }

    /// The command that fixes it, when one does; empty otherwise.
    [[nodiscard]] const std::string& remedy() const noexcept {
        return remedy_;
    }

private:
    Kind kind_;
    std::string remedy_;
};

/// The seam. Every verb is synchronous and one-shot; a failure is a
/// `ForgeError`.
class ForgeClient {
public:
    ForgeClient() = default;
    virtual ~ForgeClient() = default;
    ForgeClient(const ForgeClient&) = delete;
    ForgeClient& operator=(const ForgeClient&) = delete;
    ForgeClient(ForgeClient&&) = delete;
    ForgeClient& operator=(ForgeClient&&) = delete;

    [[nodiscard]] virtual Forge forge() const noexcept = 0;

    [[nodiscard]] virtual MergeRequest merge_request(const MergeRequestRef& ref) = 0;

    /// The merge request's changes as one unified diff (`diff --git`
    /// headers, hunks), valid UTF-8.
    [[nodiscard]] virtual std::string diff(const MergeRequestRef& ref) = 0;

    /// The login this CLI acts as on `ref`'s host (since 48c: a re-review
    /// trusts only the markers its own login left -- anyone who can comment
    /// can write the same text).
    [[nodiscard]] virtual std::string current_user(const MergeRequestRef& ref) = 0;

    /// Every comment already on the merge request, inline and general, in
    /// the order the forge lists them (since 48c: what a re-review reads its
    /// own earlier findings from).
    [[nodiscard]] virtual std::vector<PostedComment> comments(const MergeRequestRef& ref) = 0;

    /// The changes from commit `from` to commit `to` in the merge request's
    /// repository, as one unified diff (since 48c: what a re-review limits
    /// itself to -- the lines a new push added).
    [[nodiscard]] virtual std::string compare(const MergeRequestRef& ref, std::string_view from,
                                              std::string_view to) = 0;

    /// The open merge requests on `project` updated after `updated_after`
    /// (ISO 8601; empty for all), oldest update first (since 48d: what a
    /// sweep reads past its watermark).
    [[nodiscard]] virtual std::vector<MergeRequestListing> open_merge_requests(
        const ForgeProject& project, std::string_view updated_after) = 0;

    /// The repositories `owner.project` holds -- an organization's, a
    /// group's (its subgroups' too), else a user's -- archived ones left
    /// out (since 48d: an owner target, expanded at sweep time).
    [[nodiscard]] virtual std::vector<std::string> owner_projects(const ForgeProject& owner) = 0;

    /// Posts one comment on its own, anchored to `request`'s head.
    virtual void create_comment(const MergeRequest& request, const InlineComment& comment) = 0;

    /// Posts `comments` and `summary` (a general comment; empty for none)
    /// as one notification where the forge can batch, else one by one,
    /// saying so. Nothing at all is posted for no comments and no summary.
    [[nodiscard]] virtual PublishOutcome publish_batch(const MergeRequest& request,
                                                       const std::vector<InlineComment>& comments,
                                                       std::string_view summary) = 0;
};

}  // namespace apogee::forge
