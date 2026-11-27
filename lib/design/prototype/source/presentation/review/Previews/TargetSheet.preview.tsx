import { SheetHost } from '../../shell/Views/Hosts'
import { useObserved } from '../../styles/Observation/useObserved'
import type { ReviewViewModel } from '../ViewModels/ReviewViewModel'
import { TargetSheet } from '../Views/TargetSheet'
import { useReviewPreview } from './ReviewPreview'
import type { ReviewPreviewOptions } from './ReviewPreview'

/** The sheet in the shell's sheet host, framed as the app root frames it. */
function Frame({ model }: { readonly model: ReviewViewModel }) {
  useObserved(model)
  const sheet = model.targetSheet()
  if (!sheet.shown) return null
  return (
    <SheetHost
      title="Add a sweep target"
      width={520}
      right={[
        { label: 'Cancel', run: () => model.cancelTarget() },
        { label: 'Add', kind: 'btn-p', run: () => void model.addTarget(), disabled: !sheet.canAdd },
      ]}
    >
      <TargetSheet model={model} />
    </SheetHost>
  )
}

function Sheet({ options }: { readonly options: ReviewPreviewOptions }) {
  const model = useReviewPreview(options)
  return (
    <div className="ap" data-theme="dark" style={{ position: 'relative', height: 900, background: 'var(--surface-window)', color: 'var(--text-primary)' }}>
      <Frame model={model} />
    </div>
  )
}

const starting: ReviewPreviewOptions = {
  then: (review) => {
    review.pick('targets')
    review.newTarget()
  },
}
/** As the old sheet opens: gadget, watching acme/gadget, its command live under it. */
export const Starting = () => <Sheet options={starting} />

const owner: ReviewPreviewOptions = {
  then: (review) => {
    review.pick('targets')
    review.newTarget()
    review.editTarget({ watch: 'owner', owner: 'acme', post: true })
    review.toggleLensMenu()
    review.toggleLens('security')
    review.toggleLens('ai-ml')
  },
}
/** An owner's repositories, two lenses pinned in the menu -- sorted in the command -- and the grant to post. */
export const OwnerWithLenses = () => <Sheet options={owner} />

const unnamed: ReviewPreviewOptions = {
  then: (review) => {
    review.pick('targets')
    review.newTarget()
    review.editTarget({ name: '' })
  },
}
/** No name: the command says what is missing, and Add is off. */
export const Unnamed = () => <Sheet options={unnamed} />
