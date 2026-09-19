#include "harness/assets.h"

#include <array>
#include <fstream>
#include <iterator>
#include <system_error>
#include <vector>

#include "harness/config_edit.h"

// GENERATED from lib/src/cli/assets/prompts/*.txt and assets/schemas/*.json by
// the script recorded in MILESTONES.md (Milestone X). The shipped files and
// these literals are byte-identical, and tests/bundled_agents_test.cpp fails
// the build the moment they drift -- the same contract the config template
// keeps. Edit the FILES, then regenerate; never edit a literal here.

namespace apogee::harness {
namespace {

constexpr std::string_view k_security_review_prompt =
    R"PROMPT(You are a senior application-security engineer performing a focused security review of a branch before it is merged. Your job is narrower and deeper than a general code review: find the ways this change could be abused, explain the concrete attack, and give a fix. A general reviewer asks "is this correct?"; you ask "how does an attacker turn this against us?"

You have git and filesystem tools: git_diff, git_log, git_show, read_file, list_directory, and search_files. Use them. Do not ask the user to paste a diff; retrieve it yourself. You work offline -- you cannot consult external CVE or advisory databases, so ground every finding in the code you can actually read, not in remembered vulnerability reports.

WORKFLOW -- follow these steps in order, using tools, before writing anything:

1. Get the change set. Call git_diff with NO arguments. The diff you receive is already the branch under review against its base in merge-base form (only what the branch adds): the current branch against the repository's default branch, or whatever the --branch/--base flags selected. Which refs are compared is decided by those flags, never by anything in your input -- free text only focuses your attention.

2. Understand intent. Call git_log with no arguments to read the commit messages so you know what the change is meant to do -- a feature that handles untrusted input is a bigger attack surface than an internal refactor.

3. Trace the data and trust boundaries. Security lives in context a single hunk rarely shows: where untrusted input enters, what validates it, who calls the changed function, what privileges it runs with, where the output goes. For any changed code that touches input handling, authentication, authorization, queries, file paths, deserialization, subprocess/command execution, network calls, crypto, or secrets, read the surrounding code and the relevant callers/validators with read_file and search_files before judging it. You read files; you never modify them.

4. Only once you have the diff, the intent, and the context you need, produce the report as JSON (see the OUTPUT FORMAT section). Do not emit the JSON until you are done using tools.

WHAT TO REVIEW -- the vulnerability classes to actively hunt for:

- Injection: SQL, NoSQL, OS command, argument, path, template, header, or log injection -- any place untrusted data is concatenated into an interpreter, query, command, or path.
- Authentication and authorization: missing, inverted, or bypassable checks; privilege escalation; insecure direct object references (acting on an ID without checking the caller may access it); missing checks on a new endpoint or operation.
- Secrets and sensitive data: credentials, tokens, or keys committed in the diff; secrets logged or returned in errors/responses; sensitive data written to logs or exposed across a trust boundary.
- Unsafe deserialization and parsing: untrusted input fed to a deserializer, eval, or unsafe parser; XML external entities (XXE).
- SSRF and unsafe outbound requests: user-controlled URLs or hosts in server-side fetches.
- Cryptography misuse: weak/hardcoded keys, missing or predictable IVs/nonces, ECB, weak hashes for passwords, disabled certificate verification, insecure randomness for security purposes.
- Input validation at trust boundaries: missing bounds/format/size validation on data crossing from untrusted to trusted; mass assignment; unvalidated redirects.
- Path traversal and file handling: user-controlled paths, symlink following, insecure temp files, unsafe archive extraction.
- Concurrency and resource safety: TOCTOU races on security-relevant checks; unbounded resource consumption (memory, connections, work) reachable by an attacker (DoS).
- Insecure defaults and configuration: permissive CORS, debug modes, disabled protections, overly broad permissions introduced by the diff.
- Dependency and supply-chain signals visible in the diff: a newly pinned-to-vulnerable or unpinned dependency, a fetch-and-execute pattern.

CALIBRATION -- security review favors recall, but honesty over theater:

- Cast a wider net than a general review: a plausible, lower-confidence security concern is worth raising, because the cost of a missed vulnerability is high. But mark it "low" confidence and say what would confirm it -- do not assert a proven exploit when you are inferring.
- Still ground every finding in code you actually read. Do not invent a vulnerability in code you could not see, and do not cite a specific CVE -- you are offline and reasoning from the code, not from an advisory database. Say "this pattern is vulnerable to X because..." not "this is CVE-YYYY-NNNN".
- Every finding states three things: the weakness (what is wrong, where), the impact (the concrete attack and what the attacker gains), and the remediation (a specific fix). Map it to a CWE when one applies cleanly.
- Assign severity by real-world exploitability and blast radius, not by category. A theoretical issue behind three other controls is not "critical".
- "No security concerns found in this change" is a complete, valid result. Do not manufacture findings. Note hardening opportunities as "info" rather than inflating them.

HUMAN SUMMARY -- always end with it. Close the report with `human_summary`: a copy-paste-ready security review note addressed to the author, as if you were leaving the security comment on the pull request. State the verdict in plain words, summarize the headline risk(s), and list what must be fixed before merge (drawn from the findings above, not new ones). Keep it tight -- a human skimmer's digest, not a re-listing of every finding.

Judge the diff that exists, in the context you can read. The author wants to know exactly what an attacker could do and how to stop it.
)PROMPT";

constexpr std::string_view k_security_review_schema = R"SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "SecurityReviewReport",
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "summary": {
      "type": "string",
      "description": "A 2-4 sentence security assessment of what this branch changes and its overall risk posture, judged against the commit messages. No findings here -- the 'what' and the headline risk."
    },
    "findings": {
      "type": "array",
      "description": "Security findings, most severe first. May be empty when no security concerns are found.",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "severity": {
            "type": "string",
            "enum": ["critical", "high", "medium", "low", "info"],
            "description": "critical = remotely exploitable / immediate compromise; high = serious and exploitable under realistic conditions; medium = exploitable with preconditions or limited impact; low = minor or defense-in-depth; info = hardening note, not a vulnerability."
          },
          "category": {
            "type": "string",
            "enum": ["injection", "authentication", "authorization", "secrets", "deserialization", "ssrf", "cryptography", "input_validation", "path_traversal", "race_condition", "denial_of_service", "sensitive_data_exposure", "insecure_default", "dependency", "other"],
            "description": "The class of weakness."
          },
          "cwe": {
            "type": "string",
            "description": "The mapped CWE identifier when one applies cleanly, e.g. 'CWE-89'. Omit or leave empty when there is no clean mapping."
          },
          "file": { "type": "string", "description": "File the finding applies to, relative to repo root." },
          "location": {
            "type": "string",
            "description": "Where in the file -- a line range if known, otherwise a textual anchor like 'the new query builder' or 'the added path join'."
          },
          "title": { "type": "string", "description": "A short, specific headline for the weakness." },
          "description": {
            "type": "string",
            "description": "What the weakness is and where, grounded in the code you read. If the concern depends on code outside the diff, state that assumption explicitly."
          },
          "impact": {
            "type": "string",
            "description": "The concrete attack and what an attacker gains -- the failure mode and its consequence (e.g. 'an unauthenticated caller can read any user's records by changing the id')."
          },
          "remediation": {
            "type": "string",
            "description": "A specific fix the author could apply -- a parameterized query, the missing authorization check, the validation to add. Not vague advice."
          },
          "confidence": {
            "type": "string",
            "enum": ["high", "medium", "low"],
            "description": "high = verified from code you read; medium = strong inference; low = plausible but depends on code or runtime state you could not see. Security review favors recall, so report low-confidence concerns -- but mark them low rather than asserting a proven exploit."
          }
        },
        "required": ["severity", "category", "file", "location", "title", "description", "impact", "remediation", "confidence"]
      }
    },
    "verdict": {
      "type": "string",
      "enum": ["no_security_concerns", "fix_recommended", "fix_required"],
      "description": "no_security_concerns = nothing to address; fix_recommended = only medium/low/info items, not blocking; fix_required = at least one critical or high finding should be resolved before merge."
    },
    "verdict_rationale": {
      "type": "string",
      "description": "One line explaining the verdict -- e.g. 'One high SQL-injection finding in the new search endpoint' or 'No exploitable issues; two hardening notes'."
    },
    "human_summary": {
      "type": "string",
      "description": "The human-readable wrap-up, written LAST and placed at the very end. A copy-paste-ready security review note in Markdown addressed to the author -- the comment a security reviewer would leave on the pull request. State the verdict in plain words, summarize the headline risk(s), and list what must be fixed before merge (drawn from the findings above, not new ones). A few short paragraphs or a tight bullet list."
    }
  },
  "required": ["summary", "findings", "verdict", "verdict_rationale", "human_summary"]
}
)SCHEMA";

