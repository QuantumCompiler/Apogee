import type { ReactNode } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import type { TrainingViewModel } from '../ViewModels/TrainingViewModel'
import { CycleView } from '../Views/CycleView'
import { DatasetsView, PulledBanner } from '../Views/DatasetsView'
import { PipelinesView, StageList } from '../Views/PipelinesView'
import { RegimesView } from '../Views/RegimesView'
import { RunsView } from '../Views/RunsView'
import { TrainingScreen } from '../Views/TrainingScreen'
import { pulledDataset, sweep } from './TrainingWorlds'
import { useTrainingPreview } from './TrainingPreview'

/** The Training place on its own (design 1j), one preview per state a person sees differently, over the recorded worlds. */
const frame = { position: 'relative', width: 1180, height: 760, display: 'flex', flexDirection: 'column', background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Frame({ model, children }: { readonly model: TrainingViewModel; readonly children: ReactNode }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={frame}>
      {children}
    </div>
  )
}

const pull = async (training: TrainingViewModel) => {
  training.newPull()
  training.setPullRef(pulledDataset.ref)
  training.pull()
}

// The screen: its header, its bar and each tab.

/** `training`: Runs, the failed run's details, a run training -- the bar at `train-run.err`'s fourth line. */
export const Training = () => {
  const model = useTrainingPreview({ ticks: 4, then: (training) => training.newRun() })
  return (
    <Frame model={model}>
      <TrainingScreen model={model} />
    </Frame>
  )
}

/** Runs with nothing running: New run… offered. */
export const Runs = () => {
  const model = useTrainingPreview()
  return (
    <Frame model={model}>
      <TrainingScreen model={model} />
    </Frame>
  )
}

/** With no apogee: Runs says it isn't installed, New run… off. */
export const Missing = () => {
  const model = useTrainingPreview({ apogee: 'missing' })
  return (
    <Frame model={model}>
      <TrainingScreen model={model} />
    </Frame>
  )
}

/** A config the CLI refuses: each read's failure in the CLI's words. */
export const ConfigTypo = () => {
  const model = useTrainingPreview({ apogee: 'config-typo' })
  return (
    <Frame model={model}>
      <TrainingScreen model={model} />
    </Frame>
  )
}

/** `training:pipelines`: the `skills` run going here, at stage 2, iteration 2. */
export const PipelineRunning = () => {
  const model = useTrainingPreview({
    world: 'pipelines',
    tab: 'pipelines',
    ticks: 24,
    then: async (training) => {
      training.requestRun('skills')
      await training.confirm('train')
    },
  })
  return (
    <Frame model={model}>
      <TrainingScreen model={model} />
    </Frame>
  )
}

/** `training:cycle`: a skipped pass, then a passing one that set anchor v1. */
export const Cycle = () => {
  const model = useTrainingPreview({ world: 'cycle', tab: 'cycle' })
  return (
    <Frame model={model}>
      <TrainingScreen model={model} />
    </Frame>
  )
}

/** `training:regimes`: Regimes, `foundation` chosen. */
export const Regimes = () => {
  const model = useTrainingPreview({ tab: 'regimes' })
  return (
    <Frame model={model}>
      <TrainingScreen model={model} />
    </Frame>
  )
}

/** `training:datasets`: the dataset and the kits; Pull… offered. */
export const Datasets = () => {
  const model = useTrainingPreview({ tab: 'datasets' })
  return (
    <Frame model={model}>
      <TrainingScreen model={model} />
    </Frame>
  )
}

/** A pull running: its line on the bar, Pull… off. */
export const Pulling = () => {
  const model = useTrainingPreview({ tab: 'datasets', ticks: 3, then: pull })
  return (
    <Frame model={model}>
      <TrainingScreen model={model} />
    </Frame>
  )
}

// Runs alone.

/** A run that failed its gate, chosen: its facts and its two buttons; the three promoted ledgers. */
export const RunsFailedGate = () => {
  const model = useTrainingPreview()
  return (
    <Frame model={model}>
      <RunsView model={model} />
    </Frame>
  )
}

/** With no apogee: the old words, and the detail's "Choose a run". */
export const RunsMissing = () => {
  const model = useTrainingPreview({ apogee: 'missing' })
  return (
    <Frame model={model}>
      <RunsView model={model} />
    </Frame>
  )
}

