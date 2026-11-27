#!/usr/bin/env python3
"""required-checks.py -- keep `stable`'s required status checks in step with the pipeline.

    required-checks.py [--pr N] [--apply | --verify]

Shows -- and with --apply, sets -- the status checks a pull request into the
default branch must pass before it can merge, in a repository ruleset named
"Stable: CI must pass" that has NO bypass list: an admin can still merge
without an approval (the "Stable" ruleset's bypass), but nobody merges past a
red or unfinished pipeline (user decision, 2026-10-03).

--verify is the standing proof that the ruleset is in force (57m). It reads,
never writes, and exits 1 -- saying each reason -- when the ruleset is
missing, is not `active`, has any bypass actor -- or hides its bypass list,
as GitHub does from a caller without admin access -- does not require the
branch to be up to date, requires a list other than the pull request's
passing checks, or is not among the rules GitHub applies to the default
branch; it exits 0, printing the list, when it matches. A failure is the
user's to fix: --apply for a missing ruleset or a changed list, the
ruleset's settings for the rest, an admin's gh for a hidden bypass list. The ruleset was described as in force from 2026-10-03 and was never
created -- the dry run's "no ruleset yet" was a printed line, not a failure --
so the docs say it holds only once --verify says so.

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

Needs gh, signed in with rights to administer the repository for --apply and
for a --verify that can pass (only an admin is shown the bypass list); run it
from a checkout of the repository. Without --apply it changes nothing.

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
  - the ruleset is named RULESET_NAME and targets the default branch;
  - --verify reads the rules GitHub applies to the default branch from
    `rules/branches/<branch>`, each carrying the `ruleset_id` it came from;
  - a ruleset's GET carries `bypass_actors` only for a caller who can
    administer it, so --verify, missing the field, fails rather than reads
    it as an empty list.
"""

import argparse
import json
import subprocess
import sys

RULESET_NAME = "Stable: CI must pass"


def die(message):
    # stdout is block-buffered into a file and stderr is not: flushed first,
    # the refusal stays the last line of a log holding both, which is the line
    # `make pr-ci` reports as the verdict (57m).
    sys.stdout.flush()
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


def checks_rule(ruleset):
    """The ruleset's required_status_checks rule, or None."""
    if ruleset is None:
        return None
    for rule in ruleset.get("rules", []):
        if rule["type"] == "required_status_checks":
            return rule
    return None


def current_checks(ruleset):
    rule = checks_rule(ruleset)
    if rule is None:
        return []
    return sorted((c["context"], c.get("integration_id")) for c in rule["parameters"]["required_status_checks"])


def setting_problems(ruleset):
    """What keeps an existing ruleset from holding everyone, whatever its list:
    its enforcement, its bypass list, and whether it requires the branch to be
    up to date. Each is a setting --apply leaves as it finds it."""
    problems = []
    if ruleset.get("enforcement") != "active":
        problems.append(f"'{RULESET_NAME}' is {ruleset.get('enforcement')}, so it enforces nothing")
    # GitHub leaves `bypass_actors` out of a ruleset it shows a caller who
    # cannot administer it, so an absent list is "could not see", never "none".
    if "bypass_actors" not in ruleset:
        problems.append(f"cannot see '{RULESET_NAME}''s bypass list (gh is not signed in with admin access, and "
                        "GitHub shows the list to admins alone), so it may be skipped -- run --verify as a "
                        "repository admin")
    elif ruleset["bypass_actors"]:
        problems.append(f"'{RULESET_NAME}' has a bypass list ({len(ruleset['bypass_actors'])} actors), so it can "
                        "be skipped -- remove it in Settings -> Rules -> Rulesets for the checks to hold for everyone")
    rule = checks_rule(ruleset)
    if rule is not None and not rule["parameters"].get("strict_required_status_checks_policy"):
        problems.append(f"'{RULESET_NAME}' does not require the branch to be up to date before merging, so the "
                        "release's tree check can refuse the archives -- turn it on in Settings -> Rules -> Rulesets")
    return problems


