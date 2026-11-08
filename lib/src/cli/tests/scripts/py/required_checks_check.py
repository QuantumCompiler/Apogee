#!/usr/bin/env python3
"""cli.required_checks -- lib/scripts/required-checks.py against a stand-in gh (57m).

    required_checks_check.py <work-dir> [<required-checks.py>]

The "Stable: CI must pass" ruleset -- every pull-request job required, no
bypass list, the branch up to date with `stable` -- was described as in force
from 2026-10-03 and never created: the script's dry run printed "no ruleset
yet" and exited 0, and nothing else ever asked. The second bug sweep found it
missing (2026-10-31). `--verify` is the standing proof it holds, and this is
its test, together with the 2026-10-03 cases of the dry run and `--apply`
that were run then and never committed.

The real script runs under a minimal environment whose PATH has one `gh`: this
file, run as `--as-gh`, a fake GitHub serving a model of the repository from
a JSON file -- its pull requests, their workflow runs, the runs' jobs and
check suites, its rulesets -- and answering the API paths the script asks the
way GitHub does: `pulls?state=open&base=...` filtered and sorted by update,
`rules/branches/<branch>` derived from the active rulesets that target it, a
POST or PUT of a ruleset applied to the model (so a `--verify` after an
`--apply` sees what was written) and logged, anything else a 404. The model
starts from documents recorded from the real repository with read-only GETs
(tests/fixtures/required_checks/pr11.json: #11, v0.2.0's pull request, its
ci.yml and darwin.yml runs, and the one "Stable" ruleset), so the 15 checks
here are the ones the real dry run derives.

  --verify    exit 1 on a missing ruleset (the recorded repository, as it
              stood before --apply), `disabled` or `evaluate`, a bypass
              actor, a bypass list GitHub did not show (no `bypass_actors`,
              as a caller without admin access is answered), strict false,
              no checks rule, a list differing from the run's (a check
              missing, one extra, one pinned to another app), and a
              ruleset GitHub does not apply to `stable`; exit 0 on a
              match, printing the list; it never writes; it refuses --apply
              beside it.
  dry run     every check marked new, and "nothing is enforced" while the
              ruleset is missing; nothing written.
  --apply     creates the ruleset as designed (active, no bypass list, up to
              date required, the 15 checks pinned to their app), after which
              --verify passes; an update replaces the list and keeps every
              other setting; already matching writes nothing; the bypass,
              enforcement and up-to-date warnings, and the hidden bypass
              list's.
  the run     a failed run and a running one refused, the refusal the last
              line of the output when stderr goes into stdout's file, as
              `make pr-ci` logs it; an open pull request preferred over a
              merged one; two workflows combined; a pull
              request into another branch refused; the merged pull request's
              post-merge run, and the jobs a run skipped, never required.

The optional second argument runs another copy of the script -- how this was
held against the one from before 57m. POSIX only, like cli.install_parity.
"""

import copy
import json
import os
import shutil
import subprocess
import sys
import tempfile
from urllib.parse import parse_qs, urlsplit

HERE = os.path.dirname(os.path.abspath(__file__))
FIXTURE = os.path.join(HERE, "..", "..", "fixtures", "required_checks", "pr11.json")
DEFAULT_SCRIPT = os.path.join(HERE, "..", "..", "..", "..", "..", "scripts", "required-checks.py")
RULESET = "Stable: CI must pass"
APP = 15368  # github-actions, as the recorded check suites name it


# --- The fake GitHub, run as `gh` ----------------------------------------------

