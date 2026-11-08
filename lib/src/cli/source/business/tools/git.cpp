#include "tools/git.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <fstream>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "platform/child_process.h"
#include "platform/platform.h"
#include "tools/args.h"
#include "tools/fs.h"
#include "tools/process.h"

namespace apogee::tools {
namespace {

/// Ahead of every command (57o). `core.fsmonitor` names a command git runs to
/// learn what changed; `log.showSignature` would have `log` and `show` run
/// `gpg.program` on a signed commit; `status.submoduleSummary` would have
/// `status` start `git log` inside each submodule moved to another commit and
/// print its subjects -- from whatever repository a `.git` file there names;
/// `core.hooksPath` pointed at `/dev/null` runs no hook at all, neither from
/// `.git/hooks` nor from a folder the repository's config names (the review
/// form's `git fetch` would run `reference-transaction`); and no read writes
/// the index, which would run `post-index-change`: `--no-optional-locks`
/// keeps `status` from writing back the index it refreshed, and
/// `diff.autoRefreshIndex=false` keeps `git diff` against the work tree from
/// doing the same, which it does whatever the lock option says. Each is set
/// on the command line, so it outranks the repository's config, and travels
/// to any git that git starts (a submodule's).
constexpr std::array<std::string_view, 11> kGitHardening{"-c",
                                                         "core.fsmonitor=false",
                                                         "-c",
                                                         "log.showSignature=false",
                                                         "-c",
                                                         "status.submoduleSummary=false",
                                                         "-c",
                                                         "core.hooksPath=/dev/null",
                                                         "-c",
                                                         "diff.autoRefreshIndex=false",
                                                         "--no-optional-locks"};

/// After a command that diffs (57o): no `diff.external` or `diff.<driver>.command`,
/// no `textconv`, no look inside a submodule's work tree, where its own
/// config's filter drivers would run, and a submodule shown as the commit ids
/// it moved between (`--submodule=short`) -- never diffed or logged inside it,
/// as the repository's `diff.submodule` could ask: that runs a `git diff` in
/// the submodule, which git hands the `-c` options but not `--no-ext-diff` or
/// `--no-textconv`, so the submodule's own external diff or textconv would run.
constexpr std::array<std::string_view, 4> kDiffHardening{
    "--no-ext-diff", "--no-textconv", "--ignore-submodules=dirty", "--submodule=short"};

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

/// What a git command run in a folder opens (57o): the git directory, the
/// common directory -- where the objects and refs live -- and the work tree,
/// empty for a repository with none. Absolute, as git reports them.
struct Opened {
    std::filesystem::path git_dir;
    std::filesystem::path common_dir;
    std::filesystem::path work_tree;
};

/// The most object stores one repository's alternates are followed through
/// (57o), its own included; past it the repository is refused.
constexpr std::size_t kMaxObjectStores = 32;

/// Whether `path` lands inside `root`, judged by the sandbox's own resolver:
/// every link followed, a dangling one included.
bool lands_inside(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::string unused;
    return resolve_in_root(root, path.string(), unused).has_value();
}

/// The same place, both resolved: a back-pointer and the file it names.
bool same_place(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code code_a;
    std::error_code code_b;
    const std::filesystem::path left = platform::resolve_path(a, code_a);
    const std::filesystem::path right = platform::resolve_path(b, code_b);
    return !code_a && !code_b && left == right;
}

/// The first link directly inside `dir` that leads out of `root`, or empty
/// (57o). A link that cannot be read counts as one that leads out.
std::filesystem::path link_out_of(const std::filesystem::path& root,
                                  const std::filesystem::path& dir) {
    std::error_code code;
    for (std::filesystem::directory_iterator it{
             dir, std::filesystem::directory_options::skip_permission_denied, code};
         !code && it != std::filesystem::directory_iterator{}; it.increment(code)) {
        std::error_code link_code;
        const bool link = platform::is_link(it->path(), link_code);
        if (link_code || (link && !lands_inside(root, it->path()))) {
            return it->path();
        }
    }
    return {};
}

/// What a git or common directory holds that a read shows: the objects, the
/// refs, `HEAD`, and the index whose paths `git status` lists (57o). A link
/// there that leads out is refused; one elsewhere -- `hooks` linked to the
/// person's own, say -- is nothing these tools read.
constexpr std::array<std::string_view, 5> kReadEntries{"objects", "refs", "packed-refs", "HEAD",
                                                       "index"};

/// Why a repository wholly inside the root still reads from outside it, or
/// empty (57o): one of `kReadEntries` made a link out of it, a link in its
/// object store or its `pack` folder that leads out, or an
/// `objects/info/alternates` line that borrows objects from outside. Said as
/// what git would open.
std::string object_store_leak(const std::filesystem::path& root, const Opened& opened) {
    std::vector<std::filesystem::path> tops{opened.git_dir};
    if (!same_place(opened.common_dir, opened.git_dir)) {
        tops.push_back(opened.common_dir);
    }
    for (const std::filesystem::path& top : tops) {
        for (const std::string_view name : kReadEntries) {
            const std::filesystem::path entry = top / name;
            std::error_code code;
            const bool link = platform::is_link(entry, code);
            if (code || (link && !lands_inside(root, entry))) {
                return entry.string() + ", a link that leads out of it";
            }
        }
    }
    std::vector<std::filesystem::path> stores{opened.common_dir / "objects"};
    for (std::size_t at = 0; at < stores.size(); ++at) {
        const std::filesystem::path store = stores[at];
        for (const std::filesystem::path& dir : {store, store / "pack"}) {
            if (const std::filesystem::path out = link_out_of(root, dir); !out.empty()) {
                return out.string() + ", a link that leads out of it";
            }
        }
        const std::filesystem::path list = store / "info" / "alternates";
        std::ifstream in{list, std::ios::binary};
        std::string line;
        while (std::getline(in, line)) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            if (line.empty() || line.front() == '#') {
                continue;
            }
            if (line.front() == '"') {
                return "an object store named by a quoted line in " + list.string() +
                       ", which cannot be judged";
            }
            std::filesystem::path alternate{line};
            if (alternate.is_relative()) {
                alternate = store / alternate;
            }
            if (!lands_inside(root, alternate)) {
                return "objects borrowed from " + alternate.string() + " (" + list.string() + ")";
            }
            const bool seen = std::ranges::any_of(stores, [&](const std::filesystem::path& known) {
                return same_place(known, alternate);
            });
            if (!seen) {
                if (stores.size() == kMaxObjectStores) {
                    return "more than " + std::to_string(kMaxObjectStores) +
                           " object stores through " + list.string();
                }
                stores.push_back(alternate);
            }
        }
    }
    return {};
}

/// The refusal for a repository git would open outside the root (57o), in
/// the sandbox's words; `shown` names the repository the call meant.
std::string outside_root(const std::filesystem::path& root, std::string_view shown,
                         const std::string& opens, const std::filesystem::path& through) {
    std::error_code code;
    const std::filesystem::path canonical_root = platform::resolve_path(root, code);
    return std::string{shown} + " is outside the allowed root " +
           (code ? root : canonical_root).string() + ": git opens " + opens +
           (through.empty() ? std::string{} : ", by way of " + through.string()) +
           ". The git tools read a repository inside the folder Apogee was started in unless "
           "tools.fs_root in the config says otherwise: start Apogee in a folder that contains "
           "it, or set tools.fs_root to expand access.";
}

class Git {
public:
    /// The repository at `repo`. When `root` is set, git is first asked
    /// what it opens there, and the repository is refused unless that is
    /// inside `root` or reached only through what lies outside it (57o):
    /// `shown` says which repository the call meant -- `the repository at
    /// 'x'` as it named it, or the one git finds from the working folder when
    /// it named none. Then its own filter drivers are read, so every later
    /// call switches them off (57o).
    /// Throws GitError when the repository is refused, when the config
    /// cannot be read, or when it names a driver that cannot be switched off.
    Git(std::filesystem::path repo, std::chrono::milliseconds timeout,
        const std::filesystem::path& root, std::string_view shown)
        : repo_{std::move(repo)}, timeout_{timeout} {
        if (!root.empty()) {
            confine(root, shown);
        }
        const ProcessOutcome listed =
            invoke({"config", "--show-scope", "--name-only", "--get-regexp", "^filter\\."});
        // 1 is "no such key"; anything else but 0 is a config git could not
        // read, or a git too old for --show-scope: never run on a guess.
        if (!listed.start_error.empty() || listed.timed_out ||
            (listed.exit_code.value_or(2) != 0 && listed.exit_code.value_or(2) != 1)) {
            const std::string said = rstrip(listed.err.text());
            throw GitError(
                "this repository's config could not be read to switch its filter drivers off" +
                (said.empty() ? std::string{} : ": " + said));
        }
        std::string refusal;
        filters_ = filter_neutralising_options(listed.out.text(), refusal);
        if (!refusal.empty()) {
            throw GitError(refusal);
        }
    }

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
            const std::string what = rstrip(outcome.err.text());
            throw GitError(what.empty()
                               ? "git exited " + std::to_string(outcome.exit_code.value_or(-1))
                               : what);
        }
        const std::string out = rstrip(outcome.out.text());
        return out.empty() ? "(no output)" : out;
    }

    /// Runs git; empty on any failure.
    [[nodiscard]] std::string quiet(std::vector<std::string> arguments) const {
        const ProcessOutcome outcome = invoke(std::move(arguments));
        if (!outcome.start_error.empty() || outcome.timed_out ||
            outcome.exit_code.value_or(1) != 0) {
            return {};
        }
        return rstrip(outcome.out.text());
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
    /// What git opens at `repo_` (57o): `rev-parse` for the git directory,
    /// the common directory and the work tree, or the first two where there
    /// is no work tree (a bare repository, a git directory itself).
    [[nodiscard]] Opened discover() const {
        bool work_tree = true;
        ProcessOutcome outcome =
            invoke({"rev-parse", "--absolute-git-dir", "--git-common-dir", "--show-toplevel"});
        const auto failed = [](const ProcessOutcome& said) {
            return !said.start_error.empty() || said.timed_out || said.exit_code.value_or(1) != 0;
        };
        if (failed(outcome)) {
            work_tree = false;
            outcome = invoke({"rev-parse", "--absolute-git-dir", "--git-common-dir"});
        }
        if (!outcome.start_error.empty()) {
            throw GitError(outcome.start_error);
        }
        if (failed(outcome)) {
            const std::string said = rstrip(outcome.err.text());
            throw GitError(said.empty() ? "git could not open a repository at " + repo_.string()
                                        : said);
        }
        std::vector<std::string> lines;
        std::istringstream in{outcome.out.text()};
        for (std::string line; std::getline(in, line);) {
            if (!line.empty() && line.back() == '\r') {
                line.pop_back();
            }
            lines.push_back(std::move(line));
        }
        Opened opened;
        if (lines.size() != (work_tree ? 3U : 2U) || lines[0].empty() || lines[1].empty()) {
            throw GitError("git did not say which repository it opens at " + repo_.string() +
                           ", so it was not read");
        }
        opened.git_dir = lines[0];
        opened.common_dir = lines[1];
        if (opened.common_dir.is_relative()) {
            // Relative to the folder git ran in.
            opened.common_dir = std::filesystem::absolute(repo_) / opened.common_dir;
        }
        if (work_tree) {
            opened.work_tree = lines[2];
        }
        if (!opened.git_dir.is_absolute() || (work_tree && !opened.work_tree.is_absolute())) {
            throw GitError("git did not say which repository it opens at " + repo_.string() +
                           ", so it was not read");
        }
        return opened;
    }

    /// The first `.git` -- a folder, a file or a link -- that git's own
    /// search from `repo_` meets inside `root`, or nothing when it meets none
    /// there and so leaves the root before finding the repository (57o).
    [[nodiscard]] std::optional<std::filesystem::path> entry_inside(
        const std::filesystem::path& root) const {
        std::string unused;
        std::optional<std::filesystem::path> folder =
            resolve_in_root(root, std::filesystem::absolute(repo_).string(), unused);
        while (folder.has_value()) {
            const std::filesystem::path entry = *folder / ".git";
            std::error_code code;
            if (std::filesystem::exists(std::filesystem::symlink_status(entry, code))) {
                return entry;
            }
            const std::filesystem::path parent = folder->parent_path();
            if (parent == *folder || parent.empty()) {
                break;
            }
            folder = resolve_in_root(root, parent.string(), unused);
        }
        return std::nullopt;
    }

    /// Whether a git directory outside the root, reached through `entry` --
    /// a `.git` file inside it -- names that file's folder as its own work
    /// tree (57o): a linked worktree's `gitdir` back-pointer, or a
    /// submodule's `core.worktree`. Both live in the git directory, outside
    /// the root, where git wrote them when the person made the worktree or
    /// the submodule; a `.git` file pointed at any other repository has none.
    [[nodiscard]] bool vouched(const std::filesystem::path& entry, const Opened& opened) const {
        std::error_code code;
        if (!std::filesystem::is_regular_file(std::filesystem::symlink_status(entry, code))) {
            return false;  // a `.git` link or folder: what it leads to never vouches
        }
        std::ifstream back_file{opened.git_dir / "gitdir", std::ios::binary};
        std::string back;
        if (std::getline(back_file, back)) {
            back = rstrip(back);
            std::filesystem::path named{back};
            if (named.is_relative()) {
                named = opened.git_dir / named;
            }
            if (!back.empty() && same_place(named, entry)) {
                return true;
            }
        }
        const std::string worktree = quiet(
            {"config", "--file", (opened.git_dir / "config").string(), "--get", "core.worktree"});
        if (!worktree.empty()) {
            std::filesystem::path named{worktree};
            if (named.is_relative()) {
                named = opened.git_dir / named;
            }
            return same_place(named, entry.parent_path());
        }
        return false;
    }

    /// Refuses the repository unless what git opens at `repo_` is inside
    /// `root`, or is reached only through what lies outside it (57o) -- the
    /// repository a launch in a subfolder sits in, one the person keeps
    /// outside a configured root -- or through a `.git` file its own git
    /// directory vouches for. A `.git` file, link or folder the model could
    /// have written, a `core.worktree`, a `commondir` or an alternates file
    /// that leads out is refused in the sandbox's words.
    void confine(const std::filesystem::path& root, std::string_view shown) const {
        const Opened opened = discover();
        std::string opens;
        std::filesystem::path through;
        if (lands_inside(root, opened.git_dir)) {
            // Everything in it is the model's to write: all of it inside.
            if (!lands_inside(root, opened.common_dir)) {
                opens = "its objects and refs at " + opened.common_dir.string();
            } else if (!opened.work_tree.empty() && !lands_inside(root, opened.work_tree)) {
                opens = "its work tree at " + opened.work_tree.string();
            } else {
                opens = object_store_leak(root, opened);
            }
        } else if (const std::optional<std::filesystem::path> entry = entry_inside(root);
                   entry.has_value() && !vouched(*entry, opened)) {
            opens = "the repository at " + opened.git_dir.string();
            through = *entry;
        }
        if (!opens.empty()) {
            throw GitError(outside_root(root, shown, opens, through));
        }
    }

    [[nodiscard]] ProcessOutcome invoke(std::vector<std::string> arguments) const {
        platform::ChildCommand command;
        command.program = "git";
        command.arguments = {"-C", repo_.string()};
        for (const std::string_view option : kGitHardening) {
            command.arguments.emplace_back(option);
        }
        command.arguments.insert(command.arguments.end(), filters_.begin(), filters_.end());
        for (std::string& argument : arguments) {
            command.arguments.push_back(std::move(argument));
        }
        return run_to_completion(command, timeout_);
    }

    std::filesystem::path repo_;
    std::chrono::milliseconds timeout_;
    /// `-c filter.<name>.clean=` and its siblings, per driver (57o).
    std::vector<std::string> filters_;
};

