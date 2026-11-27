#pragma once

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "agent/tool.h"

/// The git toolset: status, log, diff, show -- each a `git` child.
///
/// Every argument that reaches `git` travels in an argument list, never through
/// a shell, so quoting is not a risk; **option injection** is, and the ref
/// allow-list -- a token that starts with a letter or digit -- is what closes
/// it.
///
/// The tools read and never prompt for a read, so **they are confined
/// instead** (57o):
/// an explicit `repo` must resolve inside the file tools' root, and so must
/// what git opens there, whether the folder was named or walked to -- its
/// git directory, its common directory, its work tree and its object store's
/// alternates -- unless git reaches it only through what lies outside the
/// root (the checkout a launch in a subfolder sits in) or through a `.git`
/// file its own git directory vouches for (a linked worktree's `gitdir`, a
/// submodule's `core.worktree`). So a `.git` file, link or folder written
/// into the root cannot send git to another repository. And no git they
/// start runs a command a repository's own config names -- every call
/// passes `kGitHardening`, the repository's filter drivers are switched off
/// by name, and the commands that diff never run an external diff or a
/// textconv, nor look inside a submodule's work tree, nor diff or log inside
/// a submodule at all: one shows as the commit ids it moved between.
///
/// `git_diff` carries the deterministic review form the analyze item will
/// pass parameters into: `base...head` (the merge-base diff, only what the
/// head adds), with the refs resolved against a remote and fetched on demand
/// when they are absent locally. The defaults for that form come from
/// `ReviewDefaults`, set by flags, never inferred from the model's text.
///
/// **The review fetch stays in bounds** (57x). It is the one thing the tools
/// do that can leave the machine, so `git_diff` and `git_log` reach hosts
/// while they run (`agent::Tool::reaches_hosts`) and every fetch -- a ref
/// named alone or qualified by its remote, from either tool -- takes one
/// path: `remote` must be a remote the repository has; its URL is read as
/// data and judged before git runs (`parse_fetch_url`: a folder must be
/// inside the root and its repository confined as `repo` is; a network
/// URL's host is put through the gate as a website; any other transport is
/// refused); the repository's own settings that would run a command or send
/// the fetch elsewhere refuse it by name (`fetch_setting_refusal`), a header
/// setting that names another site among them (`fetch_header_refusal`), each
/// listing read whole or the fetch refused; and git
/// fetches **by that URL**, with an explicit refspec, so no
/// `remote.<name>.*` key applies, no submodule is fetched into, no redirect
/// is followed, no bundle or protocol-v2 packfile URI a server names on
/// another host is taken (`http.followRedirects`, `transfer.bundleURI` and
/// `fetch.uriProtocols` are each set off on every fetch), and only the judged
/// transport, to the one host asked about, is used. The person's own global
/// and system settings, and their environment, stay theirs -- so their own
/// `http.<url>.followRedirects` for the remote's URL, which outranks the bare
/// switch, still follows a redirect. Every `git config` read the tools make
/// drops the person's `GIT_CONFIG`, which `git config` alone honours, so the
/// settings and filter drivers it lists are the ones git's other commands
/// read. And no git a tool
/// starts fetches a missing object from a promisor remote by its name
/// (`GIT_NO_LAZY_FETCH`, the person's own value dropped for the tools' `1`).
namespace apogee::tools {

/// How a review diff resolves when the model passes no `head`/`base`.
struct ReviewDefaults {
    /// The branch under review; empty means no review context.
    std::string head;
    /// The base; empty means the repository's default branch.
    std::string base;
    std::string remote = "origin";
    /// `auto` (fetch only when a ref is absent locally), `always`, or
    /// `never`.
    std::string fetch = "auto";

