import { useObserved } from '../../styles/Observation/useObserved'
import type { ModelsViewModel } from '../ViewModels/ModelsViewModel'
import { AdaptersSheet } from '../Views/AdaptersSheet'
import { useModelsPreview } from './ModelsPreview'

/** The adapters sheet on its own (design 1h). */
const sheet = { width: 560, padding: 20, display: 'flex', flexDirection: 'column', gap: 12, background: 'var(--surface-card)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-small)' } as const

function Sheet({ model }: { readonly model: ModelsViewModel }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={sheet}>
      <AdaptersSheet model={model} />
    </div>
  )
}

/** None applied: taking them all off is the command. */
export const NoneApplied = () => <Sheet model={useModelsPreview({ then: async (models) => (await models.select('llama3.2-1b-Q4KM'), models.editAdapters()) })} />

/** A file added, at scale 1. */
export const OneAdded = () => <Sheet model={useModelsPreview({ then: async (models) => (await models.select('llama3.2-1b-Q4KM'), models.editAdapters(), models.editAdaptersForm((form) => form.add())) })} />

/** An mlx entry: one directory, no scale, Add waiting once it has one. */
export const MlxDirectory = () => <Sheet model={useModelsPreview({ then: async (models) => (await models.select('Qwen3-8B-4bit'), models.editAdapters(), models.editAdaptersForm((form) => form.add())) })} />
