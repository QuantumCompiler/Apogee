#include "tools/git.h"

#include <algorithm>
#include <cctype>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "platform/child_process.h"
#include "tools/args.h"
#include "tools/process.h"

namespace apogee::tools {
namespace {

/// A git invocation that failed: carries what git said.
struct GitError : std::runtime_error {
    using std::runtime_error::runtime_error;
};

std::string rstrip(std::string text) {
    while (!text.empty() && (text.back() == '\n' || text.back() == '\r' || text.back() == ' ')) {
        text.pop_back();
    }
    return text;
}

class Git {
public:
    Git(std::filesystem::path repo, std::chrono::milliseconds timeout)
        : repo_{std::move(repo)}, timeout_{timeout} {}

    /// Runs git; throws GitError on a non-zero exit with stderr as the message.
    [[nodiscard]] std::string run(std::vector<std::string> arguments) const {
        const ProcessOutcome outcome = invoke(std::move(arguments));
        if (!outcome.start_error.empty()) {
            throw GitError(outcome.start_error);
        }
        if (outcome.timed_out) {
            throw GitError(
                "git did not finish within " +
                std::to_string(std::chrono::duration_cast<std::chrono::seconds>(timeout_).count()) +
                "s");
        }
        if (outcome.exit_code.value_or(1) != 0) {
            const std::string what = rstrip(outcome.err);
            throw GitError(what.empty()
                               ? "git exited " + std::to_string(outcome.exit_code.value_or(-1))
                               : what);
        }
        const std::string out = rstrip(outcome.out);
        return out.empty() ? "(no output)" : out;
    }

    /// Runs git; empty on any failure.
    [[nodiscard]] std::string quiet(std::vector<std::string> arguments) const {
        const ProcessOutcome outcome = invoke(std::move(arguments));
        if (!outcome.start_error.empty() || outcome.timed_out ||
            outcome.exit_code.value_or(1) != 0) {
            return {};
        }
        return rstrip(outcome.out);
    }

    [[nodiscard]] bool is_local_ref(const std::string& ref) const {
        return !quiet({"rev-parse", "--verify", "--quiet", ref + "^{commit}"}).empty();
    }

    [[nodiscard]] std::set<std::string> remotes() const {
        std::set<std::string> out;
        std::istringstream in{quiet({"remote"})};
        std::string name;
        while (in >> name) {
            out.insert(name);
        }
        return out;
    }

    [[nodiscard]] std::string current_branch() const {
        const std::string name = quiet({"rev-parse", "--abbrev-ref", "HEAD"});
        return name.empty() ? "HEAD" : name;
    }

    [[nodiscard]] std::string default_branch() const {
        // The remote's HEAD when a remote is configured ("refs/remotes/origin/main").
        const std::string remote_head = quiet({"symbolic-ref", "-q", "refs/remotes/origin/HEAD"});
        if (!remote_head.empty()) {
            const std::size_t slash = remote_head.rfind('/');
            if (slash != std::string::npos) {
                return remote_head.substr(slash + 1);
            }
        }
        for (const char* name : {"main", "master", "trunk", "develop"}) {
            if (!quiet({"rev-parse", "--verify", "--quiet", name}).empty()) {
                return name;
            }
        }
        return "HEAD";
    }

    [[nodiscard]] const std::filesystem::path& repo() const noexcept {
        return repo_;
    }

private:
    [[nodiscard]] ProcessOutcome invoke(std::vector<std::string> arguments) const {
        platform::ChildCommand command;
        command.program = "git";
        command.arguments = {"-C", repo_.string()};
        for (std::string& argument : arguments) {
            command.arguments.push_back(std::move(argument));
        }
        return run_to_completion(command, timeout_);
    }