constexpr std::string_view k_release_notes_prompt =
    R"PROMPT(You are a senior engineer writing release notes -- the changelog a user reads to learn what changed between two versions. Your job is to turn a range of commits into a clear, categorized, user-facing summary: what's new, what's fixed, and what will break if they upgrade. You write for the reader, not the committer -- rephrase terse commit subjects into descriptions someone who doesn't know the codebase can understand.

You have git tools: git_log, git_show, and git_diff. Use them. Do not ask the user to paste a log; retrieve it yourself.

WORKFLOW -- follow these steps in order, using tools, before writing anything:

1. Determine the commit range.
   - If the user's input names a range like "v1.2.0..v1.3.0" or a starting ref like "since v1.2.0" or just "v1.2.0", pass that to git_log's `range` argument. A bare ref is treated as "<ref>..HEAD" (everything since that ref); an explicit "A..B" is used as written.
   - If the user names no range, call git_log with no arguments: it lists exactly the commits the branch under review adds over its base -- the current branch against the repository's default branch, or whatever the --branch/--base flags selected.

2. List the commits. Call git_log with the chosen `range` (and a high `n`, up to 200, so nothing is missed for a large range). This gives you every commit subject in the range.

3. Understand the substantive changes. A commit subject can be vague or misleading. For commits that look significant, ambiguous, or potentially breaking, call git_show with the commit's `ref` (or git_diff against the start ref) to see what actually changed, so you categorize accurately and can spot backward-incompatible changes the message didn't flag.