/** GUI 6i's home: a stage run still training, older than the five newest, listed by its id alone. */
export const RunsListedByID = () => {
  const model = useTrainingPreview({ world: 'stalelock' })
  return (
    <Frame model={model}>
      <RunsView model={model} />
    </Frame>
  )
}

// Pipelines alone.

/** No named pipeline and no run yet: the CLI's two lines. */
export const PipelinesNone = () => {
  const model = useTrainingPreview({ tab: 'pipelines' })
  return (
    <Frame model={model}>
      <PipelinesView model={model} />
    </Frame>
  )
}

/** Two named pipelines, no run chosen. */
export const PipelinesNamed = () => {
  const model = useTrainingPreview({ world: 'pipelines', tab: 'pipelines' })
  return (
    <Frame model={model}>
      <PipelinesView model={model} />
    </Frame>
  )
}

/** The `skills` run aborted at its second gate: its page, Resume from stage 2, refusals. */
export const PipelineAborted = () => {
  const model = useTrainingPreview({ world: 'pipelines', tab: 'pipelines', then: (training) => training.openPipelineRun('pipe-20261026-074112') })
  return (
    <Frame model={model}>
      <PipelinesView model={model} />
    </Frame>
  )
}

/** `training:cyclepass`: a cycle pass's page -- never Resume; Show the cycle. */
export const CyclePass = () => {
  const model = useTrainingPreview({ world: 'stalelock', tab: 'pipelines', then: (training) => training.openPipelineRun(sweep.cyclePass) })
  return (
    <Frame model={model}>
      <PipelinesView model={model} />
    </Frame>
  )
}

/** `training:passrunning`: that pass mid-pass, another process holding it. */
export const CyclePassRunning = () => {
  const model = useTrainingPreview({ world: 'passrunning', tab: 'pipelines', then: (training) => training.openPipelineRun(sweep.cyclePass) })
  return (
    <Frame model={model}>
      <PipelinesView model={model} />
    </Frame>
  )
}

/** `training:resumeheld`: a resume a live process holds, in its words. */
export const ResumeHeld = () => {
  const model = useTrainingPreview({ world: 'resumeheld', tab: 'pipelines', then: (training) => training.openPipelineRun(sweep.held) })
  return (
    <Frame model={model}>
      <PipelinesView model={model} />
    </Frame>
  )
}

/** A run whose spec file has gone: Resume held, the console line said. */
export const ResumeNeedsSpec = () => {
  const model = useTrainingPreview({ world: 'stalelock', tab: 'pipelines', then: (training) => training.openPipelineRun(sweep.specFileGone) })
  return (
    <Frame model={model}>
      <PipelinesView model={model} />
    </Frame>
  )
}

/** A run that keeps an earlier failed stage: its resume line says so. */
export const ResumeKeepsFailed = () => {
  const model = useTrainingPreview({ world: 'stalelock', tab: 'pipelines', then: (training) => training.openPipelineRun(sweep.keptFailed) })
  return (
    <Frame model={model}>
      <PipelinesView model={model} />
    </Frame>
  )
}

/** A run whose manifest no recording holds: the stand-in's words on the page. */
export const PipelineUnread = () => {
  const model = useTrainingPreview({ world: 'cycle', tab: 'pipelines', then: (training) => training.openPipelineRun('pipe-20261026-074116') })
  return (
    <Frame model={model}>
      <PipelinesView model={model} />
    </Frame>
  )
}

/** The stage list alone, as the view-model words the open page's stages. */
function StagesOf({ model }: { readonly model: TrainingViewModel }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={{ ...frame, height: 'auto', padding: 20 }}>
      <StageList stages={model.face().pipelines.page.stages} />
    </div>
  )
}

/** The stages alone: the `skills` run at stage 2, iteration 2 -- one gated and passed, one training. */
export const Stages = () => (
  <StagesOf
    model={useTrainingPreview({
      world: 'pipelines',
      tab: 'pipelines',
      ticks: 24,
      then: async (training) => {
        training.requestRun('skills')
        await training.confirm('train')
      },
    })}
  />
)