    std::filesystem::path repo_;
    std::chrono::milliseconds timeout_;
};

void require_ref(const std::string& ref, std::string_view label) {
    if (!valid_ref(ref)) {
        throw GitError(std::string{label} +
                       " must be a git ref or branch name (letters, digits, and . _ / - only, "
                       "not starting with '-'); got '" +
                       ref + "'");
    }
}

/// Resolves a review ref to something a diff spec can name, fetching per
/// `fetch_mode` -- Ommi's `_resolve_review_ref`, case for case.
std::string resolve_review_ref(const Git& git, const std::string& ref, const std::string& remote,
                               const std::string& fetch_mode) {
    const std::set<std::string> remotes = git.remotes();
    const std::string first = ref.substr(0, ref.find('/'));
    const bool qualified = ref.starts_with("refs/") || remotes.contains(first);
    const bool local = git.is_local_ref(ref);
    bool do_fetch = false;
    if (fetch_mode == "always") {
        do_fetch = true;
    } else if (fetch_mode == "never") {
        do_fetch = false;
    } else {
        do_fetch = !local;
    }
    if (qualified) {
        if (do_fetch && remotes.contains(first)) {
            (void)git.quiet({"fetch", first});
        }
        if (git.is_local_ref(ref)) {
            return ref;
        }
        throw GitError("ref '" + ref + "' could not be resolved" +
                       (do_fetch ? "" : " (fetching disabled -- use fetch: always)"));
    }
    if (do_fetch) {
        (void)git.quiet({"fetch", remote, ref});
        for (const std::string& candidate : {remote + "/" + ref, std::string{"FETCH_HEAD"}}) {
            if (git.is_local_ref(candidate)) {
                return candidate;
            }
        }
    }
    if (git.is_local_ref(ref)) {
        return ref;
    }
    if (fetch_mode == "never") {
        throw GitError("ref '" + ref +
                       "' not found locally and fetching is disabled (fetch: never); fetch it "
                       "first or allow fetching");
    }
    throw GitError("ref '" + ref + "' could not be resolved locally or from remote '" + remote +
                   "'");
}

bool looks_like_sha(const std::string& text) {
    if (text.size() < 7 || text.size() > 40) {
        return false;
    }
    return std::all_of(text.begin(), text.end(),
                       [](unsigned char c) { return std::isxdigit(c) != 0; });
}

bool all_digits(const std::string& text) {
    return !text.empty() && std::all_of(text.begin(), text.end(),
                                        [](unsigned char c) { return std::isdigit(c) != 0; });
}

template <typename Body>
agent::ToolOutcome guarded(std::string_view arguments, std::string_view example, Body&& body) {
    agent::ToolOutcome failure;
    const std::optional<Arguments> args = parse_arguments(arguments, example, failure);
    if (!args.has_value()) {
        return failure;
    }
    try {
        return ok(body(*args));
    } catch (const GitError& e) {
        return error(e.what());
    } catch (const std::exception& e) {
        return error(e.what());
    }
}

}  // namespace

bool valid_ref(std::string_view ref) noexcept {
    if (ref.empty() || !(std::isalnum(static_cast<unsigned char>(ref[0])) != 0)) {
        return false;
    }
    return std::all_of(ref.begin(), ref.end(), [](unsigned char c) {
        return std::isalnum(c) != 0 || c == '.' || c == '_' || c == '/' || c == '-';
    });
}

std::filesystem::path find_repo(std::string_view hint, const std::filesystem::path& start) {
    if (!trim(hint).empty()) {
        return std::filesystem::path{trim(hint)};
    }
    std::error_code code;
    std::filesystem::path dir = start.empty() ? std::filesystem::current_path(code) : start;
    for (;;) {
        if (std::filesystem::is_directory(dir / ".git", code)) {
            return dir;
        }
        const std::filesystem::path parent = dir.parent_path();
        if (parent == dir || parent.empty()) {
            return start.empty() ? std::filesystem::path{"."} : start;
        }
        dir = parent;
    }
}

void register_git_tools(agent::ToolRegistry& registry, const GitOptions& options) {
    const auto open = [options](const Arguments& args) {
        return Git{find_repo(args.string("repo"), options.working_directory), options.timeout};
    };
    // The review defaults, read at CALL time when a live value is shared.
    const auto review_of = [options]() -> ReviewDefaults {
        return options.live_review != nullptr ? *options.live_review : options.review;
    };
    const std::string repo_property =
        R"("repo":{"type":"string","description":"Repository path; defaults to the nearest repository above the working directory"})";

    agent::Tool status;
    status.name = "git_status";
    status.description = "Show the working tree status, the current branch and the default branch.";
    status.parameters_schema = R"({"type":"object","properties":{)" + repo_property + "}}";
    status.run = [open](std::string_view arguments) {
        return guarded(arguments, "{}", [&](const Arguments& args) {
            const Git git = open(args);
            return "Repo:    " + git.repo().string() + "\nBranch:  " + git.current_branch() +
                   "  (default: " + git.default_branch() + ")\n\n" + git.run({"status"});
        });
    };
    registry.add(status);

    agent::Tool log;
    log.name = "git_log";
    log.description =
        "Show recent commits: hash, date, subject, author. `range` is a revision range like "
        "v1.0..v2.0 or main..HEAD; a bare ref means everything since that ref.";
    log.parameters_schema =
        R"({"type":"object","properties":{)" + repo_property +
        R"(,"n":{"type":"integer","description":"How many commits (1-200); default 10, or 200 with a range"},"branch":{"type":"string","description":"A branch to log instead of the current one"},"range":{"type":"string","description":"A revision range, or a bare ref meaning <ref>..HEAD"}}})";
    log.run = [open, review_of](std::string_view arguments) {
        return guarded(arguments, R"({"n": 10})", [&](const Arguments& args) {
            const Git git = open(args);
            const std::string branch = args.string("branch");
            const std::string range = args.string("range");
            const std::int64_t n = args.integer("n").value_or(range.empty() ? 10 : 200);
            if (n < 1 || n > 200) {
                throw GitError("n must be between 1 and 200");
            }
            std::vector<std::string> command{"log", "-" + std::to_string(n), "--date=short",
                                             "--pretty=format:%h %ad %s (%an)"};
            std::string header;
            const ReviewDefaults review = review_of();
            if (range.empty() && branch.empty() && review.active()) {
                // The review form: the commits the branch under review adds
                // over its base, from the same defaults `git_diff` uses --
                // so "read the commit messages" means the reviewed branch's,
                // never the checked-out one's.
                const std::string remote = review.remote.empty() ? "origin" : review.remote;
                const std::string fetch = review.fetch.empty() ? "auto" : review.fetch;
                std::string head = review.head.empty() ? "HEAD" : review.head;
                std::string base = review.base.empty() ? git.default_branch() : review.base;
                require_ref(head, "head");
                require_ref(base, "base");
                require_ref(remote, "remote");
                const std::string head_ref = resolve_review_ref(git, head, remote, fetch);
                const std::string base_ref = resolve_review_ref(git, base, remote, fetch);
                const std::string spec = base_ref + ".." + head_ref;
                command.push_back(spec);
                header = "Repo: " + git.repo().string() + "  |  Range: " + spec +
                         "  (commits the branch under review adds)\n\n";
            } else if (!range.empty()) {
                require_ref(range, "range");
                const std::string spec =
                    range.find("..") != std::string::npos ? range : range + "..HEAD";
                command.push_back(spec);
                header = "Repo: " + git.repo().string() + "  |  Range: " + spec + "\n\n";
            } else if (!branch.empty()) {
                require_ref(branch, "branch");
                command.push_back(branch);
                header = "Repo: " + git.repo().string() + "  |  Branch: " + branch + "\n\n";
            } else {
                header = "Repo: " + git.repo().string() + "  |  Branch: " + git.current_branch() +
                         "\n\n";
            }
            return header + git.run(std::move(command));
        });
    };
    registry.add(log);

    agent::Tool diff;
    diff.name = "git_diff";
    diff.description =
        "Show a diff. With `head`/`base`: the merge-base diff base...head (what the head "
        "branch adds), resolving refs against `remote` and fetching them when absent. "
        "Otherwise `target`: omitted diffs the current branch against the default branch "
        "(or shows uncommitted changes when on it); an integer N shows the last N commits; a "
        "commit SHA diffs from it to HEAD; any other ref diffs ref...HEAD. `file` restricts "
        "the diff to one path.";
    diff.parameters_schema =
        R"({"type":"object","properties":{)" + repo_property +
        R"(,"target":{"type":"string"},"head":{"type":"string","description":"The branch under review"},"base":{"type":"string","description":"The base to compare against; defaults to the default branch"},"remote":{"type":"string","description":"Remote to resolve refs against; default origin"},"fetch":{"type":"string","enum":["auto","always","never"]},"file":{"type":"string","description":"Restrict to one path"}}})";
    diff.run = [open, review_of](std::string_view arguments) {
        return guarded(arguments, "{}", [&](const Arguments& args) {
            const Git git = open(args);
            const ReviewDefaults review = review_of();
            const std::string file = args.string("file");
            if (!file.empty() && file.starts_with("-")) {
                throw GitError("file must be a path, not an option");
            }
            const std::string branch = git.current_branch();
            const std::string dft = git.default_branch();

            // Review mode: the tool's own arguments, else the defaults set by
            // flags -- plain parameters, never an environment side channel.
            std::string head = args.string("head");
            std::string base = args.string("base");
            if (head.empty() && base.empty() && review.active()) {
                head = review.head;
                base = review.base;
            }
            std::string spec;
            std::string header;
            if (!head.empty() || !base.empty()) {
                std::string remote = args.string("remote");
                if (remote.empty()) {
                    remote = review.remote.empty() ? "origin" : review.remote;
                }
                std::string fetch = args.string("fetch");
                if (fetch.empty()) {
                    fetch = review.fetch.empty() ? "auto" : review.fetch;
                }
                if (fetch != "auto" && fetch != "always" && fetch != "never") {
                    throw GitError("fetch must be auto, always, or never");
                }
                if (head.empty()) {
                    head = "HEAD";
                }
                if (base.empty()) {
                    base = dft;
                }
                require_ref(head, "head");
                require_ref(base, "base");
                require_ref(remote, "remote");
                const std::string head_ref = resolve_review_ref(git, head, remote, fetch);
                const std::string base_ref = resolve_review_ref(git, base, remote, fetch);
                spec = base_ref + "..." + head_ref;
                header = "Repo: " + git.repo().string() + "  |  Review: " + spec +
                         "  (branch under review vs base -- changes since merge base)\n\n";
            } else {
                const std::string target = args.string("target");
                if (target.empty()) {
                    if (branch == dft || dft == "HEAD") {
                        spec = "HEAD";
                        header = "Repo: " + git.repo().string() + "  |  Branch: " + branch +
                                 "  |  Diff: uncommitted changes\n\n";
                    } else {
                        spec = dft + "...HEAD";
                        header = "Repo: " + git.repo().string() + "  |  Branch: " + branch +
                                 "  |  Diff vs default branch (" + dft +
                                 ") -- changes since merge base\n\n";
                    }
                } else if (looks_like_sha(target)) {
                    spec = target + "..HEAD";
                    header = "Repo: " + git.repo().string() + "  |  Branch: " + branch +
                             "  |  Diff: HEAD since commit " + target + "\n\n";
                } else if (all_digits(target)) {
                    spec = "HEAD~" + target + "..HEAD";
                    header = "Repo: " + git.repo().string() + "  |  Branch: " + branch +
                             "  |  Diff: last " + target + " commit(s)\n\n";
                } else {
                    require_ref(target, "target");
                    spec = target + "...HEAD";
                    header = "Repo: " + git.repo().string() + "  |  Branch: " + branch +
                             "  |  Diff vs " + target + " -- changes since merge base\n\n";
                }
            }
            std::vector<std::string> command{"diff", spec};
            if (!file.empty()) {
                command.push_back("--");
                command.push_back(file);
            }
            return header + git.run(std::move(command));
        });
    };
    registry.add(diff);

    agent::Tool show;
    show.name = "git_show";
    show.description = "Show one commit (message and file stats); defaults to HEAD.";
    show.parameters_schema =
        R"({"type":"object","properties":{)" + repo_property +
        R"(,"ref":{"type":"string","description":"A commit, tag or branch; default HEAD"}}})";
    show.run = [open](std::string_view arguments) {
        return guarded(arguments, R"({"ref": "HEAD"})", [&](const Arguments& args) {
            const Git git = open(args);
            std::string ref = args.string("ref");
            if (ref.empty()) {
                ref = "HEAD";
            }
            require_ref(ref, "ref");
            return git.run({"show", "--stat", ref});
        });
    };
    registry.add(show);
}

}  // namespace apogee::tools
