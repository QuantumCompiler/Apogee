import reviewRunSilent from '@recorded/business/reads/Fixtures/review-run-silent.json?raw'
import reviewTargetsBadConfig from '@recorded/business/reads/Fixtures/review-targets-bad-config.err?raw'
import { describe, expect, test } from 'vitest'
import { ChildRegistry } from '../../../source/business/jobs/ChildRegistry'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import type { CommandRunning } from '../../../source/business/reads/CommandRunning'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode } from '../../../source/infrastructure/process/StandInChild'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { addTargetArguments, dryRunArguments, reviewForm, reviewPlaceRules, sweepsFolder, withReviewFolders } from '../../../source/presentation/review/Previews/ReviewWorlds'
import { TargetForm } from '../../../source/presentation/review/ViewModels/ReviewForms'
import { ReviewViewModel } from '../../../source/presentation/review/ViewModels/ReviewViewModel'

async function settle(): Promise<void> {
  for (let round = 0; round < 6; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/** The Review place over the stand-in (design 1n), its form the old tab's, a still clock tests advance by hand, and its doors recorded. */
async function reviewPlace(apogee: ApogeeMode = 'installed') {
  const child = new StandInChild({ rules: reviewPlaceRules(), apogee, folders: withReviewFolders() }, modeRules)
  const clock = new Clock({ still: true })
  const review = new ReviewViewModel(new ReadRunner(child), new JobStarter(child, clock, new ChildRegistry()), child.folders)
  const said: string[] = []
  const sheets: string[] = []
  const asked: string[] = []
  review.say = (text) => said.push(text)
  review.present = (kind) => sheets.push(kind)
  review.dismissSheet = () => sheets.push('dismissed')
  review.ask = (kind) => asked.push(kind)
  review.sweeps = () => [sweepsFolder]
  review.setForm(reviewForm)
  review.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
  await review.appeared()
  return { review, child, clock, said, sheets, asked }
}

describe('Review (design 1n): the Mac app’s ReviewViewModel and its tests, drawn as the old place', () => {
  test('the tabs switch: Reviews, Targets and sweeps, Skills; Add target… and Sweep… on the targets tab alone', async () => {
    const { review } = await reviewPlace()
    expect(review.face().tabs.map((tab) => [tab.label, tab.current])).toEqual([
      ['Reviews', true],
      ['Targets and sweeps', false],
      ['Skills', false],
    ])
    expect(review.face().targetButtons.shown).toBe(false)
    review.pick('targets')
    expect([review.face().tabs[1]?.current, review.face().targetButtons]).toEqual([true, { shown: true, enabled: true }])
    review.pick('skills')
    expect(review.face().tabs[2]?.current).toBe(true)
  })

  test('a review is a dry run first: the argv as typed, supervised, its finding, counts and why nothing was posted read from its document', async () => {
    const { review, child, clock } = await reviewPlace()
    expect(review.face().runs.form).toEqual({ ref: 'acme/widget#12', lenses: 'Lenses: security', model: 'Model: the chat role', command: 'apogee review mr acme/widget#12 --skills security --dry-run --output-format json' })
    expect(review.face().runs.run.shown).toBe(false)
    review.review()
    expect(child.calls.filter((call) => call.kind === 'jobs').map((call) => call.arguments)).toEqual([[...dryRunArguments]])
    clock.advance(2)
    await settle()
    expect(review.face().runs.run).toEqual({
      shown: true,
      title: 'Thumbnails',
      where: 'github.com/acme/widget#12 · head cafe2222 · full',
      lenses: 'lenses: security (named) · reviewer',
      posted: 'dry run: nothing posted',
      canPost: true,
      findings: [{ severity: 'high', severityColor: 'var(--status-error)', kind: 'correctness', at: 'app/thumbnails.py:8', by: 'high confidence · security', claim: 'The file name from the request reaches a shell command.', fix: 'Pass a list, no shell.' }],
      counts: 'candidates 1 · off change 0 · not new 0 · known 0 · duplicates 0 · dismissed 0 · tightened 0',
    })
  })

  test('silence is a complete review: shown as one, and nothing to post is offered (the Mac’s ReviewViewModelTests, on review-run-silent.json)', async () => {
    const rules = reviewPlaceRules()
    const child = new StandInChild({ rules: { ...rules, jobs: [{ arguments: dryRunArguments, exact: true, answer: { stdout: reviewRunSilent } }] }, apogee: 'installed', folders: withReviewFolders() }, modeRules)
    const clock = new Clock({ still: true })
    const review = new ReviewViewModel(new ReadRunner(child), new JobStarter(child, clock, new ChildRegistry()), child.folders)
    const asked: string[] = []
    review.ask = (kind) => asked.push(kind)
    review.setForm(reviewForm)
    review.connect(StandInChild.binary)
    review.review()
    clock.advance(2)
    await settle()
    expect([review.face().runs.run.posted, review.face().runs.run.canPost, review.face().runs.run.findings]).toEqual(['Nothing to say: the lenses found nothing in this change. That is a complete review.', false, []])
    review.askToPost()
    expect([asked, review.postWords]).toEqual([[], undefined])
  })

  test('posting asks first, saying the batch and the command that posts it; Post closes the sheet and runs nothing, as the old one does', async () => {
    const { review, child, clock, asked } = await reviewPlace()
    review.askToPost()
    expect(asked, 'nothing to post before a run').toEqual([])
    review.review()
    clock.advance(2)
    await settle()
    review.askToPost()
    expect(asked).toEqual(['reviewPost'])
    expect(review.postWords).toEqual({
      title: 'Post the review to github.com/acme/widget#12?',
      body: 'The dry run found 1 finding to post inline. Posting runs the review again with --yes and posts what that run finds, as one batch on github.com/acme/widget#12: its inline comments and a summary.',
      command: 'apogee review mr acme/widget#12 --skills security --yes --output-format json',
    })
    const calls = child.calls.length
    review.confirmPost()
    expect([review.postWords, child.calls.length]).toEqual([undefined, calls])
    review.askToPost()
    review.dismissPost()
    expect(review.postWords).toBeUndefined()
  })

  test('the dead buttons stay dead, as the prototype draws them: Review, Sweep… and a target’s Remove… run and say nothing', async () => {
    const { review, child, said, sheets, asked } = await reviewPlace()
    const calls = child.calls.length
    review.pressReview()
    review.pressSweep()
    review.pressRemove('widget')
    expect([child.calls.length, said, sheets, asked]).toEqual([calls, [], [], []])
  })

  test('the target sheet builds `apogee review targets add …` live from its fields, the lenses sorted; Add stays off until a name and a repository or owner', () => {
    const form = TargetForm.starting()
    expect(TargetForm.command(form)).toBe('apogee review targets add gadget --repo acme/gadget')
    const planted = { ...form, lenses: ['security', 'ai-ml', 'concurrency'], post: true, drafts: true, forge: 'gitlab', host: 'git.acme.dev', model: 'reviewer' }
    expect(TargetForm.command(planted)).toBe('apogee review targets add gadget --repo acme/gadget --forge gitlab --host git.acme.dev --skills ai-ml,concurrency,security --post --drafts -m reviewer')
    expect(TargetForm.arguments(planted)).toEqual(['review', 'targets', 'add', 'gadget', '--repo', 'acme/gadget', '--forge', 'gitlab', '--host', 'git.acme.dev', '--skills', 'ai-ml,concurrency,security', '--post', '--drafts', '-m', 'reviewer'])
    expect(TargetForm.lensesLabel(planted)).toBe('Lenses: ai-ml, concurrency, security')
    expect(TargetForm.lensesLabel(form)).toBe("Lenses: as each change's files say")
    const owner = { ...form, watch: 'owner' as const, owner: '' }
    expect([TargetForm.command(owner), TargetForm.canAdd(owner), TargetForm.arguments(owner)]).toEqual(['apogee review targets add gadget --owner <owner>', false, undefined])
    expect(TargetForm.command({ ...owner, owner: 'acme' })).toBe('apogee review targets add gadget --owner acme')
    const unnamed = { ...form, name: '  ' }
    expect([TargetForm.command(unnamed), TargetForm.canAdd(unnamed)]).toEqual(['apogee review targets add <name> --repo acme/gadget', false])
    expect(TargetForm.canAdd({ ...form, repo: '' })).toBe(false)
    expect(TargetForm.canAdd(form)).toBe(true)
  })

  test('Add runs the command as typed and puts the target on the tab: the stand-in keeps no state, so what the CLI said it added is kept beside the read', async () => {
    const { review, child, said, sheets } = await reviewPlace()
    review.pick('targets')
    review.newTarget()
    expect(sheets).toEqual(['target'])
    expect(review.targetSheet().command).toBe('apogee review targets add gadget --repo acme/gadget')
    await review.addTarget()
    expect(child.calls.filter((call) => call.kind === 'commands').map((call) => call.arguments)).toEqual([[...addTargetArguments]])
    expect(sheets).toEqual(['target', 'dismissed'])
    expect(said).toEqual(["added review target 'gadget' to /Users/someone/.apogee/config/config.json"])
    expect(review.face().targets.rows.map((row) => [row.name, row.facts, row.post, row.postColor])).toEqual([
      ['acme-all', 'github · owner acme · lenses by the change', 'posts when swept', 'var(--status-warning)'],
      ['widget', 'github · repository acme/widget · lenses security', 'documents only', 'var(--text-secondary)'],
      ['gadget', 'github · repository acme/gadget · lenses by the change', 'documents only', 'var(--text-secondary)'],
    ])
    review.newTarget()
    review.editTarget({ name: 'docs' })
    review.toggleLens('security')
    await review.addTarget()
    expect(said.at(-1)).toBe('no recorded answer for apogee review targets add docs --repo acme/gadget --skills security')
    expect(review.face().targets.rows.length, 'a target the CLI did not add is not drawn').toBe(3)
  })

  test('the last sweep is the document the CLI kept in its sweeps folder, in the old page’s lines', async () => {
    const { review } = await reviewPlace()
    expect(review.face().targets.sweep).toEqual({
      shown: true,
      summary: 'dry run · 1 reviewed · 0 posted · 0 deferred · 0 errors · started 2026-10-21T06:09:25Z',
      targets: [{ heading: 'widget · repo · grant none', error: '', projects: [{ project: 'github.com/acme/widget', watermark: 'watermark none → 2026-10-18T09:00:00Z', error: '', lines: ['github.com/acme/widget#12 -- Thumbnails: 1 finding; dry run: nothing posted'] }] }],
      kept: 'kept at ~/.apogee/review/sweeps/20261021T060925Z.json',
    })
  })

  test('skills: the seven lenses listed with what they apply to; security shown by default, another on a click -- recorded or hand-made -- and a reply lands only while its skill is chosen', async () => {
    const { review, child } = await reviewPlace()
    review.pick('skills')
    const face = review.face().skills
    expect(face.rows.map((row) => [row.name, row.meta, row.current])).toEqual([
      ['ai-ml', 'shipped · .ipynb, **/train/**, **/training/**, **/ml/**, **/models/**, **/notebooks/**, **/eval/**, **/evals/**, **/inference/**, **/prompts/**', false],
      ['back-end', 'shipped · .go, .py, .rb, .java, .kt, .cs, .php, .rs, .ex, .exs, .scala, .sql, **/api/**, **/server/**, **/backend/**, **/handlers/**, **/routes/**, **/controllers/**, **/services/**, **/migrations/**', false],
      ['concurrency', 'shipped · c, cpp, csharp, go, java, kotlin, rust, scala, swift', false],
      ['embedded', 'shipped · .ino, .ld, **/firmware/**, **/embedded/**, **/drivers/**, **/hal/**, **/bsp/**, **/isr/**, **/rtos/**', false],
      ['front-end', 'shipped · .js, .jsx, .ts, .tsx, .mjs, .vue, .svelte, .html, .htm, .css, .scss, **/components/**, **/frontend/**, **/web/**, **/ui/**, **/pages/**', false],
      ['general-correctness', 'shipped · every file', false],
      ['security', 'shipped · every file', true],
    ])
    expect([face.skill.name, face.skill.applies, face.skill.command, face.skill.prompt.startsWith('You are reviewing one change through one lens: security.')]).toEqual(['security', 'Applies to every file.', 'apogee review skills show security', true])
    await review.show('concurrency')
    const concurrency = review.face().skills.skill
    expect([concurrency.name, concurrency.applies, concurrency.prompt]).toEqual([
      'concurrency',
      'Applies to: c, cpp, csharp, go, java, kotlin, rust, scala, swift',
      'You are reviewing one change through one lens: concurrency -- threads, tasks, coroutines, callbacks and shared state. You get the change as a unified diff, its new-side lines numbered. Lines starting with `+` are what the change adds, lines starting with `-` are what it removes, and the rest is unchanged context. Review only what the change adds or alters. If the change touches nothing concurrent, the answer is empty.',
    ])
    expect(child.unanswered).toEqual([])
  })

  test('a slow skill’s reply that lands after the next click is dropped: the page shows the skill chosen, never the last to answer', async () => {
    const child = new StandInChild({ rules: reviewPlaceRules(), apogee: 'installed', folders: withReviewFolders() }, modeRules)
    const reads = new ReadRunner(child)
    let release: () => void = () => {}
    const held = new Promise<void>((resolve) => (release = resolve))
    // ai-ml's show answers only once released, after embedded's has landed.
    const slow: CommandRunning = {
      read: async (binary, args) => {
        const outcome = await reads.read(binary, args)
        if (args.join(' ') === 'review skills show ai-ml') await held
        return outcome
      },
      command: (binary, args, input) => reads.command(binary, args, input),
      report: (binary, args) => reads.report(binary, args),
      readKeepingStderr: (binary, args) => reads.readKeepingStderr(binary, args),
    }
    const review = new ReviewViewModel(slow, new JobStarter(child, new Clock({ still: true }), new ChildRegistry()), child.folders)
    review.connect(StandInChild.binary)
    await review.appeared()
    const aiMl = review.show('ai-ml')
    await review.show('embedded')
    release()
    await aiMl
    expect([review.selectedSkill, review.face().skills.skill.name]).toEqual(['embedded', 'embedded'])
  })

  test('no apogee: every tab says so, and nothing is listed or run; a config typo: the targets’ recorded refusal, word for word, while the skills, which read no config, still list', async () => {
    const missing = await reviewPlace('missing')
    const face = missing.review.face()
    expect([face.runs.missing, face.targets.note, face.targets.rows, face.targets.sweep.shown, face.targetButtons.enabled, face.skills.note, face.skills.rows, face.skills.empty]).toEqual([
      "Apogee isn't installed, so there is nothing to review with.",
      "Apogee isn't installed, so there is nothing to review with.",
      [],
      false,
      false,
      "Apogee isn't installed, so there is nothing to review with.",
      [],
      'Every lens a review can run, shipped and your own. Choose one to read its prompt.',
    ])
    missing.review.review()
    expect(missing.child.calls).toEqual([])
    const typo = await reviewPlace('config-typo')
    const refusal = `${reviewTargetsBadConfig.trim()} (exited with status 1)`
    expect(typo.review.face().targets.note).toBe(`The targets could not be read: ${refusal}`)
    const skills = typo.review.face().skills
    expect([skills.note, skills.rows.length, skills.skill.shown, skills.skill.name], 'the CLI lists the skills over a bad config, as the Mac’s badConfig and the old prototype do').toEqual(['', 7, true, 'security'])
    typo.review.pick('targets')
    typo.review.newTarget()
    expect(typo.review.targetSheet().lenses.map((lens) => lens.name), 'the sheet offers the seven lenses').toEqual(['ai-ml', 'back-end', 'concurrency', 'embedded', 'front-end', 'general-correctness', 'security'])
    expect([typo.review.face().targets.sweep.shown, typo.review.face().runs.missing], 'the kept sweep is the person’s file, read whatever the config').toEqual([true, ''])
  })
})