def as_gh(argv):
    world_path = os.environ["GH_WORLD"]
    with open(world_path, encoding="utf-8") as handle:
        world = json.load(handle)

    def log(entry):
        with open(os.environ["GH_LOG"], "a", encoding="utf-8") as handle:
            handle.write(json.dumps(entry) + "\n")

    def answer(document):
        print(json.dumps(document))
        return 0

    def not_found():
        print("gh: Not Found (HTTP 404)", file=sys.stderr)
        return 1

    if argv[:2] == ["repo", "view"]:
        log({"argv": argv})
        print(world["repo"])
        return 0
    if not argv or argv[0] != "api":
        log({"argv": argv})
        print(f"stand-in gh: not written for: gh {' '.join(argv)}", file=sys.stderr)
        return 1

    method, path, body = "GET", None, None
    rest = argv[1:]
    while rest:
        word = rest.pop(0)
        if word == "-X":
            method = rest.pop(0)
        elif word == "--input":
            source = rest.pop(0)
            body = json.loads(sys.stdin.read() if source == "-" else open(source, encoding="utf-8").read())
        elif path is None:
            path = word
        else:
            print(f"stand-in gh: unexpected argument {word}", file=sys.stderr)
            return 1
    log({"method": method, "path": path, "body": body})

    split = urlsplit(path)
    query = {k: v[0] for k, v in parse_qs(split.query).items()}
    parts = split.path.split("/")
    repo = world["repo"]
    owner, name = repo.split("/")
    if parts[:3] != ["repos", owner, name]:
        return not_found()
    tail = parts[3:]

    def save():
        with open(world_path, "w", encoding="utf-8") as handle:
            json.dump(world, handle)

    def summary(ruleset):
        return {k: ruleset[k] for k in ("id", "name", "target", "source_type", "source", "enforcement")
                if k in ruleset}

    if method == "GET":
        if not tail:
            return answer({"full_name": repo, "default_branch": world["default_branch"]})
        if tail[0] == "pulls" and len(tail) == 2:
            for pr in world["pulls"]:
                if str(pr["number"]) == tail[1]:
                    return answer(pr)
            return not_found()
        if tail == ["pulls"]:
            pulls = [pr for pr in world["pulls"]
                     if pr["state"] == query.get("state", "open") and pr["base"]["ref"] == query.get("base")]
            pulls.sort(key=lambda pr: pr["updated_at"], reverse=query.get("direction") == "desc")
            return answer(pulls[:int(query.get("per_page", 30))])
        if tail == ["actions", "runs"]:
            runs = [run for run in world["runs"]
                    if run["event"] == query.get("event") and run["head_sha"] == query.get("head_sha")]
            runs.sort(key=lambda run: run["created_at"], reverse=True)
            return answer({"total_count": len(runs), "workflow_runs": runs})
        if tail[:2] == ["actions", "runs"] and tail[3:] == ["jobs"]:
            jobs = world["jobs"].get(tail[2])
            return answer({"total_count": len(jobs), "jobs": jobs}) if jobs is not None else not_found()
        if tail[0] == "check-suites" and len(tail) == 2:
            suite = world["check_suites"].get(tail[1])
            return answer(suite) if suite is not None else not_found()
        if tail == ["rulesets"]:
            return answer([summary(r) for r in world["rulesets"]])
        if tail[0] == "rulesets" and len(tail) == 2:
            for ruleset in world["rulesets"]:
                if str(ruleset["id"]) == tail[1]:
                    return answer(ruleset)
            return not_found()
        if tail[:2] == ["rules", "branches"] and len(tail) == 3:
            return answer(branch_rules(world, tail[2]))
        return not_found()

    if method == "POST" and tail == ["rulesets"]:
        created = dict(body)
        created.update({"id": 900000 + len(world["rulesets"]), "source_type": "Repository", "source": repo})
        world["rulesets"].append(created)
        save()
        return answer(created)
    if method == "PUT" and tail[0] == "rulesets" and len(tail) == 2:
        for ruleset in world["rulesets"]:
            if str(ruleset["id"]) == tail[1]:
                ruleset.update(body)
                save()
                return answer(ruleset)
        return not_found()
    return not_found()