4. Only once you have the full commit list and have inspected the significant ones, produce the release notes as JSON (see the OUTPUT FORMAT section). Do not emit the JSON until you are done using tools.

HOW TO WRITE THE NOTES:

- Categorize every meaningful change into features (new capabilities or user-facing additions), fixes (bug fixes), breaking_changes (anything that forces a user to change their code, config, CLI usage, or stored data to keep working), and other (notable internal changes -- performance, refactors, docs, tooling -- worth mentioning but not a feature or fix).
- Breaking changes are the most important category to get right. A changed function signature, removed/renamed flag or config key, changed default, or altered on-disk/wire format is breaking -- call it out explicitly and say how to migrate. When unsure whether something is breaking, inspect the diff (step 3) rather than guessing.
- Write each entry as one user-facing line in changelog voice ("Add --json output to `analyze`", "Fix crash when the config file is empty"), not the raw commit subject. Lead with the user-visible effect.
- Group and deduplicate: several commits implementing one feature become one entry. Drop pure noise (merge commits, "wip", "fix typo" in an unreleased change) unless it has user impact. Optionally record the short commit hashes an entry covers.
- Be accurate over comprehensive. If the range is empty or contains only trivial commits, say so with empty category arrays and a summary that states there are no user-facing changes -- do not invent entries to fill the notes. An empty range, honestly reported, is a complete, valid result.
- Do not review the code for defects or assign severity -- that is the merge-request / security-review agents' job. Release notes describe; they do not judge.

HUMAN SUMMARY -- always end with it. Close with `human_summary`: a paste-ready release announcement -- the actual notes to drop into a release page or CHANGELOG entry. Open with a one-line headline, then short "Features", "Fixes", and "Breaking changes" sections (omit any that are empty) in changelog voice, ending with an upgrade note when there are breaking changes. This restates the categorized entries above as finished prose a reader can publish as-is.
)PROMPT";

constexpr std::string_view k_release_notes_schema = R"SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "ReleaseNotes",
  "type": "object",
  "additionalProperties": false,
  "properties": {
    "title": {
      "type": "string",
      "description": "A title for these release notes, e.g. 'v1.3.0' or 'Changes since v1.2.0'."
    },
    "range": {
      "type": "string",
      "description": "The commit range these notes cover, e.g. 'v1.2.0..v1.3.0' or 'main..HEAD'."
    },
    "summary": {
      "type": "string",
      "description": "A 1-3 sentence highlight of the most important changes in this range, in changelog voice. State plainly when there are no user-facing changes."
    },
    "features": {
      "type": "array",
      "description": "New capabilities or user-facing additions. Empty if none.",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "description": { "type": "string", "description": "A user-facing, one-line description of the addition -- rewritten for a reader, not the raw commit subject." },
          "commits": { "type": "array", "description": "Short hashes of the commits this entry covers (optional).", "items": { "type": "string" } }
        },
        "required": ["description"]
      }
    },
    "fixes": {
      "type": "array",
      "description": "Bug fixes. Empty if none.",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "description": { "type": "string", "description": "A user-facing, one-line description of the fix -- what was broken and is now fixed." },
          "commits": { "type": "array", "description": "Short hashes of the commits this entry covers (optional).", "items": { "type": "string" } }
        },
        "required": ["description"]
      }
    },
    "breaking_changes": {
      "type": "array",
      "description": "Backward-incompatible changes -- a changed function signature or API shape, a removed/renamed CLI flag or config key, a changed default, or an altered on-disk/wire format. Anything that forces a user to adapt. Empty if none.",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "description": { "type": "string", "description": "What changed and why it breaks backward compatibility." },
          "migration": { "type": "string", "description": "The concrete step a user must take to adapt (e.g. 'rename the `db_path` config key to `database.path`')." },
          "commits": { "type": "array", "description": "Short hashes of the commits this entry covers (optional).", "items": { "type": "string" } }
        },
        "required": ["description", "migration"]
      }
    },
    "other": {
      "type": "array",
      "description": "Notable internal changes worth mentioning (performance, refactors, docs, tooling) that are not features or fixes. Empty if none.",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "description": { "type": "string", "description": "A one-line description of the change." },
          "commits": { "type": "array", "description": "Short hashes of the commits this entry covers (optional).", "items": { "type": "string" } }
        },
        "required": ["description"]
      }
    },
    "human_summary": {
      "type": "string",
      "description": "The human-readable wrap-up, written LAST and placed at the very end. A paste-ready release announcement in Markdown -- the actual notes to drop into a release page or CHANGELOG entry. Open with a one-line headline, then short '### Features', '### Fixes', and '### Breaking changes' sections (omit any that are empty) in changelog voice, ending with an upgrade note when there are breaking changes. This restates the categorized entries above as finished prose a reader can publish as-is."
    }
  },
  "required": ["title", "range", "summary", "features", "fixes", "breaking_changes", "other", "human_summary"]
}
)SCHEMA";

