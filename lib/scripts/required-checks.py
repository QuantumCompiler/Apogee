#!/usr/bin/env python3
"""required-checks.py -- keep `stable`'s required status checks in step with the pipeline.

    required-checks.py [--pr N] [--apply]

Shows -- and with --apply, sets -- the status checks a pull request into the
default branch must pass before it can merge, in a repository ruleset named
"Stable: CI must pass" that has NO bypass list: an admin can still merge
without an approval (the "Stable" ruleset's bypass), but nobody merges past a
red or unfinished pipeline (user decision, 2026-10-03).

The checks are never typed. They are read from GitHub's own record of a pull
request's CI: every job that RAN AND PASSED in that pull request's runs --
every workflow, each matrix row expanded -- named exactly as GitHub names the
check, from the app that ran it. So when the pipeline changes (a job renamed,
a platform added, a GUI application's workflow beside the CLI's), the list
follows from one passing run, with nothing to keep in step by hand.

What is left out falls out of the same rule:

  - a job that only runs after a merge (`tag and release`) is skipped on an
    open pull request, so it never "ran and passed" there;
  - a merged pull request's post-merge run (the `closed` event) is created at
    or after the merge, and runs from then on are ignored.

Which pull request: --pr N, else the newest open one into the default branch,
else the most recently merged. Its head's newest run of each workflow must have
passed -- required checks come from a pipeline that succeeded, or not at all.

When the pipeline changes, the order is: open the pull request, let its run
pass, run this with --pr <it> --apply, then merge. (Before --apply, the old
names are still required, and a renamed job's old name would never report.)

Needs gh, signed in with rights to administer the repository for --apply; run
it from a checkout of the repository. Without --apply it changes nothing.

When THIS SCRIPT must change -- its assumptions, each of which a pipeline
change could break (DEVELOPER.md -> Changing the pipeline):

  - every job that runs on an open pull request is required. A job that runs
    there but must not gate a merge (an informational one) needs an exclusion
    here;
  - a job meant only for after the merge is SKIPPED on an open pull request.
    One that runs there anyway would be required, and block every merge;
  - a merged pull request's post-merge run is created at or after the merge
    (`merged_at`). A post-merge workflow started some other way needs another
    filter;
  - the ruleset is named RULESET_NAME and targets the default branch.
"""

import argparse
import json
import subprocess
import sys

RULESET_NAME = "Stable: CI must pass"


def die(message):
    print(f"required-checks: {message}", file=sys.stderr)
    sys.exit(1)


