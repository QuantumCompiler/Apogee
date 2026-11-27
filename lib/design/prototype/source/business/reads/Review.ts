import { JSONValue } from '../../data/protocol/JSONValue'
import type { JSONObject } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/**
 * Review's reads (design 1n): the Mac app's `ReviewFinding`, `ReviewRun`,
 * `ReviewTarget`, `ReviewSweep` and `ReviewSkill` (`BusinessReads`,
 * `Review.swift`), each field as the CLI's 48b-48d documents carry it.
 */

const strings = (value: JSONValue | undefined): string[] => (JSONValue.array(value) ?? []).flatMap((item) => (typeof item === 'string' ? [item] : []))

/** One finding of a review: the finding contract's fields plus where its comment anchors. */
export interface ReviewFinding {
  readonly severity: string
  readonly kind: string
  readonly file: string
  /** The line its comment anchors to; `undefined` when it has none. */
  readonly line: number | undefined
  readonly location: string
  readonly claim: string
  readonly suggestedFix: string
  readonly confidence: string
  readonly identity: string
  /** Every lens that found it. */
  readonly foundBy: readonly string[]
  /** The confirm pass reworded it. */
  readonly tightened: boolean
  /** `false` past the cap: listed in the summary instead of inline. */
  readonly inline: boolean
}

export const ReviewFinding = {
  read(value: JSONValue): ReviewFinding | undefined {
    const row = JSONValue.object(value)
    const claim = JSONValue.string(row?.claim)
    if (!row || claim === undefined) return undefined
    return {
      severity: JSONValue.string(row.severity) ?? '',
      kind: JSONValue.string(row.kind) ?? '',
      file: JSONValue.string(row.file) ?? '',
      line: JSONValue.int(row.line),
      location: JSONValue.string(row.location) ?? '',
      claim,
      suggestedFix: JSONValue.string(row.suggested_fix) ?? '',
      confidence: JSONValue.string(row.confidence) ?? '',
      identity: JSONValue.string(row.identity) ?? '',
      foundBy: strings(row.found_by),
      tightened: JSONValue.bool(row.tightened) ?? false,
      inline: JSONValue.bool(row.inline) ?? true,
    }
  },
}

/** What happened to the batch: posted, or why not -- in the CLI's words. */
export type ReviewPosted = { readonly case: 'posted'; readonly comments: number; readonly summary: boolean; readonly batched: boolean } | { readonly case: 'notPosted'; readonly reason: string }

/** `review mr <ref> --output-format json` (the CLI's 48c): one review's document, whether or not it posted. */
export interface ReviewRun {
  /** The canonical spelling: `github.com/owner/repo#12`. */
  readonly mergeRequest: string
  readonly url: string
  readonly title: string
  readonly head: string
  /** `full`, `incremental` (with `since`) or `already reviewed`. */
  readonly mode: string
  readonly since: string | undefined
  readonly model: string
  readonly skills: readonly string[]
  /** `named`, `hints`, `hints, narrowed`. */
  readonly plan: string
  readonly leftOutSkills: readonly string[]
  readonly excludedFiles: readonly string[]
  readonly chunks: number
  readonly findings: readonly ReviewFinding[]
  /** What each step left out, by its name, in the CLI's order: `candidates`, `off_change` … */
  readonly counts: ReadonlyArray<readonly [string, number]>
  readonly posted: ReviewPosted
  readonly dryRun: boolean
  readonly notes: readonly string[]
}

const countOrder = ['candidates', 'off_change', 'not_new', 'known', 'duplicates', 'dismissed', 'tightened'] as const