def branch_rules(world, branch):
    """The rules GitHub applies to a branch: every rule of each ACTIVE ruleset
    whose conditions include it, tagged with where it came from."""
    rules = []
    for ruleset in world["rulesets"]:
        if ruleset.get("enforcement") != "active":
            continue
        include = ruleset.get("conditions", {}).get("ref_name", {}).get("include", [])
        targets = ("~ALL" in include or f"refs/heads/{branch}" in include
                   or ("~DEFAULT_BRANCH" in include and branch == world["default_branch"]))
        if not targets:
            continue
        for rule in ruleset.get("rules", []):
            applied = dict(rule)
            applied.update({"ruleset_source_type": "Repository", "ruleset_source": world["repo"],
                            "ruleset_id": ruleset["id"]})
            rules.append(applied)
    return rules


# --- The model, from the recording -----------------------------------------------

def recorded():
    with open(FIXTURE, encoding="utf-8") as handle:
        doc = json.load(handle)
    pull = copy.deepcopy(doc["pull"])
    pull["updated_at"] = pull["merged_at"]
    return doc, {
        "repo": doc["repo"],
        "default_branch": doc["repository"]["default_branch"],
        "pulls": [pull],
        "runs": copy.deepcopy(doc["runs"]["workflow_runs"]),
        "jobs": {rid: copy.deepcopy(j["jobs"]) for rid, j in doc["jobs"].items()},
        "check_suites": copy.deepcopy(doc["check_suites"]),
        "rulesets": [copy.deepcopy(doc["ruleset_stable"])],
    }


WANTED = sorted([
    "what changed", "version bump", "clone llama.cpp",
    "unit tests linux-arm64", "unit tests linux-x64", "unit tests macos-arm64",
    "unit tests windows-arm64", "unit tests windows-x64",
    "build linux-arm64", "build linux-x64", "build macos-arm64", "build windows-arm64", "build windows-x64",
    "what changed (darwin)", "build darwin",
])


def ci_ruleset(checks=None, enforcement="active", bypass=None, strict=True, include=None, rid=4242,
               rules_before=()):
    """A "Stable: CI must pass" ruleset in the recorded "Stable"'s shape."""
    checks = [(c, APP) for c in WANTED] if checks is None else checks
    return {
        "id": rid, "name": RULESET, "target": "branch", "source_type": "Repository",
        "source": "QuantumCompiler/Apogee", "enforcement": enforcement,
        "conditions": {"ref_name": {"exclude": [], "include": include or ["~DEFAULT_BRANCH"]}},
        "rules": list(rules_before) + [{
            "type": "required_status_checks",
            "parameters": {
                "strict_required_status_checks_policy": strict,
                "do_not_enforce_on_create": False,
                "required_status_checks": [{"context": c, "integration_id": a} for c, a in checks],
            },
        }],
        "bypass_actors": bypass or [],
    }


# --- The harness ----------------------------------------------------------------

