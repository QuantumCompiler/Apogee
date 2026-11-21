import reviewRunDry from '@recorded/business/reads/Fixtures/review-run-dry.json?raw'
import reviewSkillShowAiMl from '@recorded/business/reads/Fixtures/review-skill-show-ai-ml.json?raw'
import reviewSkillShow from '@recorded/business/reads/Fixtures/review-skill-show.json?raw'
import reviewSkills from '@recorded/business/reads/Fixtures/review-skills.json?raw'
import reviewSweep from '@recorded/business/reads/Fixtures/review-sweep.json?raw'
import reviewTargets from '@recorded/business/reads/Fixtures/review-targets.json?raw'
import skillBackEnd from '../../../../fixtures/business/reads/review-skill-show-back-end-handmade.json?raw'
import skillConcurrency from '../../../../fixtures/business/reads/review-skill-show-concurrency-handmade.json?raw'
import skillEmbedded from '../../../../fixtures/business/reads/review-skill-show-embedded-handmade.json?raw'
import skillFrontEnd from '../../../../fixtures/business/reads/review-skill-show-front-end-handmade.json?raw'
import skillGeneralCorrectness from '../../../../fixtures/business/reads/review-skill-show-general-correctness-handmade.json?raw'
import targetsAddGadget from '../../../../fixtures/business/reads/review-targets-add-gadget-handmade.txt?raw'
import type { Rules } from '../../../infrastructure/process/StandInChild'
import type { Folders } from '../../../infrastructure/process/StandInFolders'
import { reviewFiles, reviewRules } from './ReviewWorldRules'
import type { Lookup } from './ReviewWorldRules'

export * from './ReviewWorldRules'

/**
 * The Review place's recordings (design 1n), each a `?raw` import the build
 * inlines, named in `recordings` by its file name; the answers built from
 * them are `ReviewWorldRules.ts`, which imports none. Six are hand-made in
 * the CLI's shape under the design's `fixtures/`:
 * - `review-skill-show-<lens>-handmade.json` (five): `review-skill-show.json`'s
 *   keys, `review-skills.json`'s row for the lens, `object: review.skill`, and
 *   the prompt text the old prototype's `reviewSkills()` gives (its opening
 *   paragraph, as the old shows every lens's), until a capture replaces it;
 * - `review-targets-add-gadget-handmade.txt`: `add_target`'s two lines
 *   (`review_cmd.cpp:456-459`) for the sheet's starting target.
 */
const recordings: Readonly<Record<string, string>> = {
  'review-run-dry.json': reviewRunDry,
  'review-skill-show.json': reviewSkillShow,
  'review-skill-show-ai-ml.json': reviewSkillShowAiMl,
  'review-skills.json': reviewSkills,
  'review-sweep.json': reviewSweep,
  'review-targets.json': reviewTargets,
  'review-skill-show-back-end-handmade.json': skillBackEnd,
  'review-skill-show-concurrency-handmade.json': skillConcurrency,
  'review-skill-show-embedded-handmade.json': skillEmbedded,
  'review-skill-show-front-end-handmade.json': skillFrontEnd,
  'review-skill-show-general-correctness-handmade.json': skillGeneralCorrectness,
  'review-targets-add-gadget-handmade.txt': targetsAddGadget,
}

/** A file's text, by its name; a name no import holds is refused, naming it. */
export function recording(name: string): string {
  const text = recordings[name]
  if (text === undefined) throw new Error(`no fixture named ${name}`)
  return text
}

/** What Review reads and runs on every preset (`Presets.ts`' `modelling`). */
export function reviewPlaceRules(look: Lookup = recording): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  return reviewRules(look)
}

/** `folders` with the CLI's sweeps folder beside them, unless they name it already: every preset's. */
export function withReviewFolders(folders: Folders = {}, look: Lookup = recording): Folders {
  return Object.entries(reviewFiles(look)).reduce<Folders>((all, [folder, files]) => (all[folder] !== undefined ? all : { ...all, [folder]: files }), folders)
}
