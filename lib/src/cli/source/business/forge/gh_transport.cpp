#include "forge/gh_transport.h"

#include <nlohmann/json.hpp>

#include "contracts/utf8.h"

namespace apogee::forge {
namespace {

[[nodiscard]] std::string pulls(const MergeRequestRef& ref) {
    return "repos/" + ref.project + "/pulls/" + std::to_string(ref.number);
}

/// The string at `path` in `object`, made valid UTF-8; empty when absent or
/// not a string.
[[nodiscard]] std::string text_at(const nlohmann::json& object,
                                  std::initializer_list<std::string_view> path) {
    const nlohmann::json* at = &object;
    for (const std::string_view key : path) {
        if (!at->is_object()) {
            return {};
        }
        const auto it = at->find(key);
        if (it == at->end()) {
            return {};
        }
        at = &*it;
    }
    return at->is_string() ? harness::valid_utf8(at->get<std::string>()) : std::string{};
}

[[nodiscard]] nlohmann::json parsed(const std::string& out, const MergeRequestRef& ref) {
    nlohmann::json json = nlohmann::json::parse(out, nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        throw ForgeError{ForgeError::Kind::Malformed,
                         "gh answered for " + to_string(ref) +
                             " with something that is not a "
                             "pull request",
                         ""};
    }
    return json;
}

[[nodiscard]] nlohmann::json comment_json(const InlineComment& comment) {
    return nlohmann::json{
        {"path", comment.path}, {"line", comment.line}, {"side", "RIGHT"}, {"body", comment.body}};
}

}  // namespace

GhTransport::GhTransport(CliTransportOptions options)
    : CliForgeClient{
          Cli{.name = "gh", .product = "the GitHub CLI", .install_url = "https://cli.github.com"},
          std::move(options)} {}

MergeRequest GhTransport::merge_request(const MergeRequestRef& ref) {
    require_forge(ref);
    const nlohmann::json json = parsed(api(ref, {pulls(ref)}), ref);
    MergeRequest request;
    request.ref = ref;
    request.title = text_at(json, {"title"});
    request.description = text_at(json, {"body"});
    request.author = text_at(json, {"user", "login"});
    const bool merged = json.contains("merged_at") && json["merged_at"].is_string();
    request.state = merged ? "merged" : text_at(json, {"state"});
    request.draft =
        json.contains("draft") && json["draft"].is_boolean() && json["draft"].get<bool>();
    request.source_branch = text_at(json, {"head", "ref"});
    request.target_branch = text_at(json, {"base", "ref"});
    request.head_sha = text_at(json, {"head", "sha"});
    request.base_sha = text_at(json, {"base", "sha"});
    request.start_sha = request.base_sha;
    request.web_url = text_at(json, {"html_url"});
    request.updated_at = text_at(json, {"updated_at"});
    return request;
}

std::string GhTransport::diff(const MergeRequestRef& ref) {
    require_forge(ref);
    return harness::valid_utf8(api(ref, {"-H", "Accept: application/vnd.github.diff", pulls(ref)}));
}

std::string GhTransport::current_user(const MergeRequestRef& ref) {
    const nlohmann::json json = nlohmann::json::parse(api(ref, {"user"}), nullptr, false);
    const std::string login = json.is_object() ? text_at(json, {"login"}) : std::string{};
    if (login.empty()) {
        throw ForgeError{ForgeError::Kind::Malformed,
                         "gh did not say which login it acts as on " + ref.host, ""};
    }
    return login;
}

std::vector<PostedComment> GhTransport::comments(const MergeRequestRef& ref) {
    require_forge(ref);
    std::vector<PostedComment> out;
    const auto pages = [&](const std::string& endpoint) {
        const std::optional<nlohmann::json> items =
            concatenated_arrays(api(ref, {"--paginate", endpoint}));
        if (!items.has_value()) {
            throw ForgeError{ForgeError::Kind::Malformed,
                             "gh answered for " + to_string(ref) +
                                 " with something that is not a list of comments",
                             ""};
        }
        return *items;
    };
    for (const nlohmann::json& item : pages(pulls(ref) + "/comments")) {
        PostedComment comment;
        comment.body = text_at(item, {"body"});
        comment.author = text_at(item, {"user", "login"});
        comment.path = text_at(item, {"path"});
        if (item.contains("line") && item["line"].is_number_unsigned()) {
            comment.line = item["line"].get<std::uint32_t>();
        }
        out.push_back(std::move(comment));
    }
    for (const nlohmann::json& item : pages(pulls(ref) + "/reviews")) {
        PostedComment comment;
        comment.body = text_at(item, {"body"});
        comment.author = text_at(item, {"user", "login"});
        if (!comment.body.empty()) {
            out.push_back(std::move(comment));
        }
    }
    return out;
}

std::string GhTransport::compare(const MergeRequestRef& ref, std::string_view from,
                                 std::string_view to) {
    require_forge(ref);
    if (!is_commit_id(from) || !is_commit_id(to)) {
        throw ForgeError{ForgeError::Kind::Malformed,
                         "a comparison takes two commit ids, not '" + std::string{from} +
                             "' and '" + std::string{to} + "'",
                         ""};
    }
    return harness::valid_utf8(api(
        ref, {"-H", "Accept: application/vnd.github.diff",
              "repos/" + ref.project + "/compare/" + std::string{from} + "..." + std::string{to}}));
}

void GhTransport::create_comment(const MergeRequest& request, const InlineComment& comment) {
    require_forge(request.ref);
    nlohmann::json body = comment_json(comment);
    body["commit_id"] = request.head_sha;
    (void)api(request.ref,
              {"--method", "POST", "-H", "Content-Type: application/json",
               pulls(request.ref) + "/comments", "--input", "-"},
              body.dump());
}

PublishOutcome GhTransport::publish_batch(const MergeRequest& request,
                                          const std::vector<InlineComment>& comments,
                                          std::string_view summary) {
    require_forge(request.ref);
    if (comments.empty() && summary.empty()) {
        return {};
    }
    // One review holding every comment: GitHub's batch, one notification.
    nlohmann::json body{{"commit_id", request.head_sha}, {"event", "COMMENT"}};
    if (!summary.empty()) {
        body["body"] = summary;
    }
    nlohmann::json inline_comments = nlohmann::json::array();
    for (const InlineComment& comment : comments) {
        inline_comments.push_back(comment_json(comment));
    }
    body["comments"] = std::move(inline_comments);
    (void)api(request.ref,
              {"--method", "POST", "-H", "Content-Type: application/json",
               pulls(request.ref) + "/reviews", "--input", "-"},
              body.dump());
    return PublishOutcome{.posted = comments.size(), .batched = true, .note = {}};
}

bool GhTransport::logged_out(const CliResult& result) const {
    // gh exits 4 when it needs a login, and says `gh auth login`.
    return result.exit_code == 4 || contains_folded(result.err, "gh auth login") ||
           contains_folded(result.err, "HTTP 401") ||
           contains_folded(result.err, "authentication required");
}

bool GhTransport::not_found(const CliResult& result) const {
    return contains_folded(result.err, "HTTP 404") || contains_folded(result.err, "Not Found");
}

}  // namespace apogee::forge