def gh_api(path, method="GET", body=None):
    """`gh api`, parsed. A failure dies with gh's own message."""
    command = ["gh", "api", "-X", method, path]
    if body is not None:
        command += ["--input", "-"]
    result = subprocess.run(
        command,
        input=json.dumps(body) if body is not None else None,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        die(f"gh api {method} {path} failed: {result.stderr.strip() or result.stdout.strip()}")
    return json.loads(result.stdout) if result.stdout.strip() else None


def repository():
    result = subprocess.run(
        ["gh", "repo", "view", "--json", "nameWithOwner", "--jq", ".nameWithOwner"],
        capture_output=True,
        text=True,
    )
    if result.returncode != 0 or not result.stdout.strip():
        die("could not tell which repository this is -- run it from a checkout, with gh signed in")
    return result.stdout.strip()


def choose_pull_request(repo, branch, number):
    if number is not None:
        pr = gh_api(f"repos/{repo}/pulls/{number}")
        if pr["base"]["ref"] != branch:
            die(f"#{number} merges into '{pr['base']['ref']}', not '{branch}'")
        return pr
    open_prs = gh_api(f"repos/{repo}/pulls?state=open&base={branch}&sort=updated&direction=desc&per_page=10")
    if open_prs:
        return open_prs[0]
    closed = gh_api(f"repos/{repo}/pulls?state=closed&base={branch}&sort=updated&direction=desc&per_page=20")
    merged = [pr for pr in closed if pr.get("merged_at")]
    if not merged:
        die(f"no pull request into '{branch}' has run CI yet -- open one, let it pass, and run this again")
    return max(merged, key=lambda pr: pr["merged_at"])


def required_from_runs(repo, pr):
    """[(check name, app id)] for every job that ran and passed on the pull request."""
    head = pr["head"]["sha"]
    merged_at = pr.get("merged_at")
    runs = gh_api(f"repos/{repo}/actions/runs?event=pull_request&head_sha={head}&per_page=100")["workflow_runs"]
    # A merged pull request's `closed` run is created at the merge or after it.
    if merged_at:
        runs = [run for run in runs if run["created_at"] < merged_at]
    if not runs:
        die(f"#{pr['number']}'s head {head[:7]} has no pull-request CI run -- push to it, or wait for its run to start")

    newest = {}
    for run in runs:
        current = newest.get(run["workflow_id"])
        if current is None or run["created_at"] > current["created_at"]:
            newest[run["workflow_id"]] = run

    checks = {}
    for run in sorted(newest.values(), key=lambda r: r["path"]):
        workflow = run["path"].rsplit("/", 1)[-1]
        if run["status"] != "completed":
            die(f"{workflow} run {run['id']} for #{pr['number']} is still {run['status']} -- wait for it to finish")
        if run["conclusion"] != "success":
            die(f"{workflow} run {run['id']} for #{pr['number']} ended {run['conclusion']} -- "
                "required checks come from a pipeline that passed")
        app_id = gh_api(f"repos/{repo}/check-suites/{run['check_suite_id']}")["app"]["id"]
        jobs = gh_api(f"repos/{repo}/actions/runs/{run['id']}/jobs?per_page=100")["jobs"]
        passed = [job["name"] for job in jobs if job["conclusion"] == "success"]
        print(f"  {workflow} run {run['id']}: {len(passed)} jobs passed"
              f" ({len(jobs) - len(passed)} skipped or not run on a pull request)")
        for name in passed:
            checks[name] = app_id
    return sorted(checks.items())


def find_ruleset(repo):
    for summary in gh_api(f"repos/{repo}/rulesets?per_page=100"):
        if summary["name"] == RULESET_NAME:
            return gh_api(f"repos/{repo}/rulesets/{summary['id']}")
    return None


def current_checks(ruleset):
    if ruleset is None:
        return []
    for rule in ruleset["rules"]:
        if rule["type"] == "required_status_checks":
            return sorted((c["context"], c.get("integration_id")) for c in rule["parameters"]["required_status_checks"])
    return []


def as_checks(pairs):
    return [{"context": name, "integration_id": app_id} for name, app_id in pairs]


def main():
    parser = argparse.ArgumentParser(
        description="Show, and with --apply set, the checks a pull request into the default branch must pass.")
    parser.add_argument("--pr", type=int, help="the pull request whose CI defines the checks (default: newest open, else last merged)")
    parser.add_argument("--apply", action="store_true", help=f"create or update the '{RULESET_NAME}' ruleset")
    args = parser.parse_args()

    repo = repository()
    branch = gh_api(f"repos/{repo}")["default_branch"]
    pr = choose_pull_request(repo, branch, args.pr)
    state = "merged" if pr.get("merged_at") else pr["state"]
    print(f"{repo}: the checks #{pr['number']} ({pr['head']['ref']}, {state}) ran and passed")
    wanted = required_from_runs(repo, pr)

    ruleset = find_ruleset(repo)
    have = current_checks(ruleset)
    added = [c for c in wanted if c not in have]
    removed = [c for c in have if c not in wanted]

    print()
    print(f"required to merge into '{branch}' ({len(wanted)}):")
    for name, app_id in wanted:
        mark = "+" if (name, app_id) in added else " "
        print(f"  {mark} {name}   (app {app_id})")
    for name, app_id in removed:
        print(f"  - {name}   (app {app_id}; no longer run)")

    if ruleset is None:
        print(f"\nno '{RULESET_NAME}' ruleset yet: --apply creates it, with no bypass list")
    elif ruleset.get("bypass_actors"):
        print(f"\nwarning: '{RULESET_NAME}' has a bypass list, so it can be skipped -- "
              "remove it in Settings -> Rules -> Rulesets for the checks to hold for everyone")
    if ruleset is not None and ruleset.get("enforcement") != "active":
        print(f"warning: '{RULESET_NAME}' is {ruleset.get('enforcement')}, so it enforces nothing")

    if ruleset is not None and not added and not removed:
        print("\nthe ruleset already requires exactly these: nothing to change")
        return
    if not args.apply:
        print("\ndry run: nothing was changed -- run again with --apply to set these")
        return

    if ruleset is None:
        gh_api(f"repos/{repo}/rulesets", method="POST", body={
            "name": RULESET_NAME,
            "target": "branch",
            "enforcement": "active",
            "conditions": {"ref_name": {"include": ["~DEFAULT_BRANCH"], "exclude": []}},
            "bypass_actors": [],
            "rules": [{
                "type": "required_status_checks",
                "parameters": {
                    # Up to date with the branch before merging: the release
                    # after a merge refuses archives built before it moved.
                    "strict_required_status_checks_policy": True,
                    "do_not_enforce_on_create": False,
                    "required_status_checks": as_checks(wanted),
                },
            }],
        })
        print(f"\ncreated '{RULESET_NAME}' requiring {len(wanted)} checks")
        return

    # Only the check list changes; the ruleset's other settings are left as
    # they were set.
    rules = ruleset["rules"]
    for rule in rules:
        if rule["type"] == "required_status_checks":
            rule["parameters"]["required_status_checks"] = as_checks(wanted)
            break
    else:
        rules.append({
            "type": "required_status_checks",
            "parameters": {
                "strict_required_status_checks_policy": True,
                "do_not_enforce_on_create": False,
                "required_status_checks": as_checks(wanted),
            },
        })
    gh_api(f"repos/{repo}/rulesets/{ruleset['id']}", method="PUT", body={"rules": rules})
    print(f"\nupdated '{RULESET_NAME}': {len(added)} added, {len(removed)} removed, {len(wanted)} required")


if __name__ == "__main__":
    main()