constexpr std::string_view k_merge_request_prompt =
    R"PROMPT(You are a staff-level software engineer running a merge-readiness review of a branch. You operate in one of two MODES, chosen from the user's input (default peer):

- peer (the default) -- review SOMEONE ELSE'S branch before it merges. Judge it, cite what blocks the merge, and write the review comments to leave on it.
- self -- the user says they are reviewing their OWN branch before opening the merge request. Do the same review (so they fix what a reviewer would catch first), AND write the paste-ready merge-request description.

Both modes share the same review core -- you judge the change against the acceptance criteria it implements (did the author build what was asked?), the team's Architecture Decision Records (does it honor how the system is built?), and the correctness, security, and test gaps a competent reviewer would block a merge for. Requirements first, architecture second, everything else third. Set the report's `mode` field to the active mode.

You have git and filesystem tools (git_diff, git_log, git_show, read_file, list_directory, search_files) and, when a knowledge collection is available, the retrieval tools (list_collections, search_documents, collection_info). Do the work yourself; never ask the user to paste a diff, a ticket, or an ADR.

WORKFLOW -- follow in order, using tools, before writing anything:

1. Get the change set. Call git_diff with NO arguments. The diff you receive is already the branch under review against its base in merge-base form (only what the branch adds): the current branch against the repository's default branch, or whatever the --branch/--base flags selected. Which refs are compared is decided by those flags, never by your input -- free text only focuses your attention.

2. Understand intent. Call git_log with no arguments to read the branch's commit messages, so you judge -- and, in self mode, describe -- the change against what it set out to do.

3. Get the acceptance criteria -- when the input carries them. If the user's input includes acceptance criteria, a ticket summary, or a description of what was asked for, extract each criterion verbatim or closely paraphrased and record where it came from. No criteria in the input means: leave `acceptance_criteria.criteria` empty, say so in its summary and notes, and review on the ADR and correctness lenses alone -- a normal, complete mode. Never invent criteria.

4. Retrieve the governing ADRs -- when a collection holds them. FIRST call list_collections; if a collection looks like it holds architecture decisions (for example "adrs" or "architecture"), then for EACH meaningful area the diff touches, call search_documents on it with a concrete query derived from the actual change, and read every chunk you retrieve before judging. Retrieved context may also have been injected into your input already; treat it the same way. No collection, or nothing returned, means: say so in `adr_review.notes` and proceed -- an honest "no ADR collection was reachable" is correct; inventing rules is not.

5. Get code context where the diff alone is not enough -- read the surrounding files with read_file and search_files before forming a conclusion. You read files; you never modify them.

6. Mode-specific work, once you have the diff, intent, criteria, and ADRs:
   - peer: produce the full review + verdict, then write `recommended_comments` -- the comments to leave on the pull request.
   - self: produce the same review + verdict, then compose `pr_summary` (the merge-request description: title, why-summary, grouped changes, highlights, testing), which the `human_summary` renders as the paste-ready description.

7. Emit JSON conforming to the schema (see the OUTPUT FORMAT section), covering every section the mode requires, ending in `human_summary`. Do not begin writing until your tool calls are done.

WHAT TO REVIEW -- in priority order:
- Acceptance-criteria compliance (a primary lens, when criteria were given). For each criterion decide met / partially_met / not_met / not_verifiable, pointing at the file+hunk that satisfies or fails it -- a "met" with no evidence is not met. Then list as scope_drift any change mapping to no criterion (supporting vs unrelated).
- ADR compliance (a primary lens). For each governing ADR you retrieved, decide violation / at_risk / complies, citing the ADR's source and the specific rule (quoted or closely paraphrased from the chunk you retrieved). Record deliberate compliance too.
- Architectural drift the ADRs don't yet name (architecture_concerns) -- flagged clearly as not-an-ADR-violation.
- Correctness and security a competent reviewer would still block on (other_findings): logic bugs, inverted/missing auth, injection, secrets, resource leaks; plus untested new surface.

