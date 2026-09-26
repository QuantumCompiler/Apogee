---
name: apogee-release-summary
description: Draft the GitHub release notes for an Apogee version in the house format of the published releases — title and theme, intro, Highlights, Install, Platforms, Known limitations, Reference — built from the release's evidence (ROADMAP, MILESTONES, the git range). Use when the user runs /apogee-release-summary, or asks to "write the release notes", "summarize this release", "draft the release summary for vX.Y.Z".
argument-hint: "[vX.Y.Z]"
---

# Apogee Release Summary

Writes the notes a GitHub Release page shows for one Apogee version, in the format the published releases already use ([releases](https://github.com/QuantumCompiler/Apogee/releases)). A merge into `stable` publishes the release with GitHub's generated notes (ci.yml → `tag and release`); this summary is what replaces them. The **format** comes from the published releases, the **facts** from the repo's own record — never from memory of the session.

The optional argument is the version (`v0.1.2` or `0.1.2`). With none, it is the release the checkout's `lib/release/VERSION` names — the release's own version since 2026-09-25; the CLI keeps a separate one in `lib/src/cli/CMakeLists.txt`, which moves only when the CLI changes.

## 1. Pin the release and its range

```bash
tr -d ' \r\n' < lib/release/VERSION
```

- **The target** — `v<version>`, from the argument or the command above.
- **The previous release** — the newest *published* release that is not the target, from GitHub (the public API needs no login; `gh` may not be installed):

  ```bash
  curl -s "https://api.github.com/repos/QuantumCompiler/Apogee/releases?per_page=10" \
    | python3 -c 'import json,sys; t=sys.argv[1]; rs=[r["tag_name"] for r in json.load(sys.stdin) if not r["draft"] and r["tag_name"]!=t]; print(rs[0] if rs else "")' v0.1.2
  ```

  (Substitute the target for `v0.1.2`.) An empty answer means this is the first release: there is no range, only the whole product — follow the v0.1.0 notes' shape.
- **The range base** — the previous release's commit **as GitHub has it**, never a local tag. A local tag can be stale: on the machine this skill was written on, the local `v0.1.1` was an old annotated tag on the v0.1.0 merge, and a range from it credits the previous release's own work to this one.

  ```bash
  prev=v0.1.1   # the previous release
  git ls-remote --tags origin "refs/tags/$prev" "refs/tags/$prev^{}" \
    | awk -v t="refs/tags/$prev" '{ sha[$2] = $1 } END { print (t "^{}" in sha) ? sha[t "^{}"] : sha[t] }'
  ```

  If that commit is not present locally (`git cat-file -e <sha>^{commit}`), `git fetch origin stable` brings it. If the local tag disagrees with GitHub's, tell the user — do not delete or move their tags.
- **The range end** — the target's own published commit if the release already exists (resolved the same way), else `HEAD`. Drafting ahead of the merge is normal; say so if `git status` shows uncommitted work, since it would be described but is not yet in the release.

Every range command below is `<base>..<end>`.

## 2. Read the exemplars

Fetch the two most recent published release bodies and read them **in full** — they are the template, the tone and the level of detail:

```bash
curl -s "https://api.github.com/repos/QuantumCompiler/Apogee/releases/tags/<tag>" \
  | python3 -c 'import json,sys; print(json.load(sys.stdin)["body"])'
```

(`gh release view <tag> --json body -q .body` does the same where `gh` is signed in.) If the published format has moved on from what section 4 describes, the newest published release wins; say which way it moved.

## 3. Gather the evidence

- **The theme** — [`ROADMAP.md`](../../../lib/documentation/assistant/ROADMAP.md)'s heading for the version (`### v0.1.2 — The terminal and the model directory`), and that section's checked lines: each is a candidate highlight.
- **The detail** — every [`MILESTONES.md`](../../../lib/documentation/assistant/MILESTONES.md) entry the range added, found by the git range rather than by date (a tag's commit date is not its release date):

  ```bash
  git diff <base>..<end> -- lib/documentation/assistant/MILESTONES.md | grep -E '^\+#{2,3} '
  ```

  Read each one whole: what was built, the decisions, and the **recorded costs** — "Not verified", "The cost, named", "Recorded consequence" — which feed Known limitations. Include the working tree (`git diff <base> -- …`) when drafting ahead of the merge.
- **Items shipped** — backlog documents deleted anywhere in the range (an item created and deleted inside it leaves no net diff, so ask the log):

  ```bash
  git log --diff-filter=D --name-only --format= <base>..<end> -- lib/documentation/backlog | sort -u
  ```
- **Did the CLI change at all?** `lib/scripts/changed.sh cli <base> <end>` — `cli=false` means the release **carries the previous release's CLI archives, copied** (the CLI pipeline builds nothing then), so its binaries are byte for byte the previous release's and report the older CLI version: say so up front, as v0.1.1's notes did for a binary that had not changed. With `cli=true`, `git diff --stat <base>..<end> -- lib/src/cli/source` still tells a pipeline-only change (v0.1.1: the binary functionally identical) from one the binary's user feels.
- **The history** — `git log --oneline <base>..<end>` for anything the docs missed.
- **What moves the fixed sections** — `git diff --stat <base>..<end> -- lib/scripts/install.sh lib/scripts/install.ps1 README.md` (Install), `ALL_TARGETS` in `lib/scripts/cicd.sh` (Platforms), `git diff --name-status <base>..<end> -- lib/documentation/reference` (Reference), and any work that closes a listed limitation (signing, self-update, Windows child processes, TLS on `serve`).

## 4. Write it

The shape, with the fixed sections as they stand in the published notes:

````markdown
# Apogee vX.Y.Z — <theme>

<Intro: one paragraph.>

## Highlights

**<The claim, as one sentence.>** <One paragraph: what changed for the person running the binary, then how.>

<…4–8 of them.>

## Install

macOS and Linux:

```sh
curl -fsSL https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.sh | bash
```

Windows (PowerShell):

```powershell
irm https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.ps1 | iex
```

Or take the archive for your platform from the assets below: `apogee-<target>.tar.gz` for macOS and Linux, `apogee-<target>.zip` for Windows, each holding the binary and the completion stubs. Then run `apogee check --fix` once.

## Platforms

`macos-arm64`, `linux-x64`, `linux-arm64`, `windows-x64`, `windows-arm64`. Every build is native on its own runner, has llama.cpp in it, and is built for the platform rather than the machine that built it. There is no Intel Mac build.

## Known limitations

- **<Limitation.>** <What it means and what to do about it.>

## Reference

- <Label>: `<path>`
````

Section by section:

- **Title** — `# Apogee vX.Y.Z — <theme>`: the ROADMAP heading's theme with its first letter lowercased (`The release pipeline` → `the release pipeline`), after an em dash.
- **Intro** — one paragraph, two to four sentences, for someone deciding whether to install. It opens by saying what kind of release this is — "The first release.", "A maintenance release." — then what changed at the level of the whole release. When nothing under `source/` changed, say so in bold and say whether upgrading is worth it; when a fix makes upgrading matter, say that instead.
- **Highlights** — four to eight, the most user-felt first, the process ones last unless the release is about process (v0.1.1 was). Each is a **bold claim that is one full sentence, period inside the bold**, then one paragraph: what changed for the user first, then the mechanism, naming real commands, flags, config keys and paths in backticks, and real counts. A fix names what used to happen ("where previously…") and why it matters. Group ROADMAP lines into one highlight when they are one story; never split one line into two highlights. Every sentence traces to a MILESTONES entry, a ROADMAP line, or the diff; every number is quoted from those or measured, never estimated.
- **Install** and **Platforms** — verbatim from the previous release. Change them only where step 3 found a change (a script URL, an archive name, a target), and then change only that.
- **Known limitations** — start from the previous release's list. Remove a bullet only when this release closed it (that is usually a highlight too); reword one that changed; keep the rest word for word. When the binary did not change, lead with "Unchanged from vPREV, since the binary is:". New limitations this release introduces — the recorded costs from step 3 — go after the list: one as "One new one, recorded because it is real: …", several as their own bullets under a line saying they are new.
- **Reference** — the previous list, plus any user-facing reference document the range added, plus the contributor document a process release is about (v0.1.1 added "Cutting a release: `lib/documentation/assistant/DEVELOPER.md` → **Cutting a release**"). Paths as inline code, not links.

House style — the published notes' own:

- The audience installs and runs the binary. No backlog item numbers, no milestone letters, no dates, no "user decision" or "the user's call", no session narrative, no links into MILESTONES. Contributor mechanics (workflows, `cicd.sh` flags) appear only when they are the release's subject.
- Em dashes (—) in prose; British spelling as the published notes have it ("behaviour"); "Apogee" capitalised; no emoji; plain Markdown GitHub renders.
- Honest over flattering: a cost is named where it is real, as the published notes do.

## 5. Check the draft

Script what can be scripted:

- nothing internal leaked:

  ```bash
  grep -nE 'item [0-9]|Milestone [A-Z]\b|user decision|user.s call|20[0-9]{2}-[0-9]{2}-[0-9]{2}|backlog/' <draft>
  ```

  (should print nothing);
- Install and Platforms are identical to the previous release's, unless a change was deliberate — diff the two sections with `diff --strip-trailing-cr`: a body edited on GitHub's web page comes back with CRLF line endings, so every line differs otherwise;
- every path under **Reference** exists at the range end (`git cat-file -e <end>:<path>`);
- every command and flag a highlight names exists — ask the built binary (`apogee <command> --help </dev/null`) or grep `lib/src/cli/source/commands/`;
- every highlight maps to a piece of evidence from step 3 — list the mapping for yourself, not in the notes.

## 6. Deliver

Write the draft to `apogee-release-notes-vX.Y.Z.md` in the session's scratchpad directory (or `${TMPDIR:-/tmp}` without one), and show it in full in the reply inside a four-backtick `markdown` fence, so its own code blocks survive a copy. Then say, briefly: the range it covers (`<base>..<end>`, and whether the working tree was included), which MILESTONES entries each highlight came from, and anything you left out and why.

**Do not publish.** Editing a release is public; do it only when the user explicitly asks, and then:

```bash
gh release edit vX.Y.Z --notes-file <draft>
```

which replaces the generated notes. If the release does not exist yet — drafting ahead of the merge — say it can be applied once the merge publishes it. Without `gh`, the user pastes the draft into the release page's **Edit**.