export const ReviewRun = {
  /** A `review.run` object, or `undefined` for any other. */
  from(value: JSONObject): ReviewRun | undefined {
    if (JSONValue.string(value.object) !== 'review.run') return undefined
    const counts = JSONValue.object(value.counts)
    const posted = JSONValue.object(value.posted)
    return {
      mergeRequest: JSONValue.string(value.merge_request) ?? '',
      url: JSONValue.string(value.url) ?? '',
      title: JSONValue.string(value.title) ?? '',
      head: JSONValue.string(value.head) ?? '',
      mode: JSONValue.string(value.mode) ?? '',
      since: JSONValue.string(value.since),
      model: JSONValue.string(value.model) ?? '',
      skills: strings(value.skills),
      plan: JSONValue.string(value.plan) ?? '',
      leftOutSkills: (JSONValue.array(value.left_out_skills) ?? []).flatMap((skill) => {
        const name = JSONValue.string(skill) ?? JSONValue.string(JSONValue.object(skill)?.name)
        return name === undefined ? [] : [name]
      }),
      excludedFiles: strings(value.excluded_files),
      chunks: JSONValue.int(value.chunks) ?? 0,
      findings: (JSONValue.array(value.findings) ?? []).flatMap((finding) => ReviewFinding.read(finding) ?? []),
      counts: countOrder.flatMap((key): Array<readonly [string, number]> => {
        const count = JSONValue.int(counts?.[key])
        return count === undefined ? [] : [[key, count]]
      }),
      posted:
        JSONValue.bool(posted?.done) === true
          ? { case: 'posted', comments: JSONValue.int(posted?.comments) ?? 0, summary: JSONValue.bool(posted?.summary) ?? false, batched: JSONValue.bool(posted?.batched) ?? false }
          : { case: 'notPosted', reason: JSONValue.string(posted?.reason) ?? '' },
      dryRun: JSONValue.bool(value.dry_run) ?? false,
      notes: strings(value.notes),
    }
  },

  read(document: ReadDocument): ReviewRun | undefined {
    return ReviewRun.from(document.value)
  },

  /** A review that found nothing to say: the CLI's law -- silence is a complete review, never an empty-looking failure. */
  isSilent(run: ReviewRun): boolean {
    return run.findings.length === 0 && run.mode !== 'already reviewed'
  },

  /** The findings that would go inline. */
  inlineFindings(run: ReviewRun): readonly ReviewFinding[] {
    return run.findings.filter((finding) => finding.inline)
  },

  /** The findings past the cap. */
  overCap(run: ReviewRun): readonly ReviewFinding[] {
    return run.findings.filter((finding) => !finding.inline)
  },
}

/** A sweep target, `review targets --output-format json` (the CLI's 48d): the `review:` section's entry. */
export interface ReviewTarget {
  readonly name: string
  readonly forge: string
  /** `owner/repo` for a repository target. */
  readonly repo: string | undefined
  /** The owner whose repositories are swept. */
  readonly owner: string | undefined
  readonly host: string | undefined
  /** Empty: each merge request's plan decides. */
  readonly skills: readonly string[]
  /** The declared grant: a sweep posts here with nobody at the terminal. */
  readonly post: boolean
  readonly drafts: boolean
  readonly model: string | undefined
  readonly cli: string | undefined
}

export const ReviewTarget = {
  read(value: JSONValue): ReviewTarget | undefined {
    const row = JSONValue.object(value)
    const name = JSONValue.string(row?.name)
    if (!row || name === undefined) return undefined
    return {
      name,
      forge: JSONValue.string(row.forge) ?? '',
      repo: JSONValue.string(row.repo),
      owner: JSONValue.string(row.owner),
      host: JSONValue.string(row.host),
      skills: strings(row.skills),
      post: JSONValue.bool(row.post) ?? false,
      drafts: JSONValue.bool(row.drafts) ?? false,
      model: JSONValue.string(row.model),
      cli: JSONValue.string(row.cli),
    }
  },

  list(document: ReadDocument): ReviewTarget[] {
    return (document.entries ?? []).flatMap((entry) => ReviewTarget.read(entry) ?? [])
  },
}

/** One project a sweep target reached: its watermarks and each merge request reviewed. */
export interface ReviewSweepProject {
  /** `github.com/owner/repo`. */
  readonly project: string
  readonly watermarkBefore: string | undefined
  readonly watermarkAfter: string | undefined
  /** It started at now: history stays unreviewed. */
  readonly firstSweep: boolean
  readonly reviewed: readonly ReviewRun[]
  readonly skippedDrafts: number
  readonly error: string | undefined
}

/** One target a sweep reached. */
export interface ReviewSweepTarget {
  readonly name: string
  /** `repo` or `owner`. */
  readonly kind: string
  /** `target`, `--allow-post` or `none`. */
  readonly grant: string
  readonly projects: readonly ReviewSweepProject[]
  readonly error: string | undefined
}