class Harness:
    def __init__(self, work, script):
        self.work = work
        self.script = script
        self.bin = os.path.join(work, "bin")
        self.home = os.path.join(work, "home")
        os.makedirs(self.bin)
        os.makedirs(self.home)
        shim = os.path.join(self.bin, "gh")
        with open(shim, "w", encoding="utf-8") as handle:
            handle.write(f'#!/bin/sh\nexec "{sys.executable}" "{os.path.abspath(__file__)}" --as-gh "$@"\n')
        os.chmod(shim, 0o755)
        self.checks = 0
        self.failures = 0
        self.cases = 0

    def run(self, world, *args, merged=False):
        """The real script over the model; (exit status, output, writes).

        `merged` sends stderr into stdout's file the way `make pr-ci` logs the
        script, so the output keeps the order a reader of that log sees."""
        self.cases += 1
        world_path = os.path.join(self.work, f"world-{self.cases}.json")
        log_path = os.path.join(self.work, f"gh-{self.cases}.log")
        with open(world_path, "w", encoding="utf-8") as handle:
            json.dump(world, handle)
        open(log_path, "w", encoding="utf-8").close()
        env = {"PATH": f"{self.bin}:/usr/bin:/bin", "HOME": self.home, "GH_WORLD": world_path,
               "GH_LOG": log_path, "LC_ALL": "C.UTF-8", "PYTHONDONTWRITEBYTECODE": "1"}
        streams = {"stdout": subprocess.PIPE, "stderr": subprocess.STDOUT} if merged else {"capture_output": True}
        result = subprocess.run([sys.executable, self.script, *args], env=env, cwd=self.work, text=True,
                                stdin=subprocess.DEVNULL, timeout=60, **streams)
        with open(log_path, encoding="utf-8") as handle:
            calls = [json.loads(line) for line in handle if line.strip()]
        with open(world_path, encoding="utf-8") as handle:
            after = json.load(handle)
        writes = [c for c in calls if c.get("method", "GET") != "GET"]
        self.last_world = after
        return result.returncode, result.stdout + (result.stderr or ""), writes

    def check(self, name, ok, detail=""):
        self.checks += 1
        if not ok:
            self.failures += 1
            print(f"FAIL: {name}" + (f" -- {detail}" if detail else ""), file=sys.stderr)

    def expect(self, name, outcome, status, holds=(), lacks=(), writes=0):
        rc, out, written = outcome
        self.check(f"{name}: exit {status}", rc == status, f"exit {rc}: {out}")
        for text in holds:
            self.check(f"{name}: says '{text}'", text in out, out)
        for text in lacks:
            self.check(f"{name}: never says '{text}'", text not in out, out)
        if writes is not None:
            self.check(f"{name}: {writes} writes", len(written) == writes, json.dumps(written))


def main():
    if len(sys.argv) >= 2 and sys.argv[1] == "--as-gh":
        return as_gh(sys.argv[2:])
    if len(sys.argv) not in (2, 3):
        print(__doc__)
        return 2
    work, script = sys.argv[1], os.path.abspath(sys.argv[2] if len(sys.argv) == 3 else DEFAULT_SCRIPT)
    os.makedirs(os.path.dirname(work), exist_ok=True)
    work = tempfile.mkdtemp(prefix=os.path.basename(work) + ".", dir=os.path.dirname(work))
    try:
        harness = Harness(work, script)
        cases(harness)
        print(f"required_checks: {harness.checks} checks, {harness.failures} failed")
        return 1 if harness.failures else 0
    finally:
        shutil.rmtree(work, ignore_errors=True)