    [[nodiscard]] bool active() const noexcept {
        return !head.empty() || !base.empty();
    }
};

struct GitOptions {
    std::chrono::milliseconds timeout{30000};
    /// Where the walk for a `.git` directory starts when no `repo` is given.
    std::filesystem::path working_directory;
    /// The root an explicit `repo`, and what git opens, must resolve inside
    /// (57o): the file tools' effective root (`effective_fs_root`), so the
    /// git tools read no repository `read_file` could not. Empty means
    /// `working_directory`.
    std::filesystem::path fs_root;
    ReviewDefaults review;
    /// When set, read at every call instead of `review`, so a surface that
    /// changes the review mid-session (`chat`'s `/branch`) re-points the
    /// tools without rebuilding the registry -- and without re-dialling
    /// every MCP server that lives in it. Still a plain in-process value:
    /// no environment side channel.
    std::shared_ptr<const ReviewDefaults> live_review;
};

/// Whether `ref` is a safe git ref token: letters, digits, `.`, `_`, `/`,
/// `-`, not starting with `-`.
[[nodiscard]] bool valid_ref(std::string_view ref) noexcept;

/// The repository for `hint`: `hint` itself when given, else the nearest
/// ancestor of `start` holding `.git`, else `start`.
[[nodiscard]] std::filesystem::path find_repo(std::string_view hint,
                                              const std::filesystem::path& start);

/// What every git call is given ahead of its command (57o), so that nothing a
/// repository's own config names runs: no `core.fsmonitor` command, no
/// signature check through `gpg.program` (`log.showSignature`), no submodule
/// summary (`status.submoduleSummary`, a `git log` inside each moved
/// submodule), no hook (`core.hooksPath=/dev/null`: neither `.git/hooks` nor a
/// folder the repository names is looked in), and no write to the index --
/// `git status` takes no optional lock, and `git diff` against the work tree
/// does not write back what it refreshed (`diff.autoRefreshIndex=false`),
/// which it would whatever the lock option says, running `post-index-change`.
/// Filter drivers are named per repository (`filter_neutralising_options`).
[[nodiscard]] std::span<const std::string_view> git_hardening() noexcept;

/// The `-c` options that switch off each filter driver a repository's own
/// config (`local` or `worktree` scope) defines (57o): its `clean`,
/// `smudge` and `process` emptied and `required` false, so `git status` and
/// `git diff` hash a file as it is rather than running the driver.
/// `scoped_names` is `git config --show-scope --name-only --get-regexp
/// '^filter\.'`'s output. A driver whose name `-c` cannot carry (it holds a
/// `=`) sets `refusal`.
[[nodiscard]] std::vector<std::string> filter_neutralising_options(std::string_view scoped_names,
                                                                   std::string& refusal);

/// A remote's URL as the review fetch judges it (57x), read as data before
/// git runs.
struct FetchUrl {
    enum class Kind : std::uint8_t {
        /// A folder: a path, or a `file://` URL. Nothing leaves the machine;
        /// it must be inside the root.
        Local,
        /// `https`, `http`, `ssh` (a URL or scp-like `[user@]host:path`) or
        /// `git`: its host is asked about as a website.
        Network,
    };
    Kind kind = Kind::Local;
    /// `file`, `https`, `http`, `ssh` or `git`: the one transport the fetch
    /// may use.
    std::string transport{};
    /// A network URL's host in `contracts/host.h`'s canonical form; empty for
    /// a folder.
    std::string host{};
    /// What git is handed, rebuilt from the parts judged -- the scheme
    /// lower-cased, the host canonical; for a folder, its path, which the
    /// fetch resolves against the root before git sees it.
    std::string url{};
    /// `url` with any password masked, for the question.
    std::string shown{};
};

/// Reads `url` as the review fetch judges it (57x), or nullopt with `refusal`
/// saying why it is not fetched from: a URL beginning with `-` (git would
/// read it as an option), a host or user name beginning with `-` (ssh
/// would), a host `contracts/host.h` does not accept or more than one `@`
/// before it, a `file://` URL naming a host or holding a percent escape, an
/// scp-like address with no `/` (git could read it as a remote's name), an
/// `ssh://` or `git://` path holding a `@`, `[`, `]` or `%`, or a user part
/// holding a `[` or `]` (git would read either back into the host, reaching
/// one never judged: `ssh://[other]@good/x` reaches `other`), a LOCAL or
/// `file://` folder path holding `@[` or opening with `[` (git's `host_end()`
/// reads it as a bracketed host and enters the folder after the matching `]`,
/// not the one judged), a control character, or
/// any transport but the five -- `ext::`, `fd::`, a `<vcs>::` helper,
/// `ftp://`. Schemes are read in any case.
[[nodiscard]] std::optional<FetchUrl> parse_fetch_url(std::string_view url, std::string& refusal);

/// The refusal for the repository's own settings a fetch would run or follow
/// (57x), or empty: `scoped_names` is `git config --show-scope --name-only
/// --get-regexp` output, and only `local` and `worktree` scope -- the
/// repository's own, an `include.path` it pulls in among them -- counts;
/// the person's global, system and command-line settings are theirs. The
/// keys: those that run a command (`core.sshCommand`, `core.gitProxy`,
/// `core.askPass`, `core.alternateRefsCommand`, `credential.helper` and
/// `credential.<url>.helper`) and those that send the fetch elsewhere
/// (`url.<base>.insteadOf`, an empty base -- `url..insteadof` -- included,
/// `http.proxy` and `http.<url>.proxy`, `http.curloptResolve`,
/// `http.followRedirects`, `http.cookieFile` and `http.saveCookies` -- which
/// would write a cookie jar over any file the person can -- and
/// `fetch.bundleURI`).
[[nodiscard]] std::string fetch_setting_refusal(std::string_view scoped_names);

/// The refusal for the repository's own header settings that would have a
/// review fetch's request name a site other than the one asked about (57x),
/// or empty: `scoped_values` is `git config -z --show-scope --get-regexp`
/// output for `http.[<url>.]extraHeader` and `http.[<url>.]userAgent`, and
/// only `local` and `worktree` scope counts, as with `fetch_setting_refusal`.
/// Refused: an `extraHeader` that is a `Host` header (its name, before the
/// first `:` or `;`, is `Host` in any case once spaces are trimmed), which a
/// server holding several sites at one address answers from the site it
/// names; and either setting holding a control character, which would start a
/// header line of its own. Any other header -- a credential, as
/// `actions/checkout` writes one -- is sent to the host asked about. The
/// refusal never repeats the value.
[[nodiscard]] std::string fetch_header_refusal(std::string_view scoped_values);

/// The transports the review fetch's protocol fence names (57x): every one
/// git ships -- its built-in `file`, `git` and `ssh`, and each `git-remote-*`
/// helper it installs (`ext`, `fd`, `ftp`, `ftps`, `http`, `https`). A fetch
/// sets `protocol.<name>.allow=never` for each but the judged transport,
/// since a repository's own `protocol.<name>.allow` outranks a bare
/// `protocol.allow=never`. A test holds the list to the installed git's.
[[nodiscard]] std::span<const std::string_view> fetch_fence_transports();

void register_git_tools(agent::ToolRegistry& registry, const GitOptions& options);

}  // namespace apogee::tools