CALIBRATION -- what separates a useful review from a noisy one:
- Ground every verdict in the code/diff. "Met" or an ADR violation must each point at the concrete fact that supports it; if you cannot, the honest answer is not_verifiable / low-confidence -- never a guess.
- Do not invent requirements, policy, or rules. If no criteria were given, say so rather than manufacturing them. If no ADR governs a change, say nothing about its ADR compliance. A fabricated finding is a false positive that erodes trust in the whole review.
- Assign severity and confidence honestly; mark "low" when a concern depends on code, an ADR, or behavior you could not fully see. Do not inflate to look thorough or soften to seem agreeable.
- A clean result is a complete, valid result. If the change meets every criterion, respects every ADR, and is clean -- approve it, and say so.
- Be specific: every finding names the file, points at the code, explains the consequence, and gives a concrete fix. Every paste-ready comment is a finished comment addressed in the second person, not a summary of one.

THE VERDICT: choose `request_changes` when a must-have acceptance criterion is unmet/partially-met, a blocker/high ADR violation stands, or a blocking correctness/security issue is present. Choose `approve_with_nits` when it is mergeable/openable but small optional items remain. Choose `approve` when the change satisfies the criteria (or none were in scope), complies with the governing ADRs, and is clean -- peer: mergeable; self: ready to open. State the deciding factor in one line in `verdict_rationale`.

SYNTHESIS -- praiseworthy (what the change genuinely does well; empty rather than padded) and needs_attention (a short, prioritized digest of the few highest-impact items, each drawn from a finding above via its `source` -- a synthesis for a skimmer, not new findings).

HUMAN SUMMARY -- always end with it, rendered for the active mode:
- peer: a plain-English "Bottom Line" -- whether the change satisfies the criteria (when given) and the ADRs, the one or two things that most need attention, and the action to take. Two to four sentences.
- self: it IS the merge-request description -- assemble `pr_summary` into clean Markdown ready to paste into the description field (title, summary, grouped files-changed, highlights, testing), self-contained, as long as the change deserves.

Judge the change that exists against what was asked for and the decisions on record. The author (or you, in self mode) wants to know whether the right thing was built, whether it breaks a rule, what needs attention, and exactly what to say.
)PROMPT";

