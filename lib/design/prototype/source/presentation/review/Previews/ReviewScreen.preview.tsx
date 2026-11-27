import { ConfirmHost } from '../../shell/Views/Hosts'
import { useObserved } from '../../styles/Observation/useObserved'
import type { ReviewViewModel } from '../ViewModels/ReviewViewModel'
import { ReviewScreen } from '../Views/ReviewScreen'
import { useReviewPreview } from './ReviewPreview'
import type { ReviewPreviewOptions } from './ReviewPreview'

/** The post sheet over the place, as the app root frames it in the shell's confirm host. */
function Post({ model }: { readonly model: ReviewViewModel }) {
  useObserved(model)
  const words = model.postWords
  if (!words) return null
  return <ConfirmHost title={words.title} body={words.body} detail={words.command} cancel={{ label: 'Cancel', run: () => model.dismissPost() }} confirm={{ label: 'Post', run: () => model.confirmPost() }} />
}

function Screen({ options }: { readonly options: ReviewPreviewOptions }) {
  const model = useReviewPreview(options)
  return (
    <div className="ap" data-theme="dark" style={{ position: 'relative', display: 'flex', flexDirection: 'column', height: 900, background: 'var(--surface-window)', color: 'var(--text-primary)' }}>
      <ReviewScreen model={model} />
      <Post model={model} />
    </div>
  )
}

const reviewed: ReviewPreviewOptions = { script: [[(review) => review.review(), 2]] }
/** `review`: the recorded dry run of acme/widget#12 -- its finding, its counts, nothing posted, Post… offered. */
export const Reviewed = () => <Screen options={reviewed} />

const notYet: ReviewPreviewOptions = {}
/** Nothing reviewed yet: the form and the command the dry run is. */
export const NothingReviewed = () => <Screen options={notYet} />

const reviewing: ReviewPreviewOptions = { script: [[(review) => review.review(), 0]] }
/** A dry run under way: the form, no document yet. */
export const Reviewing = () => <Screen options={reviewing} />

const post: ReviewPreviewOptions = { script: [[(review) => review.review(), 2], [(review) => review.askToPost(), 0]] }
/** `review:post`: the post sheet asking, the batch said and the command that posts it. */
export const PostAsked = () => <Screen options={post} />

const runsMissing: ReviewPreviewOptions = { apogee: 'missing' }
/** No apogee: the Reviews tab says there is nothing to review with. */
export const RunsNoApogee = () => <Screen options={runsMissing} />

const targets: ReviewPreviewOptions = { then: (review) => review.pick('targets') }
/** `review:targets`: the two targets with their grants, and the last sweep the CLI kept. */
export const Targets = () => <Screen options={targets} />

const added: ReviewPreviewOptions = {
  then: async (review) => {
    review.pick('targets')
    review.newTarget()
    await review.addTarget()
  },
}
/** The sheet's starting target added: its row on the tab, as the CLI said it added it. */
export const TargetAdded = () => <Screen options={added} />

const targetsMissing: ReviewPreviewOptions = { apogee: 'missing', then: (review) => review.pick('targets') }
/** No apogee on Targets and sweeps: Add target… and Sweep… off, nothing listed. */
export const TargetsNoApogee = () => <Screen options={targetsMissing} />

const targetsTypo: ReviewPreviewOptions = { apogee: 'config-typo', then: (review) => review.pick('targets') }
/** A config typo: the CLI's refusal where the targets are, the kept sweep under it. */
export const TargetsConfigTypo = () => <Screen options={targetsTypo} />

const skills: ReviewPreviewOptions = { then: (review) => review.pick('skills') }
/** `review:skills`: the seven lenses, security shown whole, its prompt as recorded. */
export const Skills = () => <Screen options={skills} />

const aiMl: ReviewPreviewOptions = {
  then: async (review) => {
    review.pick('skills')
    await review.show('ai-ml')
  },
}
/** ai-ml, recorded: what it applies to by its extensions and paths. */
export const SkillByHints = () => <Screen options={aiMl} />

const handMade: ReviewPreviewOptions = {
  then: async (review) => {
    review.pick('skills')
    await review.show('concurrency')
  },
}
/** concurrency, hand-made in show's shape: by its languages, its prompt the old prototype's opening. */
export const SkillByLanguages = () => <Screen options={handMade} />

const skillsMissing: ReviewPreviewOptions = { apogee: 'missing', then: (review) => review.pick('skills') }
/** No apogee on Skills: nothing listed, the page's words for no lens. */
export const SkillsNoApogee = () => <Screen options={skillsMissing} />

const skillsTypo: ReviewPreviewOptions = { apogee: 'config-typo', then: (review) => review.pick('skills') }
/** A config typo on Skills: the lenses still listed and security shown, since the CLI reads no config for them (the Mac's `badConfig`). */
export const SkillsConfigTypo = () => <Screen options={skillsTypo} />