/** `review sweep --output-format json` (the CLI's 48d): every target swept once, its watermarks, and each merge request's review. */
export interface ReviewSweep {
  readonly started: string
  readonly dryRun: boolean
  readonly allowPost: boolean
  readonly cap: number
  readonly targets: readonly ReviewSweepTarget[]
  readonly reviewed: number
  readonly posted: number
  readonly deferred: number
  readonly errors: number
  /** Where the CLI kept the document. */
  readonly document: string
}

export const ReviewSweep = {
  /** A `review.sweep` object, or `undefined` for any other. */
  from(value: JSONObject): ReviewSweep | undefined {
    if (JSONValue.string(value.object) !== 'review.sweep') return undefined
    return {
      started: JSONValue.string(value.started) ?? '',
      dryRun: JSONValue.bool(value.dry_run) ?? false,
      allowPost: JSONValue.bool(value.allow_post) ?? false,
      cap: JSONValue.int(value.cap) ?? 0,
      targets: (JSONValue.array(value.targets) ?? []).flatMap((entry): ReviewSweepTarget[] => {
        const target = JSONValue.object(entry)
        const name = JSONValue.string(target?.name)
        if (!target || name === undefined) return []
        return [
          {
            name,
            kind: JSONValue.string(target.kind) ?? '',
            grant: JSONValue.string(target.grant) ?? '',
            projects: (JSONValue.array(target.projects) ?? []).flatMap((item): ReviewSweepProject[] => {
              const project = JSONValue.object(item)
              const path = JSONValue.string(project?.project)
              if (!project || path === undefined) return []
              const drafts = project.skipped_drafts
              return [
                {
                  project: path,
                  watermarkBefore: JSONValue.string(project.watermark_before),
                  watermarkAfter: JSONValue.string(project.watermark_after),
                  firstSweep: JSONValue.bool(project.first_sweep) ?? false,
                  reviewed: (JSONValue.array(project.reviewed) ?? []).flatMap((run) => {
                    const object = JSONValue.object(run)
                    return (object && ReviewRun.from(object)) || []
                  }),
                  skippedDrafts: JSONValue.array(drafts)?.length ?? JSONValue.int(drafts) ?? 0,
                  error: JSONValue.string(project.error),
                },
              ]
            }),
            error: JSONValue.string(target.error),
          },
        ]
      }),
      reviewed: JSONValue.int(value.reviewed) ?? 0,
      posted: JSONValue.int(value.posted) ?? 0,
      deferred: JSONValue.int(value.deferred) ?? 0,
      errors: JSONValue.int(value.errors) ?? 0,
      document: JSONValue.string(value.document) ?? '',
    }
  },

  read(document: ReadDocument): ReviewSweep | undefined {
    return ReviewSweep.from(document.value)
  },
}

/** A review skill, `review skills list | show --output-format json` (the CLI's 48b): a markdown lens, where it comes from and what it applies to. */
export interface ReviewSkill {
  readonly name: string
  readonly description: string
  /** `shipped`, `seeded` or `user`. */
  readonly source: string
  readonly path: string | undefined
  readonly extensions: readonly string[]
  readonly paths: readonly string[]
  readonly languages: readonly string[]
  /** No hints: it applies to every file. */
  readonly everywhere: boolean
  /** The hunt prompt -- `show`'s alone. */
  readonly prompt: string | undefined
}

export const ReviewSkill = {
  read(value: JSONValue): ReviewSkill | undefined {
    const row = JSONValue.object(value)
    const name = JSONValue.string(row?.name)
    if (!row || name === undefined) return undefined
    const applies = JSONValue.object(row.applies)
    return {
      name,
      description: JSONValue.string(row.description) ?? '',
      source: JSONValue.string(row.source) ?? '',
      path: JSONValue.string(row.path),
      extensions: strings(applies?.extensions),
      paths: strings(applies?.paths),
      languages: strings(applies?.languages),
      everywhere: JSONValue.bool(row.everywhere) ?? false,
      prompt: JSONValue.string(row.prompt),
    }
  },

  list(document: ReadDocument): ReviewSkill[] {
    return (document.entries ?? []).flatMap((entry) => ReviewSkill.read(entry) ?? [])
  },

  /** `review skills show <name>`: the lens whole, or `undefined` when the document is not a `review.skill`. */
  show(document: ReadDocument): ReviewSkill | undefined {
    return document.object === 'review.skill' ? ReviewSkill.read(document.value) : undefined
  },
}
