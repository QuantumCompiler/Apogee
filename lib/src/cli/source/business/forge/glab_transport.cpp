#include "forge/glab_transport.h"

#include <nlohmann/json.hpp>

#include <sstream>

#include "contracts/utf8.h"

namespace apogee::forge {
namespace {

[[nodiscard]] std::string merge_requests(const MergeRequestRef& ref) {
    return "projects/" + encode_project(ref.project) + "/merge_requests/" +
           std::to_string(ref.number);
}

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

[[nodiscard]] bool flag_at(const nlohmann::json& object, std::string_view key) {
    const auto it = object.find(key);
    return it != object.end() && it->is_boolean() && it->get<bool>();
}

[[nodiscard]] ForgeError malformed(const MergeRequestRef& ref, std::string_view what) {
    return ForgeError{
        ForgeError::Kind::Malformed,
        "glab answered for " + to_string(ref) + " with something that is not " + std::string{what},
        ""};
}

/// Every value in `out`, one after another: `--paginate` prints each page
/// as its own JSON array.
[[nodiscard]] nlohmann::json concatenated_arrays(const std::string& out,
                                                 const MergeRequestRef& ref) {
    nlohmann::json all = nlohmann::json::array();
    std::istringstream in{out};
    while ((in >> std::ws) && in.peek() != std::char_traits<char>::eof()) {
        nlohmann::json page;
        try {
            in >> page;
        } catch (const nlohmann::json::exception&) {
            throw malformed(ref, "a list of file diffs");
        }
        if (!page.is_array()) {
            throw malformed(ref, "a list of file diffs");
        }
        for (nlohmann::json& file : page) {
            all.push_back(std::move(file));
        }
    }
    return all;
}

/// Where a comment sits: the merge request's three commits and the line.
[[nodiscard]] nlohmann::json position(const MergeRequest& request, const InlineComment& comment) {
    nlohmann::json at{{"position_type", "text"},        {"base_sha", request.base_sha},
                      {"start_sha", request.start_sha}, {"head_sha", request.head_sha},
                      {"old_path", comment.path},       {"new_path", comment.path},
                      {"new_line", comment.line}};
    if (comment.old_line.has_value()) {
        at["old_line"] = *comment.old_line;
    }
    return at;
}

}  // namespace

std::string encode_project(std::string_view project) {
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    for (const char c : project) {
        const auto byte = static_cast<unsigned char>(c);
        const bool unreserved = (byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
                                (byte >= '0' && byte <= '9') || c == '-' || c == '.' || c == '_' ||
                                c == '~';
        if (unreserved) {
            out += c;
        } else {
            out += '%';
            out += kHex[byte >> 4U];
            out += kHex[byte & 0x0FU];
        }
    }
    return out;
}

std::string unified_diff(const nlohmann::json& files) {
    std::string out;
    for (const nlohmann::json& file : files) {
        if (!file.is_object()) {
            continue;
        }
        const std::string old_path = text_at(file, {"old_path"});
        const std::string new_path = text_at(file, {"new_path"});
        const bool added = flag_at(file, "new_file");
        const bool deleted = flag_at(file, "deleted_file");
        const bool renamed = flag_at(file, "renamed_file");
        std::string a_mode = text_at(file, {"a_mode"});
        std::string b_mode = text_at(file, {"b_mode"});
        out += "diff --git a/" + old_path + " b/" + new_path + "\n";
        if (added) {
            out += "new file mode " + (b_mode.empty() ? std::string{"100644"} : b_mode) + "\n";
        } else if (deleted) {
            out += "deleted file mode " + (a_mode.empty() ? std::string{"100644"} : a_mode) + "\n";
        } else if (renamed) {
            out += "rename from " + old_path + "\nrename to " + new_path + "\n";
        }
        const std::string hunks = text_at(file, {"diff"});
        if (hunks.empty()) {
            continue;  // a rename alone, or a binary file: the header says it
        }
        out += "--- " + (added ? std::string{"/dev/null"} : "a/" + old_path) + "\n";
        out += "+++ " + (deleted ? std::string{"/dev/null"} : "b/" + new_path) + "\n";
        out += hunks;
        if (out.back() != '\n') {
            out += '\n';
        }
    }
    return out;
}

GlabTransport::GlabTransport(CliTransportOptions options)
    : CliForgeClient{Cli{.name = "glab",
                         .product = "the GitLab CLI",
                         .install_url = "https://gitlab.com/gitlab-org/cli"},
                     std::move(options)} {}

MergeRequest GlabTransport::merge_request(const MergeRequestRef& ref) {
    require_forge(ref);
    const nlohmann::json json =
        nlohmann::json::parse(api(ref, {merge_requests(ref)}), nullptr, false);
    if (json.is_discarded() || !json.is_object()) {
        throw malformed(ref, "a merge request");
    }
    MergeRequest request;
    request.ref = ref;
    request.title = text_at(json, {"title"});
    request.description = text_at(json, {"description"});
    request.author = text_at(json, {"author", "username"});
    request.state = text_at(json, {"state"});
    if (request.state == "opened") {
        request.state = "open";
    }
    request.draft = flag_at(json, "draft") || flag_at(json, "work_in_progress");
    request.source_branch = text_at(json, {"source_branch"});
    request.target_branch = text_at(json, {"target_branch"});
    request.base_sha = text_at(json, {"diff_refs", "base_sha"});
    request.start_sha = text_at(json, {"diff_refs", "start_sha"});
    request.head_sha = text_at(json, {"diff_refs", "head_sha"});
    if (request.head_sha.empty()) {
        request.head_sha = text_at(json, {"sha"});
    }
    request.web_url = text_at(json, {"web_url"});
    request.updated_at = text_at(json, {"updated_at"});
    return request;
}

std::string GlabTransport::diff(const MergeRequestRef& ref) {
    require_forge(ref);
    const std::string out = api(ref, {"--paginate", merge_requests(ref) + "/diffs?per_page=100"});
    return unified_diff(concatenated_arrays(out, ref));
}

void GlabTransport::create_comment(const MergeRequest& request, const InlineComment& comment) {
    require_forge(request.ref);
    const nlohmann::json body{{"body", comment.body}, {"position", position(request, comment)}};
    (void)api(request.ref,
              {"--method", "POST", "-H", "Content-Type: application/json",
               merge_requests(request.ref) + "/discussions", "--input", "-"},
              body.dump());
}

PublishOutcome GlabTransport::publish_batch(const MergeRequest& request,
                                            const std::vector<InlineComment>& comments,
                                            std::string_view summary) {
    require_forge(request.ref);
    if (comments.empty() && summary.empty()) {
        return {};
    }
    // Each a draft note, then one bulk publish: GitLab's batch, one
    // notification. A failure part-way leaves the drafts made so far as
    // the login's own drafts on the merge request, and says so.
    const std::string drafts = merge_requests(request.ref) + "/draft_notes";
    std::size_t made = 0;
    const auto draft = [&](const nlohmann::json& body) {
        try {
            (void)api(request.ref,
                      {"--method", "POST", "-H", "Content-Type: application/json", drafts,
                       "--input", "-"},
                      body.dump());
        } catch (const ForgeError& e) {
            if (made == 0) {
                throw;
            }
            throw ForgeError{e.kind(),
                             std::string{e.what()} + " (" + std::to_string(made) +
                                 " draft note(s) were made and not published: they stay as "
                                 "this login's drafts on the merge request)",
                             e.remedy()};
        }
        ++made;
    };
    for (const InlineComment& comment : comments) {
        draft(nlohmann::json{{"note", comment.body}, {"position", position(request, comment)}});
    }
    if (!summary.empty()) {
        draft(nlohmann::json{{"note", summary}});
    }
    try {
        (void)api(request.ref, {"--method", "POST", drafts + "/bulk_publish"});
    } catch (const ForgeError& e) {
        throw ForgeError{e.kind(),
                         std::string{e.what()} + " (" + std::to_string(made) +
                             " draft note(s) were made and not published: they stay as this "
                             "login's drafts on the merge request)",
                         e.remedy()};
    }
    return PublishOutcome{.posted = comments.size(), .batched = true, .note = {}};
}

bool GlabTransport::logged_out(const CliResult& result) const {
    return contains_folded(result.err, "glab auth login") ||
           contains_folded(result.err, "401 Unauthorized") ||
           contains_folded(result.err, "no token");
}

bool GlabTransport::not_found(const CliResult& result) const {
    return contains_folded(result.err, "404");
}

}  // namespace apogee::forge
