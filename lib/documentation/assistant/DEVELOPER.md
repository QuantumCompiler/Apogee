# Developer Guide

This document describes the layout of the Apogee repository and what each file does. For user-facing documentation, see [`README.md`](../../../README.md) (not yet written); for how to work in the repo, see [CLAUDE.md](CLAUDE.md).

---

## Directory Layout

```
Apogee/
├── CLAUDE.md                — Pointer to the contributor docs system
├── .claude/
│   └── skills/
│       ├── apogee-backlog-item/        — Repo-local skill: take the next backlog item per the docs-first process
│       ├── apogee-create-backlog-item/ — Repo-local skill: spec a new item into the backlog + index + roadmap
│       ├── apogee-document-update/     — Repo-local skill: pre-MR docs pass — reconcile every doc against the branch diff
│       └── apogee-pull-request/        — Repo-local skill: draft the MR description from the branch's docs evidence
├── .gitignore
└── lib/
    ├── documentation/       — Project documentation
    │   ├── assistant/       — Contributor docs (CLAUDE, SPEC, ROADMAP, MILESTONES, this file)
    │   └── backlog/         — The work queue: pending work, one document per item (see its README)
    ├── src/                 — Application source, one directory per app (planned; lands with cpp-project-skeleton)
    │   ├── cli/             — The CLI application
    │   │   ├── source/      — main.cpp + one package dir per concern (harness, backends, agentloop, commands, …)
    │   │   └── tests/       — Test dirs mirroring source/'s packages
    │   └── <darwin|linux|windows>/ — GUI applications, one per platform (future — down the road)
    └── scripts/             — Repo scripts
        ├── cicd.sh          — CI/CD entry point: builds the current branch for any of the six
        │                      release targets (--platform linux|macos|windows × x64|arm64, or all;
        │                      non-native targets defer to the CI matrix), --fresh = clean-room
        │                      clone of github.com/QuantumCompiler/Apogee at that branch; --test, --clean
        └── cicd-completion.bash — Tab completion for cicd.sh's flags (source from your shell rc)
```

_TODO:_ no source code exists yet — extend this tree, and add a `## <path> — <role>` section per package below, as the first modules land.

---

*(No source packages yet — each package gets its own section here as it lands: a sentence on its job, a file/purpose table, and subsections for key interfaces.)*

---

## Build system

_TODO:_ no build exists yet — record the build/run/clean commands here once the stack is chosen.

## Testing

_TODO:_ no test harness exists yet — record where tests live and how to run the full suite and a single test once it does.

---

## Adding a new extension point

*(Recipes get written as each extension point lands in code — given the product shape, the first will almost certainly be **Adding a new provider backend** once that seam exists. See the codebase map in [CLAUDE.md](CLAUDE.md) for where things live.)*

---

## Cutting a release

See [CLAUDE.md](CLAUDE.md) → **Release and Install Infrastructure**: development happens on a version-named branch (currently `v0.1.0`) and merges into `stable`; the tagging/packaging procedure is still a `_TODO:_` there.