def cases(h):
    doc, base = recorded()
    listed = [f"    {name}   (app {APP})" for name in WANTED]

    # The fake answers the recording's own question as GitHub did.
    h.check("the fake's rules for stable are the recorded ones",
            branch_rules(base, "stable") == doc["rules_branches_stable"],
            json.dumps(branch_rules(base, "stable")))

    # --- --verify ---------------------------------------------------------------
    h.expect("verify, the repository as recorded (no ruleset)", h.run(base, "--verify"), 1,
             holds=[f"there is no '{RULESET}' ruleset, so nothing is required to merge into 'stable'",
                    "lib/scripts/required-checks.py --pr 11 --apply"],
             lacks=["is in force"])
    h.expect("verify --pr 11, the repository as recorded", h.run(base, "--verify", "--pr", "11"), 1,
             holds=[f"'{RULESET}' is not in force"])

    def with_ruleset(ruleset):
        world = copy.deepcopy(base)
        world["rulesets"].append(ruleset)
        return world

    h.expect("verify, a match", h.run(with_ruleset(ci_ruleset()), "--verify"), 0,
             holds=[f"'{RULESET}' is in force on 'stable'", "exactly the 15 checks #11 ran and passed",
                    *listed, "in force"],
             lacks=["NOT in force", "tag and release"])
    for enforcement in ("disabled", "evaluate"):
        h.expect(f"verify, {enforcement}", h.run(with_ruleset(ci_ruleset(enforcement=enforcement)), "--verify"), 1,
                 holds=[f"'{RULESET}' is {enforcement}, so it enforces nothing"], lacks=["is in force on"])
    bypass = [{"actor_id": 5, "actor_type": "RepositoryRole", "bypass_mode": "always"}]
    h.expect("verify, a bypass actor", h.run(with_ruleset(ci_ruleset(bypass=bypass)), "--verify"), 1,
             holds=["has a bypass list (1 actors), so it can be skipped"], lacks=["is in force on"])
    # GitHub shows the bypass list to an admin alone: a caller without that
    # access gets the ruleset with no `bypass_actors` at all.
    hidden = ci_ruleset(bypass=bypass)
    del hidden["bypass_actors"]
    h.expect("verify, the bypass list not shown (gh without admin access)",
             h.run(with_ruleset(hidden), "--verify"), 1,
             holds=[f"cannot see '{RULESET}''s bypass list (gh is not signed in with admin access",
                    "run --verify as a repository admin", "NOT in force as the pipeline needs (1)"],
             lacks=["is in force on", "no bypass list", "in force --"])
    h.expect("verify, strict false", h.run(with_ruleset(ci_ruleset(strict=False)), "--verify"), 1,
             holds=["does not require the branch to be up to date before merging"], lacks=["is in force on"])
    fewer = [(c, APP) for c in WANTED if c != "build darwin"]
    h.expect("verify, a check missing", h.run(with_ruleset(ci_ruleset(checks=fewer)), "--verify"), 1,
             holds=["'build darwin' (app 15368) passed on #11 but is not required",
                    "lib/scripts/required-checks.py --pr 11 --apply sets the list"])
    more = [(c, APP) for c in WANTED] + [("build macos-x64", APP)]
    h.expect("verify, a check extra", h.run(with_ruleset(ci_ruleset(checks=more)), "--verify"), 1,
             holds=["'build macos-x64' (app 15368) is required but did not run and pass on #11"])
    other_app = [(c, 99 if c == "version bump" else APP) for c in WANTED]
    h.expect("verify, a check pinned to another app", h.run(with_ruleset(ci_ruleset(checks=other_app)), "--verify"),
             1, holds=["'version bump' (app 15368) passed on #11 but is not required",
                       "'version bump' (app 99) is required but did not run and pass"])
    no_rule = ci_ruleset()
    no_rule["rules"] = [{"type": "non_fast_forward"}]
    h.expect("verify, no checks rule", h.run(with_ruleset(no_rule), "--verify"), 1,
             holds=["has no required-status-checks rule"])
    h.expect("verify, a ruleset on another branch",
             h.run(with_ruleset(ci_ruleset(include=["refs/heads/main"])), "--verify"), 1,
             holds=["GitHub does not apply 'Stable: CI must pass' to 'stable'"])
    h.expect("verify, several at once",
             h.run(with_ruleset(ci_ruleset(bypass=bypass, strict=False)), "--verify"), 1,
             holds=["NOT in force as the pipeline needs (2)", "(and 1 more)"])
    rc, out, _ = h.run(base, "--verify", "--apply")
    h.check("verify refuses --apply beside it", rc == 2 and "not allowed with" in out, out)

    # --- the dry run ------------------------------------------------------------
    h.expect("dry run, no ruleset", h.run(base), 0,
             holds=[*(f"  + {name}   (app {APP})" for name in WANTED),
                    f"no '{RULESET}' ruleset yet, so nothing is enforced: no check is required to merge into "
                    "'stable' until --apply creates it, with no bypass list",
                    "dry run: nothing was changed"],
             lacks=["tag and release"])
    h.expect("dry run, already up to date", h.run(with_ruleset(ci_ruleset())), 0,
             holds=["the ruleset already requires exactly these: nothing to change"], lacks=["  + "])
    h.expect("dry run, the bypass and enforcement warnings",
             h.run(with_ruleset(ci_ruleset(enforcement="evaluate", bypass=bypass, strict=False))), 0,
             holds=["warning: 'Stable: CI must pass' has a bypass list",
                    "warning: 'Stable: CI must pass' is evaluate, so it enforces nothing",
                    "warning: 'Stable: CI must pass' does not require the branch to be up to date"])
    h.expect("dry run, the bypass list not shown", h.run(with_ruleset(hidden)), 0,
             holds=[f"warning: cannot see '{RULESET}''s bypass list",
                    "the ruleset already requires exactly these: nothing to change"],
             lacks=["has a bypass list"])

    # --- --apply ------------------------------------------------------------------
    outcome = h.run(base, "--apply")
    h.expect("apply, created", outcome, 0, holds=[f"created '{RULESET}' requiring 15 checks"], writes=1)
    created = h.last_world
    posts = outcome[2]
    if posts:
        post = posts[0]
        body = post["body"]
        rule = body["rules"][0] if body.get("rules") else {}
        h.check("apply, POST repos/<repo>/rulesets",
                post["method"] == "POST" and post["path"] == "repos/QuantumCompiler/Apogee/rulesets", json.dumps(post))
        h.check("apply, created as designed",
                body.get("name") == RULESET and body.get("target") == "branch"
                and body.get("enforcement") == "active" and body.get("bypass_actors") == []
                and body.get("conditions") == {"ref_name": {"include": ["~DEFAULT_BRANCH"], "exclude": []}}
                and len(body.get("rules", [])) == 1 and rule.get("type") == "required_status_checks"
                and rule["parameters"].get("strict_required_status_checks_policy") is True,
                json.dumps(body))
        h.check("apply, the 15 checks pinned to their app",
                sorted((c["context"], c["integration_id"]) for c in rule.get("parameters", {}).get(
                    "required_status_checks", [])) == [(c, APP) for c in WANTED], json.dumps(rule))
    h.expect("verify after apply", h.run(created, "--verify"), 0, holds=[f"'{RULESET}' is in force on 'stable'"])

    stale = [(c, APP) for c in WANTED if c not in ("build darwin", "what changed (darwin)")]
    stale.append(("build macos-x86_64", APP))
    kept_rule = {"type": "non_fast_forward"}
    old = ci_ruleset(checks=stale, strict=False, rules_before=[kept_rule], rid=5151)
    h.expect("dry run, an update shown", h.run(with_ruleset(old)), 0,
             holds=[f"  + build darwin   (app {APP})", f"  + what changed (darwin)   (app {APP})",
                    f"  - build macos-x86_64   (app {APP}; no longer run)", "dry run: nothing was changed"])
    outcome = h.run(with_ruleset(old), "--apply")
    h.expect("apply, updated", outcome, 0,
             holds=[f"updated '{RULESET}': 2 added, 1 removed, 15 required"], writes=1)
    if outcome[2]:
        put = outcome[2][0]
        rules = put["body"].get("rules", [])
        checks_rules = [r for r in rules if r["type"] == "required_status_checks"]
        h.check("apply, PUT repos/<repo>/rulesets/<id> with the rules alone",
                put["method"] == "PUT" and put["path"] == "repos/QuantumCompiler/Apogee/rulesets/5151"
                and list(put["body"]) == ["rules"], json.dumps(put))
        h.check("apply, an update keeps the other rules and settings",
                kept_rule in rules and len(checks_rules) == 1
                and checks_rules[0]["parameters"]["strict_required_status_checks_policy"] is False,
                json.dumps(rules))
        h.check("apply, an update sets the list",
                checks_rules and sorted((c["context"], c["integration_id"]) for c in
                                        checks_rules[0]["parameters"]["required_status_checks"])
                == [(c, APP) for c in WANTED], json.dumps(checks_rules))
    h.expect("apply, already up to date", h.run(with_ruleset(ci_ruleset()), "--apply"), 0,
             holds=["nothing to change"], writes=0)

    # --- which run, and which jobs ------------------------------------------------
    for mode in ((), ("--verify",), ("--apply",)):
        failed = copy.deepcopy(base)
        for run in failed["runs"]:
            if run["id"] == 41226403061:
                run["conclusion"] = "failure"
        h.expect(f"a failed run refused {' '.join(mode)}", h.run(failed, *mode), 1,
                 holds=["ci.yml run 41226403061 for #11 ended failure -- required checks come from a pipeline "
                        "that passed"])
        running = copy.deepcopy(base)
        for run in running["runs"]:
            if run["id"] == 41226403105:
                run["status"], run["conclusion"] = "in_progress", None
        h.expect(f"a running pipeline refused {' '.join(mode)}", h.run(running, *mode), 1,
                 holds=["darwin.yml run 41226403105 for #11 is still in_progress -- wait for it to finish"])

    # `make pr-ci` logs --verify with stderr merged into stdout and reports the
    # log's last line as the verdict: a refusal printed while the run summary
    # still sat in stdout's buffer came out first, and the summary read
    # "NEEDS THE USER:   ci.yml run ...: 13 jobs passed".
    for name, world, refusal in (
            ("a running pipeline", running,
             "required-checks: darwin.yml run 41226403105 for #11 is still in_progress -- wait for it to finish"),
            ("a failed run", failed, "required-checks: ci.yml run 41226403061 for #11 ended failure")):
        rc, out, _ = h.run(world, "--verify", merged=True)
        last = ([line for line in out.splitlines() if line.strip()] or [""])[-1]
        h.check(f"{name} refused, logged as pr-ci logs it: the refusal is the last line",
                rc == 1 and last.startswith(refusal), f"exit {rc}, last line {last!r}: {out}")

    h.expect("two workflows combined, the post-merge run and skipped jobs left out", h.run(base), 0,
             holds=["the checks #11 (v0.2.0, merged) ran and passed",
                    "ci.yml run 41226403061: 13 jobs passed (1 skipped or not run on a pull request)",
                    "darwin.yml run 41226403105: 2 jobs passed",
                    "required to merge into 'stable' (15):"],
             lacks=["41233143727", "tag and release", "matrix.name"])

    opened = copy.deepcopy(base)
    head = "1" * 40
    opened["pulls"].append({"number": 12, "state": "open", "merged_at": None, "base": {"ref": "stable"},
                            "head": {"ref": "v0.2.1", "sha": head}, "updated_at": "2026-10-01T00:00:00Z"})
    for run in copy.deepcopy([r for r in base["runs"] if r["id"] != 41233143727]):
        rid = str(run["id"])
        run["id"] += 1000
        run["head_sha"] = head
        opened["runs"].append(run)
        opened["jobs"][str(run["id"])] = [dict(job, run_id=run["id"]) for job in base["jobs"][rid]]
    h.expect("an open pull request preferred", h.run(opened), 0,
             holds=["the checks #12 (v0.2.1, open) ran and passed", "ci.yml run 41226404061", "(15):"])
    h.expect("--pr picks the merged one", h.run(opened, "--pr", "11"), 0,
             holds=["the checks #11 (v0.2.0, merged) ran and passed"])

    elsewhere = copy.deepcopy(base)
    elsewhere["pulls"].append({"number": 13, "state": "open", "merged_at": None, "base": {"ref": "main"},
                               "head": {"ref": "topic", "sha": "2" * 40}, "updated_at": "2026-11-01T00:00:00Z"})
    for mode in ((), ("--verify",), ("--apply",)):
        h.expect(f"a pull request into another branch refused {' '.join(mode)}",
                 h.run(elsewhere, "--pr", "13", *mode), 1, holds=["#13 merges into 'main', not 'stable'"])


if __name__ == "__main__":
    sys.exit(main())
