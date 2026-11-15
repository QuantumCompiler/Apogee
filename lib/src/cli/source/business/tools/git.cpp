#include "tools/git.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <charconv>
#include <fstream>
#include <map>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <vector>

#include "contracts/host.h"
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

/// Why a `git config` listing the tools judge was not judged (57x): longer
/// than the 256 KiB of a child's output they keep, so its start -- a driver
/// or a command set before any padding -- was not read.
constexpr std::string_view kListedTooMuch =
    "this repository's config lists more than the tools read whole";

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
        // Only the last 256 KiB of a child's output is kept, so a listing
        // padded past that would lose the drivers named first (57x).
        if (listed.out.omitted() > 0) {
            throw GitError(std::string{kListedTooMuch} +
                           ", so its filter drivers could not all be switched off");
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

    /// Runs git and hands back what happened, whatever it was.
    [[nodiscard]] ProcessOutcome attempt(std::vector<std::string> arguments) const {
        return invoke(std::move(arguments));
    }

    /// The remotes `git remote` lists, one per line: a name is the whole
    /// line, so a configured name holding a space is never read as two.
    [[nodiscard]] std::set<std::string> remotes() const {
        std::set<std::string> out;
        std::istringstream in{quiet({"remote"})};
        for (std::string name; std::getline(in, name);) {
            if (!name.empty() && name.back() == '\r') {
                name.pop_back();
            }
            if (!name.empty()) {
                out.insert(name);
            }
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

    /// The folder git reads a relative remote URL against (57x): the work
    /// tree's top, where a command git sets up runs, or a bare repository's
    /// own folder.
    [[nodiscard]] std::filesystem::path setup_folder() const {
        for (const std::string_view asked : {"--show-toplevel", "--absolute-git-dir"}) {
            if (std::string folder = quiet({"rev-parse", std::string{asked}}); !folder.empty()) {
                return folder;
            }
        }
        return std::filesystem::absolute(repo_);
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
        // No object a read needs is fetched from a promisor remote (57x): a
        // partial clone's lazy fetch runs `git fetch <remote>` by name, with
        // that remote's `uploadpack` and URL, from any command that misses an
        // object. Read since git 2.44; an older git ignores it. The person's
        // own value is dropped first: an added variable comes after the
        // inherited ones and git reads the first of two, so their `0` --
        // git's way to allow lazy fetches again -- would win.
        command.unset_environment = {"GIT_NO_LAZY_FETCH"};
        command.extra_environment = {{"GIT_NO_LAZY_FETCH", "1"}};
        if (!arguments.empty() && arguments.front() == "config") {
            // The person's GIT_CONFIG has `git config` read that one file
            // alone, while every other git command ignores it and reads the
            // repository's own config (57x): the tools' own config reads --
            // the filter drivers and the settings a fetch would run --
            // drop it, so they list what the commands they guard will read.
            command.unset_environment.emplace_back("GIT_CONFIG");
        }
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

/// The transports the review fetch uses, said where it refuses another.
constexpr std::string_view kTransports =
    "which the review fetch does not use -- it fetches over https, http, ssh or git, or from a "
    "folder inside the root";

/// The gate's words for a host it refused (57x), with how to allow one.
std::string host_refusal(const std::string& host) {
    return "permission to reach " + host +
           " was not given, so nothing was sent to it. A website is allowed with --allow-host " +
           host + " where the command takes it (chat, execute, complete), with config " +
           "add-allowed-host " + host + ", or by answering always when asked";
}

/// Why git would read a LOCAL or `file://` folder path as a different folder
/// than the one judged (57x, round 5), or empty. git's `parse_connect_url`
/// runs `host_end()` on a local path too, so an `@[` anywhere -- or a leading
/// `[` -- names a bracketed host and git enters the path after the matching
/// `]`, not the path handed in (`q@[y]/z` -> `]/z`; `/p/q@[y]/z` -> `]/z`
/// relative to the fetch's cwd; a `file://` URL the tool strips to that same
/// bare path). So a planted `.git` suffix outside the root is reached though
/// the judged path is a repository inside it. The path git enters must be the
/// path judged, so one git would split is refused before git runs. (A
/// relative leading `[` is made safe by resolving against the root, but is
/// refused all the same -- the cost is a folder path holding `@[` or opening
/// with `[`, which no fetch reads.)
inline std::string local_path_host_split(std::string_view path) {
    const bool leading = path.starts_with('[');
    if (!leading && path.find("@[") == std::string_view::npos) {
        return {};
    }
    return std::string{"its path "} + (leading ? "begins with '['" : "holds '@['") +
           ", which git reads as a bracketed host, so git would enter a folder other than the one "
           "judged";
}

/// How one review fetch ended (57x).
struct FetchOutcome {
    /// git fetched the branch.
    bool fetched = false;
    /// Why no fetch was made, or empty: a remote that is not fetched from, a
    /// host the gate refused. Git's own failure leaves it empty.
    std::string refusal{};
    /// The refusal is of the call's own `remote`, which is no remote: the
    /// call is refused whatever is already here.
    bool not_a_remote = false;
};

/// The one review fetch (57x). Every fetch `git_diff`'s and `git_log`'s
/// review forms make -- a ref named alone, or qualified by its remote's name
/// -- goes through `fetch`, which judges the remote before git runs and then
/// fetches by the URL it judged. One per call: each remote is judged once,
/// and each host asked about once.
class ReviewFetch {
public:
    ReviewFetch(const Git& git, std::filesystem::path root, std::chrono::milliseconds timeout,
                const agent::TargetGate& gate)
        : git_{git}, root_{std::move(root)}, timeout_{timeout}, gate_{gate} {}

    /// Fetches `branch` from `remote` into `refs/remotes/<remote>/<branch>`.
    [[nodiscard]] FetchOutcome fetch(const std::string& remote, const std::string& branch) {
        if (!remotes().contains(remote)) {
            return FetchOutcome{.refusal = "'" + remote +
                                           "' is not a remote this repository has -- the review "
                                           "fetch reads from a configured remote only",
                                .not_a_remote = true};
        }
        const Source& from = source(remote);
        if (!from.refusal.empty()) {
            return FetchOutcome{.refusal = from.refusal};
        }
        if (!from.host.empty()) {
            // A website, asked about by its host before anything is sent --
            // once per call, though both refs may need it.
            auto answered = hosts_.find(from.host);
            if (answered == hosts_.end()) {
                const bool allowed = gate_ && gate_(from.host, from.shown);
                answered = hosts_.emplace(from.host, allowed).first;
            }
            if (!answered->second) {
                return FetchOutcome{.refusal = host_refusal(from.host)};
            }
        }
        // Only the judged transport; no redirect followed, so the host asked
        // about is the host reached -- unless the person's own config sets
        // `http.<url>.followRedirects` for this URL, which git ranks above
        // the bare `-c` (theirs, as tools.md says; a repository's own is
        // refused by name); no bundle, and no protocol-v2 packfile URI, a
        // server names elsewhere. Each `-c` outranks the repository's
        // own value, and a per-protocol one is set for every transport git
        // ships -- its built-ins and each `git-remote-*` helper, `ftp` and
        // `ftps` among them (`fetch_fence_transports`) -- since a
        // repository's `protocol.<name>.allow` outranks a bare
        // `protocol.allow`.
        std::vector<std::string> arguments{"-c", "protocol.allow=never"};
        for (const std::string_view transport : fetch_fence_transports()) {
            arguments.emplace_back("-c");
            arguments.push_back("protocol." + std::string{transport} +
                                (transport == from.transport ? ".allow=always" : ".allow=never"));
        }
        // `fetch.uriProtocols=` empty: git advertises no `packfile-uris`
        // capability, so a server cannot name a pack on another host for `git
        // http-fetch` to download under the judged transport's `.allow=always`
        // (57x). No `fetch.<url>.` variant exists, so the bare `-c` wins
        // outright -- unlike `http.followRedirects`, which a repository's own
        // config is refused by name for.
        for (const std::string_view option :
             {"-c", "http.followRedirects=false", "-c", "transfer.bundleURI=false", "-c",
              "fetch.uriProtocols=", "fetch", "--no-recurse-submodules", "--no-tags", "--no-prune",
              "--no-auto-maintenance", "--end-of-options"}) {
            arguments.emplace_back(option);
        }
        arguments.push_back(from.url);
        arguments.push_back("+refs/heads/" + branch + ":refs/remotes/" + remote + "/" + branch);
        const ProcessOutcome outcome = git_.attempt(std::move(arguments));
        return FetchOutcome{.fetched = outcome.start_error.empty() && !outcome.timed_out &&
                                       outcome.exit_code.value_or(1) == 0};
    }

private:
    /// A remote as judged: the URL git is handed, or why it is not.
    struct Source {
        std::string refusal{};
        std::string url{};
        std::string transport{};
        /// A network remote's host; empty for a folder.
        std::string host{};
        std::string shown{};
    };

    [[nodiscard]] const std::set<std::string>& remotes() {
        if (!remotes_.has_value()) {
            remotes_ = git_.remotes();
        }
        return *remotes_;
    }

    /// Why the repository's own settings refuse every fetch, or empty: read
    /// once, local and worktree scope only -- the keys a fetch would run or
    /// follow, then the header settings it would send, read with their values.
    [[nodiscard]] const std::string& settings_refusal() {
        if (settings_.has_value()) {
            return *settings_;
        }
        std::string why;
        const std::optional<std::string> names =
            listed({"config", "--show-scope", "--name-only", "--get-regexp",
                    "^(core|credential|url|https?|fetch)\\."},
                   why);
        settings_ = names.has_value() ? fetch_setting_refusal(*names) : why;
        if (settings_->empty()) {
            const std::optional<std::string> headers =
                listed({"config", "-z", "--show-scope", "--get-regexp",
                        "^http\\.(.*\\.)?(extraheader|useragent)$"},
                       why);
            settings_ = headers.has_value() ? fetch_header_refusal(*headers) : why;
        }
        return *settings_;
    }

    /// What a `git config` listing printed, or nothing, with `why`, for a
    /// config it could not read whole: 1 is "no such key"; anything else but
    /// 0, or a listing longer than the 256 KiB of a child's output the tools
    /// keep (57x), is never fetched on a guess.
    [[nodiscard]] std::optional<std::string> listed(std::vector<std::string> listing,
                                                    std::string& why) const {
        const ProcessOutcome outcome = git_.attempt(std::move(listing));
        const int code = outcome.exit_code.value_or(2);
        if (!outcome.start_error.empty() || outcome.timed_out || (code != 0 && code != 1)) {
            why =
                "this repository's config could not be read for the settings a fetch would run "
                "or follow";
            return std::nullopt;
        }
        if (outcome.out.omitted() > 0) {
            why = std::string{kListedTooMuch} +
                  ", so the settings a fetch would run or follow could not be judged";
            return std::nullopt;
        }
        return std::string{outcome.out.text()};
    }

    /// The remote, judged once.
    [[nodiscard]] const Source& source(const std::string& remote) {
        if (const auto known = sources_.find(remote); known != sources_.end()) {
            return known->second;
        }
        Source from = judge(remote);
        if (!from.refusal.empty()) {
            from.url.clear();
            from.host.clear();
        }
        return sources_.emplace(remote, std::move(from)).first->second;
    }

    /// Judges a remote: its name, the repository's settings, then its URL --
    /// read as data, with the person's own rewrites applied -- and where
    /// that URL leads.
    [[nodiscard]] Source judge(const std::string& remote) {
        Source from;
        const auto refused = [&remote, &from](const std::string& why) {
            from.refusal = "the remote '" + remote + "' is not fetched from: " + why;
            return from;
        };
        if (!valid_ref(remote) || remote.find("..") != std::string::npos) {
            return refused("its name cannot name a branch under refs/remotes");
        }
        if (const std::string& settings = settings_refusal(); !settings.empty()) {
            from.refusal = settings;
            return from;
        }
        const std::string url = git_.quiet({"remote", "get-url", remote});
        if (url.empty()) {
            return refused("it has no URL");
        }
        std::string why;
        const std::optional<FetchUrl> parsed = parse_fetch_url(url, why);
        if (!parsed.has_value()) {
            return refused(why);
        }
        if (parsed->kind == FetchUrl::Kind::Local) {
            judge_folder(remote, *parsed, from);
            if (!from.refusal.empty()) {
                return from;
            }
        } else {
            from.url = parsed->url;
            from.transport = parsed->transport;
            from.host = parsed->host;
            from.shown = parsed->shown;
        }
        if (remotes().contains(from.url)) {
            // `git fetch <url>` reads the settings of a remote named as that
            // URL: its own URL, its uploadpack.
            return refused("a remote of this repository is named '" + from.shown +
                           "', so git would read that remote's settings for its URL");
        }
        if (git_.quiet({"ls-remote", "--get-url", "--end-of-options", from.url}) != from.url) {
            // The person's own rewrite was applied once already; one that
            // would apply again sends git somewhere nobody judged.
            return refused(
                "git would rewrite its URL again (a url.<base>.insteadOf setting), so the "
                "address it reaches is not the one judged");
        }
        return from;
    }

    /// A folder remote: inside the root, its repository confined as `repo`
    /// is (57o), and handed to git as the path judged.
    void judge_folder(const std::string& remote, const FetchUrl& parsed, Source& from) const {
        std::filesystem::path path{parsed.url};
        if (path.is_relative()) {
            // Relative to the folder git's fetch runs in, as git reads it.
            path = git_.setup_folder() / path;
        }
        const std::string shown = "the remote '" + remote + "' at '" + parsed.shown + "'";
        std::string problem;
        const std::optional<std::filesystem::path> resolved =
            resolve_in_root(root_, path.string(), problem);
        if (!resolved.has_value()) {
            from.refusal = outside_root(
                root_, shown, "the repository at " + path.lexically_normal().string(), {});
            return;
        }
        std::filesystem::path top;
        try {
            const Git confined{*resolved, timeout_, root_, shown};
            top = confined.setup_folder();
        } catch (const GitError& e) {
            from.refusal = e.what();
            return;
        }
        if (!same_place(top, *resolved)) {
            // git's local upload-pack enters a folder non-strictly -- `P/.git`,
            // `P`, `P.git/.git`, `P.git` -- so a plain folder judged at `P`
            // serves the repository git reaches by walking up or by a `.git`
            // suffix a `write_file` could have planted, not the one confined.
            // The fetch must reach exactly the repository at the path judged,
            // so a folder that is not itself a repository's top (or git
            // directory) is refused (57x).
            from.refusal = "the remote '" + remote + "' at '" + parsed.shown +
                           "' is not a git repository: git would serve " + top.string() +
                           " instead, which is not the folder judged. Point the remote at a "
                           "repository's own folder inside the root.";
            return;
        }
        if (const std::string split = local_path_host_split(resolved->string()); !split.empty()) {
            // The backstop: the path git is handed is the one judged even when
            // the `@[` is not in the remote's URL (which `parse_fetch_url`
            // already refuses) -- a link inside the root to a folder holding
            // one, since `resolve_in_root` returns the path with links
            // followed (`link/z`, `x/link` a link to `x/q@[y]`), or the root's
            // own path.
            from.refusal = "the remote '" + remote + "' at '" + parsed.shown +
                           "' is not fetched from: " + split;
            return;
        }
        from.url = resolved->string();
        from.transport = "file";
        from.shown = parsed.shown;
    }

    const Git& git_;
    std::filesystem::path root_;
    std::chrono::milliseconds timeout_;
    const agent::TargetGate& gate_;
    std::optional<std::set<std::string>> remotes_;
    std::optional<std::string> settings_;
    std::map<std::string, Source> sources_;
    std::map<std::string, bool> hosts_;
};

/// Says, once, why a review read what was already here (57x).
void note_not_fetched(std::vector<std::string>& notes, const std::string& why) {
    if (why.empty()) {
        return;
    }
    const std::string line = "Not fetched: " + why;
    if (std::ranges::find(notes, line) == notes.end()) {
        notes.push_back(line);
    }
}

/// A ref qualified by where it lives: `<remote>/<branch>`, fetched from that
/// remote, or `refs/...`, never fetched.
std::string resolve_qualified_ref(const Git& git, ReviewFetch& review_fetch, const std::string& ref,
                                  const std::string& first, bool do_fetch,
                                  std::vector<std::string>& notes) {
    std::string not_fetched;
    if (do_fetch && !ref.starts_with("refs/") && ref.size() > first.size() + 1) {
        not_fetched = review_fetch.fetch(first, ref.substr(first.size() + 1)).refusal;
    }
    if (git.is_local_ref(ref)) {
        note_not_fetched(notes, not_fetched);
        return ref;
    }
    std::string why;
    if (!not_fetched.empty()) {
        why = ", and it was not fetched: " + not_fetched;
    } else if (!do_fetch) {
        why = " (fetching disabled -- use fetch: always)";
    }
    throw GitError("ref '" + ref + "' could not be resolved" + why);
}

/// Resolves a review ref to something a diff spec can name, fetching per
/// `fetch_mode` through the one review fetch (57x). A fetch that was not
/// made leaves the review to what is already here, said in `notes`; a
/// `remote` that is no remote refuses the call.
std::string resolve_review_ref(const Git& git, ReviewFetch& review_fetch, const std::string& ref,
                               const std::string& remote, const std::string& fetch_mode,
                               std::vector<std::string>& notes) {
    const std::string first = ref.substr(0, ref.find('/'));
    const bool local = git.is_local_ref(ref);
    const bool do_fetch = fetch_mode == "always" || (fetch_mode != "never" && !local);
    if (ref.starts_with("refs/") || git.remotes().contains(first)) {
        return resolve_qualified_ref(git, review_fetch, ref, first, do_fetch, notes);
    }
    std::string not_fetched;
    if (do_fetch) {
        const FetchOutcome outcome = review_fetch.fetch(remote, ref);
        if (outcome.not_a_remote) {
            throw GitError(outcome.refusal);
        }
        not_fetched = outcome.refusal;
        if (outcome.fetched) {
            for (const std::string& candidate : {remote + "/" + ref, std::string{"FETCH_HEAD"}}) {
                if (git.is_local_ref(candidate)) {
                    return candidate;
                }
            }
        } else if (not_fetched.empty() && git.is_local_ref(remote + "/" + ref)) {
            // git's own failure: the remote's branch as last fetched, as before.
            return remote + "/" + ref;
        }
    }
    if (git.is_local_ref(ref)) {
        note_not_fetched(notes, not_fetched);
        return ref;
    }
    if (!not_fetched.empty()) {
        throw GitError("ref '" + ref +
                       "' is not in this repository, and it was not fetched from '" + remote +
                       "': " + not_fetched);
    }
    if (fetch_mode == "never") {
        throw GitError("ref '" + ref +
                       "' not found locally and fetching is disabled (fetch: never); fetch it "
                       "first or allow fetching");
    }
    throw GitError("ref '" + ref + "' could not be resolved locally or from remote '" + remote +
                   "'");
}

/// The lines a review's header carries for fetches not made.
std::string with_notes(const std::vector<std::string>& notes) {
    std::string out;
    for (const std::string& note : notes) {
        out += note + "\n";
    }
    return out;
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

namespace {

bool scheme_char(char c, bool first) {
    const auto byte = static_cast<unsigned char>(c);
    return std::isalpha(byte) != 0 ||
           (!first && (std::isdigit(byte) != 0 || c == '+' || c == '-' || c == '.'));
}

std::string lowercase(std::string_view text) {
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        out.push_back(static_cast<char>(std::tolower(static_cast<unsigned char>(c))));
    }
    return out;
}

/// A host and its port as written between `@` and the path, judged: the
/// host canonical, never beginning with `-`, the port 1-65535. Brackets hold
/// an IPv6 literal -- or, scp-like, `host:port`.
struct Endpoint {
    std::string host;
    std::string port;
    bool bracketed = false;
};

std::optional<Endpoint> judge_endpoint(std::string_view written, std::string& refusal) {
    Endpoint out;
    std::string_view host = written;
    std::string_view port;
    if (host.starts_with('[')) {
        const std::size_t close = host.find(']');
        if (close == std::string_view::npos) {
            refusal = "its host '" + std::string{written} + "' is not a host name";
            return std::nullopt;
        }
        const std::string_view after = host.substr(close + 1);
        host = host.substr(1, close - 1);
        out.bracketed = true;
        if (!after.empty()) {
            if (!after.starts_with(':')) {
                refusal = "its host '" + std::string{written} + "' is not a host name";
                return std::nullopt;
            }
            port = after.substr(1);
            if (port.empty()) {
                refusal = "its port is empty";
                return std::nullopt;
            }
        } else if (!harness::canonical_host(host).has_value()) {
            // `[host:port]`, an scp-like address's way to name a port.
            if (const std::size_t colon = host.rfind(':'); colon != std::string_view::npos) {
                port = host.substr(colon + 1);
                host = host.substr(0, colon);
            }
        }
    } else if (const std::size_t colon = host.find(':'); colon != std::string_view::npos) {
        port = host.substr(colon + 1);
        host = host.substr(0, colon);
        if (port.empty()) {
            refusal = "its port is empty";
            return std::nullopt;
        }
    }
    if (host.starts_with('-')) {
        refusal = "its host '" + std::string{host} +
                  "' begins with '-', which ssh would read as an option";
        return std::nullopt;
    }
    const std::optional<std::string> canonical = harness::canonical_host(host);
    if (!canonical.has_value()) {
        refusal = "its host '" + std::string{host} +
                  "' is not a host name a website can be asked "
                  "about by";
        return std::nullopt;
    }
    if (!port.empty()) {
        unsigned value = 0;
        const auto [end, failed] = std::from_chars(port.data(), port.data() + port.size(), value);
        if (failed != std::errc{} || end != port.data() + port.size() || value == 0 ||
            value > 65535 || port.size() > 5) {
            refusal = "its port '" + std::string{port} + "' is not a port";
            return std::nullopt;
        }
        out.port = std::string{port};
    }
    out.host = *canonical;
    return out;
}

/// `user[:password]@`, judged: one `@` at most, no bracket, no user beginning
/// with `-`. The written form for git, and the shown form with any password
/// masked.
bool judge_userinfo(std::string_view& authority, std::string& written, std::string& shown,
                    std::string& refusal) {
    const std::size_t at = authority.find('@');
    if (at == std::string_view::npos) {
        return true;
    }
    if (authority.find('@', at + 1) != std::string_view::npos) {
        refusal =
            "its address holds more than one '@' before its host, so the host cannot be "
            "judged";
        return false;
    }
    const std::string_view userinfo = authority.substr(0, at);
    if (userinfo.find_first_of("[]") != std::string_view::npos) {
        // git's ssh:// and git:// read an address that starts with `[` as a
        // bracketed host and drop what follows its `]`: `[other]@good`
        // reaches `other`. No user part may hold a bracket, so one that does
        // is refused rather than judged by the host after its `@` (57x).
        refusal =
            "its user part holds a '[' or ']', which git's ssh and git transports read back "
            "into the host, so the host cannot be judged";
        return false;
    }
    authority = authority.substr(at + 1);
    const std::size_t colon = userinfo.find(':');
    const std::string_view user = userinfo.substr(0, colon);
    if (user.starts_with('-')) {
        refusal = "its user name begins with '-', which ssh would read as an option";
        return false;
    }
    written = std::string{userinfo} + "@";
    shown = std::string{user} + (colon == std::string_view::npos ? "" : ":***") + "@";
    return true;
}

std::string host_part(const Endpoint& endpoint) {
    return endpoint.host.find(':') != std::string::npos ? "[" + endpoint.host + "]" : endpoint.host;
}

}  // namespace

namespace {

/// `<scheme>://...`: a folder by `file://`, or a network URL rebuilt from its
/// judged parts.
std::optional<FetchUrl> scheme_url(std::string_view url, std::size_t scheme_end,
                                   std::string& refusal) {
    // Read in any case, rebuilt in lower case: `HTTPS://` is https to the
    // judgment and to git alike, never a helper named `HTTPS`.
    const std::string scheme = lowercase(url.substr(0, scheme_end));
    const std::string_view rest = url.substr(scheme_end + 3);
    FetchUrl out;
    if (scheme == "file") {
        if (!rest.starts_with('/')) {
            refusal = "its file:// URL names a host, so the folder cannot be judged";
            return std::nullopt;
        }
        if (rest.find('%') != std::string_view::npos) {
            refusal = "its file:// URL holds a percent escape, so the folder cannot be judged";
            return std::nullopt;
        }
        if (std::string split = local_path_host_split(rest); !split.empty()) {
            refusal = std::move(split);
            return std::nullopt;
        }
        out.kind = FetchUrl::Kind::Local;
        out.transport = "file";
        out.url = std::string{rest};
        out.shown = std::string{url};
        return out;
    }
    if (scheme != "https" && scheme != "http" && scheme != "ssh" && scheme != "git") {
        refusal = "its URL uses the transport " + scheme + "://, " + std::string{kTransports};
        return std::nullopt;
    }
    const std::size_t slash = rest.find('/');
    std::string_view authority = rest.substr(0, slash);
    const std::string_view path =
        slash == std::string_view::npos ? std::string_view{} : rest.substr(slash);
    if ((scheme == "ssh" || scheme == "git") &&
        path.find_first_of("@[]%") != std::string_view::npos) {
        // git's parse_connect_url scans the whole URL for `@[` to split the
        // host (percent-decoding an ssh:// URL first), and git:// looks up the
        // bare name that precedes a path `@`. A `@`, `[`, `]` or `%` in the
        // path would be read back into the host, so a different host than the
        // one judged is reached: refuse it rather than judge a host git will
        // not use.
        refusal = "its " + scheme +
                  ":// path holds a '@', '[', ']' or '%' that git would read back into the host, "
                  "so the host cannot be judged";
        return std::nullopt;
    }
    if (authority.find_first_of("\\%? #") != std::string_view::npos) {
        refusal = "its address '" + std::string{authority} +
                  "' holds a character a host name cannot, so the host cannot be judged";
        return std::nullopt;
    }
    std::string userinfo;
    std::string shown_userinfo;
    if (!judge_userinfo(authority, userinfo, shown_userinfo, refusal)) {
        return std::nullopt;
    }
    const std::optional<Endpoint> endpoint = judge_endpoint(authority, refusal);
    if (!endpoint.has_value()) {
        return std::nullopt;
    }
    const std::string address =
        host_part(*endpoint) + (endpoint->port.empty() ? "" : ":" + endpoint->port);
    out.kind = FetchUrl::Kind::Network;
    out.transport = scheme;
    out.host = endpoint->host;
    out.url = scheme + "://" + userinfo + address + std::string{path};
    out.shown = scheme + "://" + shown_userinfo + address + std::string{path};
    return out;
}

/// scp-like `[user@]host:path`, ssh: the path starts at the first `:` after
/// the host -- after its bracket, when a bracket holds a `:` -- as git reads
/// it.
std::optional<FetchUrl> scp_like(std::string_view url, std::size_t first_colon,
                                 std::string& refusal) {
    std::size_t host_from = 0;
    if (const std::size_t at_bracket = url.find("@["); at_bracket != std::string_view::npos) {
        host_from = at_bracket + 1;
    }
    std::size_t colon = first_colon;
    if (url.substr(host_from).starts_with('[')) {
        const std::size_t close = url.find(']', host_from);
        if (close == std::string_view::npos) {
            refusal = "its address has an unclosed '[', so the host cannot be judged";
            return std::nullopt;
        }
        colon = url.find(':', close);
        if (colon != close + 1) {
            refusal = "its address has no ':' right after its host, so the host cannot be judged";
            return std::nullopt;
        }
    }
    if (url.find('/') == std::string_view::npos) {
        refusal = "its scp-style address has no '/', so git could read it as a remote's name";
        return std::nullopt;
    }
    std::string_view authority = url.substr(0, colon);
    const std::string_view path = url.substr(colon + 1);
    if (authority.find_first_of("\\%/ ") != std::string_view::npos) {
        refusal = "its address '" + std::string{authority} +
                  "' holds a character a host name cannot, so the host cannot be judged";
        return std::nullopt;
    }
    std::string userinfo;
    std::string shown_userinfo;
    if (!judge_userinfo(authority, userinfo, shown_userinfo, refusal)) {
        return std::nullopt;
    }
    const std::optional<Endpoint> endpoint = judge_endpoint(authority, refusal);
    if (!endpoint.has_value()) {
        return std::nullopt;
    }
    std::string address = host_part(*endpoint);
    if (!endpoint->port.empty()) {
        address = "[" + endpoint->host + ":" + endpoint->port + "]";
    } else if (endpoint->bracketed && endpoint->host.find(':') == std::string::npos) {
        address = "[" + endpoint->host + "]";
    }
    FetchUrl out;
    out.kind = FetchUrl::Kind::Network;
    out.transport = "ssh";
    out.host = endpoint->host;
    out.url = userinfo + address + ":" + std::string{path};
    out.shown = shown_userinfo + address + ":" + std::string{path};
    return out;
}

}  // namespace

std::optional<FetchUrl> parse_fetch_url(std::string_view url, std::string& refusal) {
    if (url.empty()) {
        refusal = "it has no URL";
        return std::nullopt;
    }
    if (std::ranges::any_of(url, [](char c) {
            const auto byte = static_cast<unsigned char>(c);
            return byte < 0x20 || byte == 0x7f;
        })) {
        refusal = "its URL holds a control character, so it cannot be judged";
        return std::nullopt;
    }
    if (url.front() == '-') {
        refusal = "its URL begins with '-', which git would read as an option";
        return std::nullopt;
    }
    std::size_t scheme_end = 0;
    while (scheme_end < url.size() && scheme_char(url[scheme_end], scheme_end == 0)) {
        ++scheme_end;
    }
    const std::string_view after_scheme = url.substr(scheme_end);
    if (scheme_end > 0 && after_scheme.starts_with("::")) {
        // A transport git hands to a helper program: `ext::`, `fd::`, a
        // `<vcs>::` remote helper.
        refusal = "its URL uses the transport " + std::string{url.substr(0, scheme_end)} + "::, " +
                  std::string{kTransports};
        return std::nullopt;
    }
    if (scheme_end > 0 && after_scheme.starts_with("://")) {
        return scheme_url(url, scheme_end, refusal);
    }
    // Not a URL: a folder, unless git reads it as scp-like -- a `:` with no
    // `/` before it.
    const std::size_t first_colon = url.find(':');
    const std::size_t first_slash = url.find('/');
    if (first_colon == std::string_view::npos ||
        (first_slash != std::string_view::npos && first_slash < first_colon)) {
        if (std::string split = local_path_host_split(url); !split.empty()) {
            refusal = std::move(split);
            return std::nullopt;
        }
        FetchUrl out;
        out.kind = FetchUrl::Kind::Local;
        out.transport = "file";
        out.url = std::string{url};
        out.shown = out.url;
        return out;
    }
    return scp_like(url, first_colon, refusal);
}

namespace {

/// Whether a setting takes a subsection.
enum class Subsection : std::uint8_t { None, Allowed, Required };

/// A repository setting a fetch would run or follow (57x), keyed by section
/// and variable as git prints them (lower case).
struct FetchSetting {
    std::string_view section;
    std::string_view variable;
    /// The variable as git's documentation spells it.
    std::string_view spelled;
    Subsection subsection = Subsection::None;
    /// What it does to a fetch, in the refusal's words.
    std::string_view does;
};

constexpr std::string_view kRunsCommand = "a command git would run to fetch";
constexpr std::string_view kProxy = "a proxy the fetch would go through";
constexpr std::string_view kBundle = "a place the fetch would download from first";

constexpr std::string_view kCookieFile = "a file the fetch would write cookies back to";

constexpr std::array<FetchSetting, 13> kFetchSettings{{
    {.section = "core", .variable = "sshcommand", .spelled = "sshCommand", .does = kRunsCommand},
    {.section = "core", .variable = "gitproxy", .spelled = "gitProxy", .does = kRunsCommand},
    {.section = "core", .variable = "askpass", .spelled = "askPass", .does = kRunsCommand},
    {.section = "core",
     .variable = "alternaterefscommand",
     .spelled = "alternateRefsCommand",
     .does = kRunsCommand},
    {.section = "credential",
     .variable = "helper",
     .spelled = "helper",
     .subsection = Subsection::Allowed,
     .does = kRunsCommand},
    {.section = "url",
     .variable = "insteadof",
     .spelled = "insteadOf",
     .subsection = Subsection::Required,
     .does = "a rewrite of the URL git would fetch from"},
    {.section = "http",
     .variable = "proxy",
     .spelled = "proxy",
     .subsection = Subsection::Allowed,
     .does = kProxy},
    {.section = "https",
     .variable = "proxy",
     .spelled = "proxy",
     .subsection = Subsection::Allowed,
     .does = kProxy},
    {.section = "http",
     .variable = "curloptresolve",
     .spelled = "curloptResolve",
     .subsection = Subsection::Allowed,
     .does = "an address the remote's host name would be read as"},
    {.section = "http",
     .variable = "followredirects",
     .spelled = "followRedirects",
     .subsection = Subsection::Allowed,
     .does = "whether the fetch follows a server that sends it elsewhere"},
    {.section = "http",
     .variable = "cookiefile",
     .spelled = "cookieFile",
     .subsection = Subsection::Allowed,
     .does = kCookieFile},
    {.section = "http",
     .variable = "savecookies",
     .spelled = "saveCookies",
     .subsection = Subsection::Allowed,
     .does = kCookieFile},
    {.section = "fetch", .variable = "bundleuri", .spelled = "bundleURI", .does = kBundle},
}};

/// The refusal for one key a repository's own config sets, or empty when a
/// fetch neither runs nor follows it.
std::string fetch_setting_named(std::string_view key) {
    const std::size_t first = key.find('.');
    const std::size_t last = key.rfind('.');
    if (first == std::string_view::npos) {
        return {};
    }
    const std::string section = lowercase(key.substr(0, first));
    const std::string variable = lowercase(key.substr(last + 1));
    // Three parts, the middle one possibly empty: git honours `[url ""]` and
    // lists its rewrite as `url..insteadof` (57x), so an empty subsection is a
    // subsection, never none.
    const bool has_subsection = last > first;
    const std::string_view subsection =
        has_subsection ? key.substr(first + 1, last - first - 1) : std::string_view{};
    for (const FetchSetting& known : kFetchSettings) {
        const bool shaped = has_subsection ? known.subsection != Subsection::None
                                           : known.subsection != Subsection::Required;
        if (section != known.section || variable != known.variable || !shaped) {
            continue;
        }
        return "the repository's own config sets " + std::string{known.section} + "." +
               (has_subsection ? std::string{subsection} + "." : std::string{}) +
               std::string{known.spelled} + ", " + std::string{known.does} +
               "; fetch it yourself, or remove the setting";
    }
    return {};
}

}  // namespace

std::string fetch_setting_refusal(std::string_view scoped_names) {
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
        // The person's own settings -- global, system, the command line --
        // are theirs; a repository's, an included file's among them, are not.
        if (tab == std::string_view::npos ||
            (line.substr(0, tab) != "local" && line.substr(0, tab) != "worktree")) {
            continue;
        }
        if (std::string refusal = fetch_setting_named(line.substr(tab + 1)); !refusal.empty()) {
            return refusal;
        }
    }
    return {};
}

std::string fetch_header_refusal(std::string_view scoped_values) {
    std::size_t from = 0;
    while (from < scoped_values.size()) {
        // `-z`: the scope, a NUL, then the key, a newline and the value, a NUL.
        const std::size_t scope_end = scoped_values.find('\0', from);
        if (scope_end == std::string_view::npos) {
            break;
        }
        std::size_t entry_end = scoped_values.find('\0', scope_end + 1);
        if (entry_end == std::string_view::npos) {
            entry_end = scoped_values.size();
        }
        const std::string_view scope = scoped_values.substr(from, scope_end - from);
        const std::string_view entry =
            scoped_values.substr(scope_end + 1, entry_end - scope_end - 1);
        from = entry_end + 1;
        // The person's own are theirs, as with every setting the fetch reads.
        if (scope != "local" && scope != "worktree") {
            continue;
        }
        const std::size_t newline = entry.find('\n');
        const std::string_view key = entry.substr(0, newline);
        const std::string_view value =
            newline == std::string_view::npos ? std::string_view{} : entry.substr(newline + 1);
        const std::size_t dot = key.rfind('.');
        const std::string variable = lowercase(key.substr(dot + 1));
        if (dot == std::string_view::npos || !key.starts_with("http.") ||
            (variable != "extraheader" && variable != "useragent")) {
            continue;
        }
        // Named as git's documentation spells it, `http.<url>.` kept; the
        // value is never repeated, since a header may carry a credential.
        const std::string named = std::string{key.substr(0, dot + 1)} +
                                  (variable == "extraheader" ? "extraHeader" : "userAgent");
        // A line break would start a header line of its own -- a second Host
        // among them -- and git hands the value on as it is.
        if (std::ranges::any_of(value, [](char c) {
                const auto byte = static_cast<unsigned char>(c);
                return byte < 0x20 || byte == 0x7f;
            })) {
            return "the repository's own config sets " + named +
                   " to a value holding a control character, which would start a header line of "
                   "its own; fetch it yourself, or remove the setting";
        }
        // A Host header names the site the request asks the address for,
        // which a front serving several sites from one address -- a shared
        // host, a CDN edge, a reverse proxy -- hands to that site.
        if (variable == "extraheader" &&
            lowercase(trim(value.substr(0, value.find_first_of(":;")))) == "host") {
            return "the repository's own config sets " + named +
                   " to a Host header, which would have the fetch ask the remote's address for a "
                   "site other than the one asked about; fetch it yourself, or remove the setting";
        }
    }
    return {};
}

std::span<const std::string_view> fetch_fence_transports() {
    static constexpr std::array<std::string_view, 9> transports{
        "file", "git", "ssh", "http", "https", "ftp", "ftps", "ext", "fd"};
    return transports;
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
    const std::chrono::milliseconds timeout = options.timeout;
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
    // Its review form fetches, and a network fetch asks about the host (57x).
    log.reaches_hosts = true;
    log.run_gated = [open, review_of, root, timeout](std::string_view arguments,
                                                     const agent::TargetGate& gate) {
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
                ReviewFetch review_fetch{git, root, timeout, gate};
                std::vector<std::string> notes;
                const std::string head_ref =
                    resolve_review_ref(git, review_fetch, head, remote, fetch, notes);
                const std::string base_ref =
                    resolve_review_ref(git, review_fetch, base, remote, fetch, notes);
                const std::string spec = base_ref + ".." + head_ref;
                command.push_back(spec);
                header = "Repo: " + git.repo().string() + "  |  Range: " + spec +
                         "  (commits the branch under review adds)\n" + with_notes(notes) + "\n";
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
    // Its review form fetches, and a network fetch asks about the host (57x).
    diff.reaches_hosts = true;
    diff.run_gated = [open, review_of, root, timeout](std::string_view arguments,
                                                      const agent::TargetGate& gate) {
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
                ReviewFetch review_fetch{git, root, timeout, gate};
                std::vector<std::string> notes;
                const std::string head_ref =
                    resolve_review_ref(git, review_fetch, head, remote, fetch, notes);
                const std::string base_ref =
                    resolve_review_ref(git, review_fetch, base, remote, fetch, notes);
                spec = base_ref + "..." + head_ref;
                header = "Repo: " + git.repo().string() + "  |  Review: " + spec +
                         "  (branch under review vs base -- changes since merge base)\n" +
                         with_notes(notes) + "\n";
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