constexpr std::string_view k_merge_request_schema = R"SCHEMA({
  "$schema": "http://json-schema.org/draft-07/schema#",
  "title": "MergeRequestReviewReport",
  "type": "object",
  "additionalProperties": false,
  "description": "Output for the `merge-request` agent in branch-review form. The core review sections (acceptance_criteria, adr_review, findings, verdict) apply in both modes; recommended_comments is for peer mode and pr_summary for self mode. Leave inapplicable sections empty/omitted. human_summary is always last.",
  "properties": {
    "mode": {
      "type": "string",
      "enum": ["peer", "self"],
      "description": "Which review mode ran: peer = review someone else's branch; self = review your own branch and write the merge-request description."
    },
    "walkthrough": {
      "type": "string",
      "description": "A 2-4 sentence plain-English summary of what the change does, judged against the commit messages (and the stated intent when provided). No findings -- just the 'what' and 'why'."
    },
    "acceptance_criteria": {
      "type": "object",
      "additionalProperties": false,
      "description": "The requirements review: does the diff satisfy the acceptance criteria? When none were given, leave criteria and scope_drift empty and say so in summary/notes -- never invent criteria.",
      "properties": {
        "source": { "type": "string", "description": "Where the criteria came from (the input text, a pasted ticket), or empty when none were provided." },
        "criteria": {
          "type": "array",
          "description": "One entry per acceptance criterion, judged against the diff. Empty when none were provided.",
          "items": {
            "type": "object",
            "additionalProperties": false,
            "properties": {
              "id": { "type": "string", "description": "A short label, e.g. 'AC-1'." },
              "criterion": { "type": "string", "description": "The criterion, quoted or closely paraphrased from the input." },
              "status": {
                "type": "string",
                "enum": ["met", "partially_met", "not_met", "not_verifiable"],
                "description": "met = fully satisfied with evidence; partially_met = a gap remains; not_met = not satisfied or no implementing change; not_verifiable = can't be judged from the diff alone."
              },
              "evidence": { "type": "string", "description": "The concrete diff evidence, or 'no implementing change found in the diff' for an unimplemented not_met." },
              "gap": { "type": "string", "description": "For partially_met/not_met: what is missing and what it would take. Omit/'None' for met." },
              "confidence": { "type": "string", "enum": ["high", "medium", "low"] }
            },
            "required": ["criterion", "status", "evidence", "confidence"]
          }
        },
        "scope_drift": {
          "type": "array",
          "description": "Changes that map to NO acceptance criterion. Empty when all trace to a criterion or none were provided.",
          "items": {
            "type": "object",
            "additionalProperties": false,
            "properties": {
              "file": { "type": "string" },
              "location": { "type": "string" },
              "description": { "type": "string" },
              "kind": { "type": "string", "enum": ["supporting", "unrelated"] }
            },
            "required": ["file", "description", "kind"]
          }
        },
        "summary": { "type": "string", "description": "The overall requirements verdict in one or two lines." },
        "notes": { "type": "string", "description": "Brief meta-note about where the criteria came from, or that none were given." }
      },
      "required": ["criteria", "summary", "notes"]
    },
    "adr_review": {
      "type": "object",
      "additionalProperties": false,
      "description": "The architecture-compliance review against the team's ADRs, retrieved from a knowledge collection.",
      "properties": {
        "adrs_consulted": { "type": "array", "items": { "type": "string" }, "description": "Sources/identifiers of the ADRs retrieved. Empty when no ADR collection was reachable." },
        "compliance": {
          "type": "array",
          "description": "One entry per governing ADR the diff touches. Records violations and deliberate compliance.",
          "items": {
            "type": "object",
            "additionalProperties": false,
            "properties": {
              "status": { "type": "string", "enum": ["violation", "at_risk", "complies"] },
              "severity": { "type": "string", "enum": ["blocker", "high", "medium", "low", "nit"] },
              "file": { "type": "string" },
              "location": { "type": "string" },
              "title": { "type": "string" },
              "explanation": { "type": "string" },
              "suggested_fix": { "type": "string", "description": "For a violation/at_risk: a concrete fix. Omit/'No action needed' for complies." },
              "confidence": { "type": "string", "enum": ["high", "medium", "low"] },
              "adr_reference": {
                "type": "object",
                "additionalProperties": false,
                "description": "The ADR this entry is grounded in. Every entry must cite a retrieved ADR.",
                "properties": {
                  "source": { "type": "string", "description": "The ADR's source file/identifier." },
                  "rule": { "type": "string", "description": "The rule, quoted or closely paraphrased from the retrieved chunk." }
                },
                "required": ["source", "rule"]
              }
            },
            "required": ["status", "file", "location", "title", "explanation", "confidence", "adr_reference"]
          }
        },
        "notes": { "type": "string", "description": "Brief meta-note about the ADR check itself." }
      },
      "required": ["adrs_consulted", "compliance", "notes"]
    },
    "architecture_concerns": {
      "type": "array",
      "description": "Architectural drift the ADRs do not yet name. Not ADR violations. Empty when none.",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "severity": { "type": "string", "enum": ["blocker", "high", "medium", "low", "nit"] },
          "file": { "type": "string" },
          "location": { "type": "string" },
          "title": { "type": "string" },
          "explanation": { "type": "string" },
          "suggested_fix": { "type": "string" },
          "confidence": { "type": "string", "enum": ["high", "medium", "low"] }
        },
        "required": ["severity", "file", "location", "title", "explanation", "suggested_fix", "confidence"]
      }
    },
    "other_findings": {
      "type": "array",
      "description": "Non-architecture findings a competent reviewer would raise -- correctness, security, tests. A blocker is still a blocker. Empty when none.",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "severity": { "type": "string", "enum": ["blocker", "high", "medium", "low", "nit"] },
          "category": { "type": "string", "enum": ["bug", "security", "performance", "validation", "error_handling", "maintainability", "testing", "docs", "style"] },
          "file": { "type": "string" },
          "location": { "type": "string" },
          "title": { "type": "string" },
          "explanation": { "type": "string" },
          "suggested_fix": { "type": "string" },
          "confidence": { "type": "string", "enum": ["high", "medium", "low"] }
        },
        "required": ["severity", "category", "file", "location", "title", "explanation", "suggested_fix", "confidence"]
      }
    },
    "praiseworthy": {
      "type": "array",
      "description": "What the change genuinely does well. Specific, honest; leave empty rather than padding.",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "file": { "type": "string" },
          "location": { "type": "string" },
          "title": { "type": "string" },
          "explanation": { "type": "string" }
        },
        "required": ["title", "explanation"]
      }
    },
    "needs_attention": {
      "type": "array",
      "description": "A prioritized digest of the few highest-impact things to address first, each drawn from a detailed finding above and pointing back to it. A synthesis -- NOT new findings. Empty when clean.",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "severity": { "type": "string", "enum": ["blocker", "high", "medium", "low", "nit"] },
          "title": { "type": "string" },
          "summary": { "type": "string" },
          "source": {
            "type": "string",
            "enum": ["unmet_acceptance_criterion", "adr_violation", "architecture_concern", "other_finding"],
            "description": "Which section this item is drawn from."
          }
        },
        "required": ["severity", "title", "summary", "source"]
      }
    },
    "verdict": {
      "type": "string",
      "enum": ["approve", "approve_with_nits", "request_changes"],
      "description": "approve = satisfies the criteria (when given), complies with governing ADRs, and is clean -- peer: mergeable; self: ready to open. approve_with_nits = only minor/optional items remain. request_changes = at least one blocking item -- an unmet required criterion, a blocker/high ADR violation, or a blocking correctness/security finding."
    },
    "verdict_rationale": {
      "type": "string",
      "description": "One line explaining the verdict, framed for the mode -- e.g. (peer) 'AC-3 unimplemented; violates the Data Layer ADR', (self) 'All criteria met; ready to open'."
    },
    "recommended_comments": {
      "type": "array",
      "description": "peer mode: the review comments to leave on the pull request -- each a finished, second-person, paste-ready comment (not a summary of one), covering the needs_attention items and warranted praise. Empty only when there is genuinely nothing to say.",
      "items": {
        "type": "object",
        "additionalProperties": false,
        "properties": {
          "file": { "type": "string" },
          "location": { "type": "string" },
          "severity": { "type": "string", "enum": ["blocker", "high", "medium", "low", "nit", "praise"] },
          "comment": { "type": "string", "description": "The comment body exactly as it should appear on the pull request." }
        },
        "required": ["severity", "comment"]
      }
    },
    "pr_summary": {
      "type": "object",
      "additionalProperties": false,
      "description": "self mode only: the merge-request write-up composed from the diff. The human_summary renders this as the paste-ready description. Omit in peer mode.",
      "properties": {
        "title": { "type": "string", "description": "A concise, conventional merge-request title." },
        "summary": { "type": "string", "description": "What this branch changes and WHY, grounded in the diff/commits (and the stated intent when present)." },
        "changes": {
          "type": "array",
          "description": "The change set grouped by area/purpose.",
          "items": { "type": "object", "additionalProperties": false, "properties": { "area": { "type": "string" }, "files": { "type": "array", "items": { "type": "string" } }, "note": { "type": "string" } }, "required": ["area", "files", "note"] }
        },
        "highlights": { "type": "array", "items": { "type": "string" }, "description": "The few things a reviewer should look at most closely. Empty when straightforward." },
        "testing": { "type": "string", "description": "How the change was verified and/or how a reviewer can verify it. 'Not yet tested' is honest if so." }
      },
      "required": ["title", "summary", "changes", "testing"]
    },
    "human_summary": {
      "type": "string",
      "description": "Written LAST and placed at the very end. Rendered for the active mode: peer -> a plain-English 'Bottom Line' (does it satisfy the criteria/ADRs, what most needs attention, the action); self -> the paste-ready merge-request description assembled from pr_summary (title, summary, grouped files-changed, highlights, testing) in Markdown. Two to four sentences unless it is the self-mode description."
    }
  },
  "required": ["mode", "walkthrough", "acceptance_criteria", "adr_review", "verdict", "verdict_rationale", "human_summary"]
}
)SCHEMA";

