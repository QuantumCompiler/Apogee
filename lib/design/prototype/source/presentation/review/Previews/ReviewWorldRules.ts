import type { Rule, Rules } from '../../../infrastructure/process/StandInChild'
import type { Folders } from '../../../infrastructure/process/StandInFolders'
import { reviewStartingTarget } from '../ViewModels/ReviewWords'

/**
 * The Review place's stand-in answers (design 1n), every one a recording of
 * the Mac app's or a `*-handmade*` file in the CLI's shape: `review targets`,
 * `review skills list` and each skill's `review skills show` (`security` and
 * `ai-ml` recorded, the other five hand-made in `review-skill-show.json`'s
 * shape with the old prototype's prompt text), the recorded dry run of
 * `acme/widget#12`, the add-target sheet's starting target added, and the
 * last sweep the CLI kept in its sweeps folder (`review-sweep.json`, at its
 * own `document` path).
 *
 * Each file is named by its file name, looked up in `ReviewWorlds.ts`'
 * `recordings`, where the `?raw` imports are: this module imports none, so
 * the fixture check (`tests/presentation/library/LibraryFixtures.test.ts`,
 * which holds Library's and Review's presets) lists what every preset names
 * without loading a fixture.
 */

/** Where a world's recordings come from: `ReviewWorlds.ts`' `recording`, or a test's lookup that lists the names. */
export type Lookup = (name: string) => string

/** The merge request the old Reviews tab holds, and the lens it names: the form `review-run-dry.json` answers. */
export const reviewForm = { ref: 'acme/widget#12', skills: ['security'] } as const

/** `review mr acme/widget#12 --skills security --dry-run --output-format json`: the dry run the CLI recorded. */
export const dryRunArguments = ['review', 'mr', reviewForm.ref, '--skills', 'security', '--dry-run', '--output-format', 'json'] as const

/** The add-target sheet as the old prototype opens it (`rv.addTarget`): `gadget`, watching the repository `acme/gadget`. */
export const startingTarget = reviewStartingTarget

/** `review targets add gadget --repo acme/gadget`: the sheet's starting target, as Add runs it. */
export const addTargetArguments = ['review', 'targets', 'add', startingTarget.name, '--repo', startingTarget.repo] as const

/** Where the CLI keeps each sweep's document: `review-sweep.json`'s own `document` folder. */
export const sweepsFolder = '/Users/someone/.apogee/review/sweeps'

/** The seven lenses `review-skills.json` lists, each with the file its `review skills show` answers. */
export const skillShows: Readonly<Record<string, string>> = {
  'ai-ml': 'review-skill-show-ai-ml.json',
  'back-end': 'review-skill-show-back-end-handmade.json',
  concurrency: 'review-skill-show-concurrency-handmade.json',
  embedded: 'review-skill-show-embedded-handmade.json',
  'front-end': 'review-skill-show-front-end-handmade.json',
  'general-correctness': 'review-skill-show-general-correctness-handmade.json',
  security: 'review-skill-show.json',
}

/** The Review presets, in the old `data-props` order: what the fixture check walks. Each reads the place's answers, every preset's. */
export const reviewPresets = ['review', 'review:post', 'review:targets', 'review:skills'] as const

/** A Review preset. */
export type ReviewPreset = (typeof reviewPresets)[number]

const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })

/** What Review reads and runs on every preset: the targets, the skills and each one's show, the recorded dry run, and the starting target added, each change for its exact arguments. */
export function reviewRules(look: Lookup): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  return {
    reads: [read(['review', 'targets'], look('review-targets.json')), read(['review', 'skills', 'list'], look('review-skills.json')), ...Object.entries(skillShows).map(([skill, file]) => read(['review', 'skills', 'show', skill], look(file)))],
    commands: [{ arguments: addTargetArguments, exact: true, answer: { stdout: look('review-targets-add-gadget-handmade.txt') } }],
    jobs: [{ arguments: dryRunArguments, exact: true, answer: { stdout: look('review-run-dry.json') } }],
  }
}

/** The sweep's document's file name, from its own `document` path. */
export function sweepFile(sweep: string): string {
  const document = (JSON.parse(sweep) as { document?: string }).document ?? ''
  return document.split('/').at(-1) ?? document
}

/** The CLI's sweeps folder, holding the last sweep's document it kept. */
export function reviewFiles(look: Lookup): Folders {
  const sweep = look('review-sweep.json')
  return { [sweepsFolder]: [{ name: sweepFile(sweep), text: sweep }] }
}

/**
 * The file names the place's answers and folders look up, each once
 * (`reviewRules`, `reviewFiles`): what the fixture check holds to exist on
 * every Review preset. The lookup answers `{}`, since `reviewFiles` reads the
 * sweep's own `document` path.
 */
export function reviewPlaceNames(): string[] {
  const named = new Set<string>()
  const look: Lookup = (name) => (named.add(name), '{}')
  reviewRules(look)
  reviewFiles(look)
  return [...named]
}
