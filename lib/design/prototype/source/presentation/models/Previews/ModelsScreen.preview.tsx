import { useObserved } from '../../styles/Observation/useObserved'
import { ConvertForm } from '../ViewModels/StoreForms'
import type { ModelsViewModel } from '../ViewModels/ModelsViewModel'
import { BackendsTab, ModelsScreen, PairDrafterSheet, RolesTab } from '../Views/ModelsScreen'
import { handles } from './ModelsWorlds'
import { useModelsPreview } from './ModelsPreview'

/** The Models place on its own (design 1h), one preview per state a person sees differently, over the recorded worlds. */
const frame = { position: 'relative', width: 1180, height: 760, display: 'flex', flexDirection: 'column', background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const
const sheet = { width: 560, padding: 20, display: 'flex', flexDirection: 'column', gap: 12, background: 'var(--surface-card)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-small)' } as const

function Screen({ model }: { readonly model: ModelsViewModel }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <ModelsScreen model={model} />
    </div>
  )
}

/** `models`: Backends, llama3.2-1b-Q4KM's details open -- its facts, lineage, files and no adapters. */
export const Backends = () => <Screen model={useModelsPreview({ then: (models) => models.select('llama3.2-1b-Q4KM') })} />

/** Backends with nothing selected: the two sections alone. */
export const BackendsAlone = () => <Screen model={useModelsPreview()} />

/** A vision MLX backend with no drafter: its DRAFTER section offers Pair a drafter…. */
export const VisionUnpaired = () => <Screen model={useModelsPreview({ then: (models) => models.select('Gemma4-E4B-4bit') })} />

/** An API backend with no key: Add an API key…. */
export const ApiNoKey = () => <Screen model={useModelsPreview({ then: (models) => models.select('claude') })} />

/** A mock backend, a kind no glyph marks: its place kept, its actions the roles' and the removal. */
export const MockBackend = () => <Screen model={useModelsPreview({ then: (models) => models.select('fastroot') })} />

/** What a delete printed, kept on Backends until dismissed. */
export const Deleted = () => (
  <Screen
    model={useModelsPreview({
      then: async (models) => {
        await models.select('llama3.2-1b-Q4KM')
        await models.act('deleteWithWeights')
        await models.confirm('model', true)
      },
    })}
  />
)

/** With no binary: the prototype's empty table (its missing-CLI state is a follow-up). */
export const Missing = () => <Screen model={useModelsPreview({ apogee: 'missing' })} />

/** Roles: the six, under suite fast. */
export const Roles = () => <Screen model={useModelsPreview({ then: (models) => models.pick('roles') })} />

/** `models:downloads`: Jobs, the Llama conversion mid-run. */
export const JobsRunning = () => (
  <Screen
    model={useModelsPreview({
      world: 'conversions',
      ticks: 10,
      then: async (models) => {
        models.pick('jobs')
        await models.downloads.convert(ConvertForm.gguf(handles.llamaSnapshot, 2482980600))?.ready
      },
    })}
  />
)

/** Jobs before anything ran. */
export const JobsEmpty = () => <Screen model={useModelsPreview({ then: (models) => models.pick('jobs') })} />

/** The Backends tab on its own, as the screen draws it. */
export const BackendsTabAlone = () => {
  const model = useModelsPreview()
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <BackendsTab model={model} />
    </div>
  )
}

/** The Roles tab on its own. */
export const RolesTabAlone = () => {
  const model = useModelsPreview()
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <RolesTab model={model} />
    </div>
  )
}

function Pair({ model }: { readonly model: ModelsViewModel }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={sheet}>
      <PairDrafterSheet model={model} />
    </div>
  )
}

/** The pair sheet with the drafter the binary offers Gemma4-E4B-4bit. */
export const PairOffered = () => (
  <Pair
    model={useModelsPreview({
      then: async (models) => {
        await models.select('Gemma4-E4B-4bit')
        await models.askToPairDrafter()
      },
    })}
  />
)

/** The pair sheet where the binary offers none: its words, Pair waiting. */
export const PairNone = () => (
  <Pair
    model={useModelsPreview({
      then: async (models) => {
        await models.select('Gemma4-12B-4bit')
        await models.askToPairDrafter()
      },
    })}
  />
)