/** The manifest of a run that failed its gate (`train-pipeline-status-aborted.json`): greetings passed, refusals failed. */
export const StagesFailed = () => <StagesOf model={useTrainingPreview({ world: 'pipelines', tab: 'pipelines', then: (training) => training.openPipelineRun('pipe-20261026-074112') })} />


// The cycle alone.

/** No history yet: the old words; Run one pass offered. */
export const CycleNoHistory = () => {
  const model = useTrainingPreview({ tab: 'cycle' })
  return (
    <Frame model={model}>
      <CycleView model={model} />
    </Frame>
  )
}

/** Two passes, newest first, anchor v1. */
export const CycleHistory = () => {
  const model = useTrainingPreview({ world: 'cycle', tab: 'cycle' })
  return (
    <Frame model={model}>
      <CycleView model={model} />
    </Frame>
  )
}

/** A pass running here: its narration and its pipeline's stage. */
export const CyclePassHere = () => {
  const model = useTrainingPreview({
    world: 'cycle',
    tab: 'cycle',
    ticks: 10,
    then: async (training) => {
      training.requestPass()
      await training.confirm('train')
    },
  })
  return (
    <Frame model={model}>
      <CycleView model={model} />
    </Frame>
  )
}

/** The breaker's halt: the failed pass's note, Resume… offered. */
export const CycleHalted = () => {
  const model = useTrainingPreview({
    world: 'cycle',
    tab: 'cycle',
    ticks: 23,
    then: async (training) => {
      training.requestPass()
      await training.confirm('train')
    },
  })
  return (
    <Frame model={model}>
      <CycleView model={model} />
    </Frame>
  )
}

/** `training:stalelock`: a dead pass's lock, its path cut in its middle, Show in Finder. */
export const CycleStaleLock = () => {
  const model = useTrainingPreview({ world: 'stalelock', tab: 'cycle' })
  return (
    <Frame model={model}>
      <CycleView model={model} />
    </Frame>
  )
}

/** `training:unreadable`: the history the CLI cannot read, and what it holds. */
export const CycleUnreadable = () => {
  const model = useTrainingPreview({ world: 'unreadable', tab: 'cycle' })
  return (
    <Frame model={model}>
      <CycleView model={model} />
    </Frame>
  )
}

/** Another process's pass holding the cycle (a live lock). */
export const CycleHeld = () => {
  const model = useTrainingPreview({ world: 'passrunning', tab: 'cycle' })
  return (
    <Frame model={model}>
      <CycleView model={model} />
    </Frame>
  )
}

// Regimes alone.

/** `foundation`: four kits in order. */
export const RegimeFoundation = () => {
  const model = useTrainingPreview({ tab: 'regimes' })
  return (
    <Frame model={model}>
      <RegimesView model={model} />
    </Frame>
  )
}

/** `knowledge`: one kit. */
export const RegimeKnowledge = () => {
  const model = useTrainingPreview({ tab: 'regimes', then: (training) => training.selectRegime('knowledge') })
  return (
    <Frame model={model}>
      <RegimesView model={model} />
    </Frame>
  )
}

/** With no apogee: no regime read, nothing chosen. */
export const RegimesMissing = () => {
  const model = useTrainingPreview({ tab: 'regimes', apogee: 'missing' })
  return (
    <Frame model={model}>
      <RegimesView model={model} />
    </Frame>
  )
}

// Datasets alone.

/** One dataset, the eight kits. */
export const DatasetsList = () => {
  const model = useTrainingPreview({ tab: 'datasets' })
  return (
    <Frame model={model}>
      <DatasetsView model={model} />
    </Frame>
  )
}

/** With no apogee: the old words, "Choose a dataset." */
export const DatasetsMissing = () => {
  const model = useTrainingPreview({ tab: 'datasets', apogee: 'missing' })
  return (
    <Frame model={model}>
      <DatasetsView model={model} />
    </Frame>
  )
}

/** A pull finished: the banner above the list. */
export const DatasetsPulled = () => {
  const model = useTrainingPreview({ tab: 'datasets', ticks: 17, then: pull })
  return (
    <Frame model={model}>
      <DatasetsView model={model} />
    </Frame>
  )
}

/** The banner alone. */
export const Pulled = () => {
  const model = useTrainingPreview({ tab: 'datasets', ticks: 17, then: pull })
  return (
    <Frame model={model}>
      <PulledBanner model={model} />
    </Frame>
  )
}
