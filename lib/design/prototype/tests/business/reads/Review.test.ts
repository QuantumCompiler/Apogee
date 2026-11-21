import reviewRunDry from '@recorded/business/reads/Fixtures/review-run-dry.json?raw'
import reviewRunPosted from '@recorded/business/reads/Fixtures/review-run-posted.json?raw'
import reviewRunSilent from '@recorded/business/reads/Fixtures/review-run-silent.json?raw'
import reviewSkillShow from '@recorded/business/reads/Fixtures/review-skill-show.json?raw'
import reviewSkills from '@recorded/business/reads/Fixtures/review-skills.json?raw'
import reviewSweep from '@recorded/business/reads/Fixtures/review-sweep.json?raw'
import reviewTargets from '@recorded/business/reads/Fixtures/review-targets.json?raw'
import { describe, expect, test } from 'vitest'
import { ReviewRun, ReviewSkill, ReviewSweep, ReviewTarget } from '../../../source/business/reads/Review'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

const documentOf = (text: string) => ReadDocument.from(text) as ReadDocument

describe("Review's reads (design 1n): the Mac app's ReviewRun, ReviewTarget, ReviewSweep and ReviewSkill (Review.swift)", () => {
  test('a dry run: the merge request, its head, mode, lenses and plan, the finding whole, the counts in the CLI’s order, and why nothing was posted', () => {
    const run = ReviewRun.read(documentOf(reviewRunDry)) as ReviewRun
    expect([run.mergeRequest, run.title, run.head, run.mode, run.model, run.plan, run.dryRun]).toEqual(['github.com/acme/widget#12', 'Thumbnails', 'cafe2222cafe2222', 'full', 'reviewer', 'named', true])
    expect(run.skills).toEqual(['security'])
    expect(run.findings).toEqual([
      { severity: 'high', kind: 'correctness', file: 'app/thumbnails.py', line: 8, location: '8-10', claim: 'The file name from the request reaches a shell command.', suggestedFix: 'Pass a list, no shell.', confidence: 'high', identity: 'f1a0544c42c5ee08', foundBy: ['security'], tightened: false, inline: true },
    ])
    expect(run.counts).toEqual([
      ['candidates', 1],
      ['off_change', 0],
      ['not_new', 0],
      ['known', 0],
      ['duplicates', 0],
      ['dismissed', 0],
      ['tightened', 0],
    ])
    expect(run.posted).toEqual({ case: 'notPosted', reason: 'dry run: nothing posted' })
    expect([ReviewRun.isSilent(run), ReviewRun.inlineFindings(run).length, ReviewRun.overCap(run).length]).toEqual([false, 1, 0])
  })

  test('a posted run says what it posted; a silent run is a complete review; a document of another object is none', () => {
    expect((ReviewRun.read(documentOf(reviewRunPosted)) as ReviewRun).posted).toEqual({ case: 'posted', comments: 1, summary: true, batched: true })
    expect(ReviewRun.isSilent(ReviewRun.read(documentOf(reviewRunSilent)) as ReviewRun)).toBe(true)
    expect(ReviewRun.read(documentOf(reviewTargets))).toBeUndefined()
  })

  test('targets: a repository and an owner, their lenses (none: each change’s plan decides) and their grant', () => {
    const targets = ReviewTarget.list(documentOf(reviewTargets))
    expect(targets.map((target) => [target.name, target.forge, target.repo, target.owner, target.skills, target.post])).toEqual([
      ['acme-all', 'github', undefined, 'acme', [], true],
      ['widget', 'github', 'acme/widget', undefined, ['security'], false],
    ])
  })

  test('a sweep: its counts, each target and project, the watermarks, each merge request reviewed, and where the CLI kept it', () => {
    const sweep = ReviewSweep.read(documentOf(reviewSweep)) as ReviewSweep
    expect([sweep.dryRun, sweep.reviewed, sweep.posted, sweep.deferred, sweep.errors, sweep.started, sweep.document]).toEqual([true, 1, 0, 0, 0, '2026-10-21T06:09:25Z', '/Users/someone/.apogee/review/sweeps/20261021T060925Z.json'])
    const [widget] = sweep.targets
    expect([widget?.name, widget?.kind, widget?.grant]).toEqual(['widget', 'repo', 'none'])
    const [project] = widget?.projects ?? []
    expect([project?.project, project?.watermarkBefore, project?.watermarkAfter, project?.firstSweep, project?.skippedDrafts]).toEqual(['github.com/acme/widget', undefined, '2026-10-18T09:00:00Z', false, 0])
    expect(project?.reviewed.map((run) => run.title)).toEqual(['Thumbnails'])
  })

  test('skills: every lens with what it applies to, everywhere or by its hints; show adds its prompt, and only a review.skill document is one', () => {
    const skills = ReviewSkill.list(documentOf(reviewSkills))
    expect(skills.map((skill) => skill.name)).toEqual(['ai-ml', 'back-end', 'concurrency', 'embedded', 'front-end', 'general-correctness', 'security'])
    expect(skills[2]?.languages).toEqual(['c', 'cpp', 'csharp', 'go', 'java', 'kotlin', 'rust', 'scala', 'swift'])
    expect([skills[6]?.everywhere, skills[6]?.prompt]).toEqual([true, undefined])
    const security = ReviewSkill.show(documentOf(reviewSkillShow)) as ReviewSkill
    expect(security.prompt?.startsWith('You are reviewing one change through one lens: security.')).toBe(true)
    expect(ReviewSkill.show(documentOf(reviewSkills))).toBeUndefined()
  })
})