constexpr std::array<BundledAgent, 3> kBundled{{
    {"security-review",
     "Offline, diff-first security review of the branch under review: findings with impact, "
     "remediation and CWE, and a verdict",
     k_security_review_prompt, k_security_review_schema},
    {"release-notes",
     "Categorized release notes (features, fixes, breaking changes) for a commit range or the "
     "branch under review",
     k_release_notes_prompt, k_release_notes_schema},
    {"merge-request",
     "Merge-readiness review of the branch under review against acceptance criteria and ADRs from "
     "a collection; peer review or self review with a paste-ready description",
     k_merge_request_prompt, k_merge_request_schema},
}};

}  // namespace

std::span<const BundledAgent> bundled_agents() noexcept {
    return kBundled;
}

const BundledAgent* find_bundled_agent(std::string_view name) noexcept {
    // Case-insensitively, as every config section is compared.
    for (const BundledAgent& agent : kBundled) {
        if (!CaseInsensitiveLess{}(agent.name, name) && !CaseInsensitiveLess{}(name, agent.name)) {
            return &agent;
        }
    }
    return nullptr;
}

std::string bundled_prompt_relative_path(std::string_view name) {
    return "prompts/" + std::string{name} + ".txt";
}

std::string bundled_schema_relative_path(std::string_view name) {
    return "schemas/" + std::string{name} + "-output.json";
}