/// `command` -- a `diff` or `show` and its arguments -- with the diff
/// hardening put right after the command word, ahead of any `--`.
std::vector<std::string> hardened_diff(std::vector<std::string> command) {
    command.insert(command.begin() + 1, kDiffHardening.begin(), kDiffHardening.end());
    return command;
}

void require_ref(const std::string& ref, std::string_view label) {
    if (!valid_ref(ref)) {
        throw GitError(std::string{label} +
                       " must be a git ref or branch name (letters, digits, and . _ / - only, "
                       "not starting with '-'); got '" +
                       ref + "'");
    }
}

/// Resolves a review ref to something a diff spec can name, fetching per
/// `fetch_mode`.
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

std::span<const std::string_view> git_hardening() noexcept {
    return kGitHardening;
}

std::vector<std::string> filter_neutralising_options(std::string_view scoped_names,
                                                     std::string& refusal) {
    std::set<std::string, std::less<>> drivers;
    std::size_t from = 0;
    while (from < scoped_names.size()) {
        std::size_t end = scoped_names.find('\n', from);
        if (end == std::string_view::npos) {
            end = scoped_names.size();
        }
        std::string_view line = scoped_names.substr(from, end - from);
        from = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        const std::size_t tab = line.find('\t');
        if (tab == std::string_view::npos) {
            continue;
        }
        // The user's own settings -- global, system, the command line -- are
        // theirs, a git-lfs filter among them; a repository's are not.
        const std::string_view scope = line.substr(0, tab);
        if (scope != "local" && scope != "worktree") {
            continue;
        }
        const std::string_view key = line.substr(tab + 1);
        constexpr std::string_view kSection = "filter.";
        const std::size_t last = key.rfind('.');
        if (!key.starts_with(kSection) || last == std::string_view::npos ||
            last <= kSection.size()) {
            continue;
        }
        drivers.emplace(key.substr(kSection.size(), last - kSection.size()));
    }
    std::vector<std::string> options;
    for (const std::string& driver : drivers) {
        if (driver.find('=') != std::string::npos) {
            refusal = "this repository's config defines the filter driver '" + driver +
                      "', whose name the git tools cannot switch off, so it was not read";
            return {};
        }
        for (const std::string_view setting : {"clean=", "smudge=", "process=", "required=false"}) {
            options.emplace_back("-c");
            options.push_back("filter." + driver + "." + std::string{setting});
        }
    }
    return options;
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
    // The root the git tools read inside (57o): the file tools' own.
    std::filesystem::path root =
        options.fs_root.empty() ? options.working_directory : options.fs_root;
    if (root.empty()) {
        std::error_code code;
        root = std::filesystem::current_path(code);
    }
    const auto open = [options, root](const Arguments& args) {
        const std::string hint = args.string("repo");
        if (hint.empty()) {
            // The walk to the nearest repository above the working folder,
            // as before: a launch in a subfolder of a checkout sees it. What
            // git opens there is judged too, so a `.git` written into the
            // root cannot send it elsewhere (57o).
            const std::filesystem::path walked = find_repo("", options.working_directory);
            return Git{walked, options.timeout, root,
                       "the repository git finds from " + walked.string()};
        }
        // A repository the model names must be inside the file tools' root
        // (57o), judged by the sandbox's own resolver and refused in its
        // words -- the folder named, and then what git opens there.
        std::string problem;
        const std::optional<std::filesystem::path> repo = resolve_in_root(root, hint, problem);
        if (!repo.has_value()) {
            throw GitError(problem);
        }
        return Git{*repo, options.timeout, root, "the repository at '" + hint + "'"};
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
                   "  (default: " + git.default_branch() + ")\n\n" +
                   git.run({"status", "--ignore-submodules=dirty"});
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
            return header + git.run(hardened_diff(std::move(command)));
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
            return git.run(hardened_diff({"show", "--stat", ref}));
        });
    };
    registry.add(show);
}

}  // namespace apogee::tools
