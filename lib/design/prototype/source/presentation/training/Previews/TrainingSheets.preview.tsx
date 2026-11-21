import { useObserved } from '../../styles/Observation/useObserved'
import type { TrainingViewModel } from '../ViewModels/TrainingViewModel'
import { PromoteSheet, PullSheet } from '../Views/TrainingSheets'
import { promotedBackend, pulledDataset } from './TrainingWorlds'
import { useTrainingPreview } from './TrainingPreview'

/** The promote and pull sheets' content (design 1j), as the shell's sheet host holds it. */
const sheet = { width: 560, padding: 20, display: 'flex', flexDirection: 'column', gap: 12, background: 'var(--surface-card)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-small)' } as const

function Promote({ model }: { readonly model: TrainingViewModel }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={sheet}>
      <PromoteSheet model={model} />
    </div>
  )
}

function Pull({ model }: { readonly model: TrainingViewModel }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={sheet}>
      <PullSheet model={model} />
    </div>
  )
}

/** Merged, as it opens: the run's gate, As backend empty, the command with `<backend>`. */
export const PromoteMerged = () => <Promote model={useTrainingPreview({ then: (training) => training.newPromotion() })} />

/** Merged, named and forced: the command built. */
export const PromoteForced = () => <Promote model={useTrainingPreview({ then: (training) => (training.newPromotion(), training.setPromote({ backend: promotedBackend, force: true, quantize: 'Q4_K_M' })) })} />

/** The adapter alone onto q4: Onto, the ledger's words, the gate suite and the judge. */
export const PromoteAdapter = () => <Promote model={useTrainingPreview({ then: (training) => (training.newPromotion(), training.setPromote({ adapter: true, backend: 'q4' })) })} />

/** Reopened after a refusal: the CLI's line above, the mode it names. */
export const PromoteRefused = () => (
  <Promote
    model={useTrainingPreview({
      ticks: 2,
      then: (training) => {
        training.newPromotion()
        training.setPromote({ backend: 'based', force: true })
        training.promote()
      },
      after: (training) => training.changePromotion(),
    })}
  />
)

/** The pull sheet as it opens: Pull off. */
export const PullEmpty = () => <Pull model={useTrainingPreview({ then: (training) => training.newPull() })} />

/** A reference typed: the command it runs. */
export const PullTyped = () => <Pull model={useTrainingPreview({ then: (training) => (training.newPull(), training.setPullRef(pulledDataset.ref)) })} />