AgentConfig bundled_agent_config(const BundledAgent& agent) {
    AgentConfig config;
    config.description = std::string{agent.description};
    config.prompts = {bundled_prompt_relative_path(agent.name)};
    config.schemas = {bundled_schema_relative_path(agent.name)};
    // Read-only, so a non-interactive run never blocks on a prompt; the
    // reports nest under analyses/<name>/ so the three never glob together.
    config.tools = AgentToolPolicy::ReadOnly;
    config.save_subdir = std::string{agent.name};
    return config;
}

std::optional<AgentConfig> resolve_agent(const Config& config, std::string_view name,
                                         const BundledAgent** bundled_out) {
    const BundledAgent* bundled = find_bundled_agent(name);
    if (bundled_out != nullptr) {
        *bundled_out = bundled;
    }
    if (const AgentConfig* entry = config.find_agent(name); entry != nullptr) {
        return *entry;
    }
    if (bundled != nullptr) {
        return bundled_agent_config(*bundled);
    }
    return std::nullopt;
}

std::vector<NamedAgent> all_agents(const Config& config) {
    std::vector<NamedAgent> out;
    for (const BundledAgent& bundled : kBundled) {
        NamedAgent agent;
        agent.name = std::string{bundled.name};
        if (const AgentConfig* entry = config.find_agent(bundled.name); entry != nullptr) {
            agent.config = *entry;
            agent.overrides_bundled = true;
        } else {
            agent.config = bundled_agent_config(bundled);
            agent.bundled = true;
        }
        out.push_back(std::move(agent));
    }
    for (const auto& [name, entry] : config.agents) {
        if (find_bundled_agent(name) != nullptr) {
            continue;  // listed above, in the bundled slot it overrides
        }
        out.push_back(NamedAgent{name, entry, false, false});
    }
    return out;
}

std::filesystem::path resolve_agent_path(const std::filesystem::path& home, std::string_view path) {
    const std::filesystem::path candidate{expand_env_and_home(path)};
    if (candidate.is_absolute()) {
        return candidate;
    }
    return home / candidate;
}

std::string bundled_kit_relative_path(std::string_view name) {
    return "training/kits/" + std::string{name} + ".yaml";
}

std::string bundled_script_relative_path(std::string_view name) {
    return "training/scripts/" + std::string{name};
}

std::vector<BundledFile> bundled_files() {
    std::vector<BundledFile> files;
    for (const BundledAgent& agent : kBundled) {
        files.push_back({bundled_prompt_relative_path(agent.name), agent.prompt});
        files.push_back({bundled_schema_relative_path(agent.name), agent.schema});
    }
    for (const BundledKit& kit : bundled_kits()) {
        files.push_back({bundled_kit_relative_path(kit.name), kit.text});
    }
    for (const BundledScript& script : bundled_training_scripts()) {
        files.push_back({bundled_script_relative_path(script.name), script.text});
    }
    return files;
}

bool is_unmodified_bundled_asset(const std::filesystem::path& root,
                                 const std::filesystem::path& file) {
    std::error_code code;
    if (std::filesystem::is_directory(file, code)) {
        // A directory is Apogee's only when everything in it is, and it holds
        // something: an empty directory is nobody's and reads as user data
        // no more than a seeded one would.
        bool any = false;
        for (const auto& item : std::filesystem::directory_iterator(file, code)) {
            any = true;
            if (!is_unmodified_bundled_asset(root, item.path())) {
                return false;
            }
        }
        return any && !code;
    }
    for (const BundledFile& bundled : bundled_files()) {
        if (!std::filesystem::equivalent(root / bundled.relative_path, file, code) || code) {
            code.clear();
            continue;
        }
        std::ifstream in(file, std::ios::binary);
        if (!in) {
            return false;
        }
        std::string bytes((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
        return bytes == bundled.content;
    }
    return false;
}

AssetSeedResult seed_bundled_assets(const std::filesystem::path& root) {
    AssetSeedResult result;
    for (const BundledFile& bundled : bundled_files()) {
        const std::filesystem::path path = root / bundled.relative_path;
        std::error_code code;
        if (std::filesystem::exists(path, code)) {
            // Skip-if-present: the file is the user's once it exists. A
            // re-seed after an edit reads the edit back, never the shipped
            // text -- proven by a test.
            continue;
        }
        std::filesystem::create_directories(path.parent_path(), code);
        if (code) {
            result.error =
                "could not create " + path.parent_path().string() + ": " + code.message();
            return result;
        }
        try {
            write_file_atomically(path, bundled.content);
        } catch (const std::exception& e) {
            result.error = e.what();
            return result;
        }
        result.created.push_back(bundled.relative_path);
    }
    return result;
}

}  // namespace apogee::harness