def applies_to_branch(repo, branch, ruleset):
    """Whether GitHub applies the ruleset's required checks to the branch, as
    `rules/branches/<branch>` -- the rules in force there -- lists them."""
    rules = gh_api(f"repos/{repo}/rules/branches/{branch}?per_page=100") or []
    return any(rule["type"] == "required_status_checks" and rule.get("ruleset_id") == ruleset["id"]
               for rule in rules)


def verify(repo, branch, pr, wanted, ruleset):
    """--verify: exit 0 printing the list when the ruleset holds as designed,
    else exit 1 naming every reason. Reads only."""
    number = pr["number"]
    if ruleset is None:
        print(f"\nNOT in force: there is no '{RULESET_NAME}' ruleset, so nothing is required to merge into '{branch}'")
        print(f"needs the user: lib/scripts/required-checks.py --pr {number} --apply creates it "
              "(it changes the repository's settings)")
        print(f"required-checks: '{RULESET_NAME}' is not in force: there is no such ruleset -- "
              f"the user's --pr {number} --apply creates it")
        sys.exit(1)

    problems = setting_problems(ruleset)
    have = current_checks(ruleset)
    if checks_rule(ruleset) is None:
        problems.append(f"'{RULESET_NAME}' has no required-status-checks rule, so it requires no check")
    missing = [c for c in wanted if c not in have]
    extra = [c for c in have if c not in wanted]
    for name, app_id in missing:
        problems.append(f"'{name}' (app {app_id}) passed on #{number} but is not required")
    for name, app_id in extra:
        problems.append(f"'{name}' (app {app_id}) is required but did not run and pass on #{number}, "
                        "so a merge would wait for it forever")
    if ruleset.get("enforcement") == "active" and not applies_to_branch(repo, branch, ruleset):
        problems.append(f"GitHub does not apply '{RULESET_NAME}' to '{branch}': rules/branches/{branch} "
                        "lists no required checks from it -- its target must include the default branch")

    if problems:
        print(f"\nNOT in force as the pipeline needs ({len(problems)}):")
        for problem in problems:
            print(f"  - {problem}")
        if missing or extra or checks_rule(ruleset) is None:
            print(f"needs the user: once #{number}'s run has passed, "
                  f"lib/scripts/required-checks.py --pr {number} --apply sets the list")
        print(f"required-checks: '{RULESET_NAME}' is not in force as designed: {problems[0]}"
              + (f" (and {len(problems) - 1} more)" if len(problems) > 1 else ""))
        sys.exit(1)

    print(f"\n'{RULESET_NAME}' is in force on '{branch}': active, no bypass list, the branch required to be "
          f"up to date, and exactly the {len(wanted)} checks #{number} ran and passed:")
    for name, app_id in wanted:
        print(f"    {name}   (app {app_id})")
    print(f"required-checks: in force -- '{RULESET_NAME}' requires the {len(wanted)} checks #{number} passed, "
          "with no bypass and the branch up to date")


def as_checks(pairs):
    return [{"context": name, "integration_id": app_id} for name, app_id in pairs]


def main():
    parser = argparse.ArgumentParser(
        description="Show, and with --apply set, the checks a pull request into the default branch must pass.")
    parser.add_argument("--pr", type=int, help="the pull request whose CI defines the checks (default: newest open, else last merged)")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--apply", action="store_true", help=f"create or update the '{RULESET_NAME}' ruleset")
    mode.add_argument("--verify", action="store_true",
                      help=f"read only: exit 0 if '{RULESET_NAME}' is in force and requires exactly these checks, else 1")
    args = parser.parse_args()

    repo = repository()
    branch = gh_api(f"repos/{repo}")["default_branch"]
    pr = choose_pull_request(repo, branch, args.pr)
    state = "merged" if pr.get("merged_at") else pr["state"]
    print(f"{repo}: the checks #{pr['number']} ({pr['head']['ref']}, {state}) ran and passed")
    wanted = required_from_runs(repo, pr)

    ruleset = find_ruleset(repo)
    if args.verify:
        verify(repo, branch, pr, wanted, ruleset)
        return
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
        print(f"\nno '{RULESET_NAME}' ruleset yet, so nothing is enforced: no check is required to merge into "
              f"'{branch}' until --apply creates it, with no bypass list")
    else:
        problems = setting_problems(ruleset)
        if problems:
            print()
        for problem in problems:
            print(f"warning: {problem}")

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
