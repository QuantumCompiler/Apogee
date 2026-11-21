import { useObserved } from '../../styles/Observation/useObserved'
import type { StoreViewModel } from '../ViewModels/StoreViewModel'
import { StoreSheet, StoreView } from '../Views/StoreView'
import { handles } from './ModelsWorlds'
import { useModelsPreview } from './ModelsPreview'

/** Stored weights on its own (design 1h): one preview per state a person sees differently. */
const frame = { position: 'relative', width: 1180, height: 760, display: 'flex', background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const
const sheet = { width: 600, padding: 20, display: 'flex', flexDirection: 'column', gap: 14, background: 'var(--surface-card)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-small)' } as const

function Store({ model }: { readonly model: StoreViewModel }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <StoreView model={model} />
    </div>
  )
}

function Sheet({ model }: { readonly model: StoreViewModel }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={sheet}>
      <StoreSheet model={model} />
    </div>
  )
}

/** The store world: two snapshots folded, Register the 2 not registered…. */
export const Folded = () => <Store model={useModelsPreview().store} />

/** Show them: the used snapshots listed, one's facts unread in the stand-in's words. */
export const ShowingAll = () => <Store model={useModelsPreview({ then: (models) => models.store.setShowingAll(true) }).store} />

/** `models:store`: the Llama snapshot before conversion, weights only, its two conversions offered. */
export const Snapshot = () => <Store model={useModelsPreview({ world: 'conversions', then: (models) => models.store.select(handles.llamaSnapshot) }).store} />

/** A registered GGUF: its backend, its lineage, made from the F16. */
export const Registered = () => <Store model={useModelsPreview({ then: (models) => models.store.select(handles.llamaQ4) }).store} />

/** An unquantized GGUF not registered: Quantize… and Register…. */
export const Unquantized = () => <Store model={useModelsPreview({ then: (models) => models.store.select(handles.llamaF16) }).store} />

/** A voice model: Open Voice, its voices by language. */
export const Voice = () => <Store model={useModelsPreview({ then: (models) => models.store.select('Kokoro-82M/voice/5f3a2c91d0e4') }).store} />

/** A set a job is working on: busy, its actions waiting. */
export const Busy = () => (
  <Store
    model={
      useModelsPreview({
        world: 'conversions',
        then: async (models) => {
          await models.store.select(handles.llamaSnapshot)
          models.store.busy = (handle) => (handle === handles.llamaSnapshot ? 'convert' : undefined)
        },
      }).store
    }
  />
)

/** `models:convert`: the convert sheet, GGUF f16 then Q4_K_M, registered. */
export const ConvertSheet = () => (
  <Sheet
    model={
      useModelsPreview({
        world: 'conversions',
        then: async (models) => {
          await models.store.select(handles.llamaSnapshot)
          await models.store.actAsync('convertToGGUF')
        },
      }).store
    }
  />
)

/** The convert sheet on MLX with a taken name: it refuses. */
export const ConvertRefused = () => (
  <Sheet
    model={
      useModelsPreview({
        world: 'conversions',
        then: async (models) => {
          await models.store.select(handles.llamaSnapshot)
          await models.store.actAsync('convertToMLX')
          models.store.editConvert((form) => (form.baseName = 'Gemma4-E4B'))
        },
      }).store
    }
  />
)

/** The quantize sheet on the F16. */
export const QuantizeSheet = () => (
  <Sheet
    model={
      useModelsPreview({
        then: async (models) => {
          await models.store.select(handles.llamaF16)
          await models.store.actAsync('quantize')
        },
      }).store
    }
  />
)

/** `models:register`: the register sheet on the Gemma MLX set, named as config scan names it. */
export const RegisterSheet = () => (
  <Sheet
    model={
      useModelsPreview({
        then: async (models) => {
          await models.store.select(handles.gemmaMlx)
          await models.store.actAsync('register')
        },
      }).store
    }
  />
)

/** The rename sheet on the Llama model. */
export const RenameSheet = () => (
  <Sheet
    model={
      useModelsPreview({
        then: async (models) => {
          await models.store.select(handles.llamaF16)
          models.store.askToRename('Llama3.2-1B-Instruct')
          models.store.editRename((form) => (form.name = 'Llama-1B'))
        },
      }).store
    }
  />
)
