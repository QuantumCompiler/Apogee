import datasetsKits from '@recorded/business/reads/Fixtures/datasets-kits.json?raw'
import datasetsList from '@recorded/business/reads/Fixtures/datasets-list.json?raw'
import datasetsPullText from '@recorded/business/reads/Fixtures/datasets-pull-handmade.txt?raw'
import cycleHalt from '@recorded/business/reads/Fixtures/train-cycle-halt.txt?raw'
import cycleHaltDuringPass from '@recorded/business/reads/Fixtures/train-cycle-halt-during-pass.txt?raw'
import cycleResume from '@recorded/business/reads/Fixtures/train-cycle-resume.txt?raw'
import cycleResumeAfterKept from '@recorded/business/reads/Fixtures/train-cycle-resume-after-kept.txt?raw'
import cycleStatusLiveLock from '@recorded/business/reads/Fixtures/train-cycle-status-live-lock.json?raw'
import cycleStatusPassed from '@recorded/business/reads/Fixtures/train-cycle-status-passed.json?raw'
import cycleStatusStaleLock from '@recorded/business/reads/Fixtures/train-cycle-status-stale-lock.json?raw'
import cycleStatusUnreadable from '@recorded/business/reads/Fixtures/train-cycle-status-unreadable.err?raw'
import pipelineList from '@recorded/business/reads/Fixtures/train-pipeline-list.json?raw'
import pipelineListNone from '@recorded/business/reads/Fixtures/train-pipeline-list-none.json?raw'
import pipelineListSweep from '@recorded/business/reads/Fixtures/train-pipeline-list-sweep.json?raw'
import pipelineResumeCyclePass from '@recorded/business/reads/Fixtures/train-pipeline-resume-cycle-pass.err?raw'
import pipelineResumeHeld from '@recorded/business/reads/Fixtures/train-pipeline-resume-held.err?raw'
import pipelineResumeNoSpec from '@recorded/business/reads/Fixtures/train-pipeline-resume-no-spec.err?raw'
import pipelineStatusAborted from '@recorded/business/reads/Fixtures/train-pipeline-status-aborted.json?raw'
import pipelineStatusComplete from '@recorded/business/reads/Fixtures/train-pipeline-status-complete.json?raw'
import pipelineStatusCyclePass from '@recorded/business/reads/Fixtures/train-pipeline-status-cycle-pass.json?raw'
import pipelineStatusKeptFailed from '@recorded/business/reads/Fixtures/train-pipeline-status-kept-failed.json?raw'
import pipelineStatusList from '@recorded/business/reads/Fixtures/train-pipeline-status-list.json?raw'
import pipelineStatusNamed from '@recorded/business/reads/Fixtures/train-pipeline-status-named.json?raw'
import pipelineStatusNamedHeld from '@recorded/business/reads/Fixtures/train-pipeline-status-named-held.json?raw'
import pipelineStatusNone from '@recorded/business/reads/Fixtures/train-pipeline-status-none.json?raw'
import pipelineStatusResumesList from '@recorded/business/reads/Fixtures/train-pipeline-status-resumes-list.json?raw'
import pipelineStatusSpecFileGone from '@recorded/business/reads/Fixtures/train-pipeline-status-spec-file-gone.json?raw'
import pipelineStatusStopped from '@recorded/business/reads/Fixtures/train-pipeline-status-stopped.json?raw'
import promoteAdapterNoSuite from '@recorded/business/reads/Fixtures/train-promote-adapter-no-suite.err?raw'
import promoteAdapterErr from '@recorded/business/reads/Fixtures/train-promote-adapter.err?raw'
import promoteAdapterOut from '@recorded/business/reads/Fixtures/train-promote-adapter.out?raw'
import promoteHoldsMerged from '@recorded/business/reads/Fixtures/train-promote-holds-merged.err?raw'
import promoteNamesAdapter from '@recorded/business/reads/Fixtures/train-promote-names-adapter.err?raw'
import trainRegimes from '@recorded/business/reads/Fixtures/train-regimes.json?raw'
import trainRun from '@recorded/business/reads/Fixtures/train-run.err?raw'
import trainStatus from '@recorded/business/reads/Fixtures/train-status.json?raw'
import trainStatusCycle from '@recorded/business/reads/Fixtures/train-status-cycle.json?raw'
import trainStatusLiveLock from '@recorded/business/reads/Fixtures/train-status-cycle-live-lock.json?raw'
import trainStatusStaleLock from '@recorded/business/reads/Fixtures/train-status-cycle-stale-lock.json?raw'
import trainStatusUnreadable from '@recorded/business/reads/Fixtures/train-status-cycle-unreadable.json?raw'
import trainVersions from '@recorded/business/reads/Fixtures/train-versions-adapters.json?raw'
import datasetsPull from '@recorded/business/jobs/Fixtures/datasets-pull-handmade.jsonl?raw'
import cycleRunFailed from '@recorded/business/jobs/Fixtures/train-cycle-run-failed.jsonl?raw'
import cycleRunLocked from '@recorded/business/jobs/Fixtures/train-cycle-run-locked.jsonl?raw'
import cycleRunPassed from '@recorded/business/jobs/Fixtures/train-cycle-run-passed.jsonl?raw'
import cycleRunSkipped from '@recorded/business/jobs/Fixtures/train-cycle-run-skipped.jsonl?raw'
import cycleRunUnconfigured from '@recorded/business/jobs/Fixtures/train-cycle-run-unconfigured.jsonl?raw'
import pipelineResume from '@recorded/business/jobs/Fixtures/train-pipeline-resume.jsonl?raw'
import pipelineRun from '@recorded/business/jobs/Fixtures/train-pipeline-run.jsonl?raw'
import pipelineRunNightly from '@recorded/business/jobs/Fixtures/train-pipeline-run-nightly.jsonl?raw'
import prepareHeatNotes from '../../../../fixtures/business/jobs/datasets-prepare-heat-notes-handmade.err?raw'
import promoteMergedErr from '../../../../fixtures/business/jobs/train-promote-merged-handmade.err?raw'
import promoteMergedOut from '../../../../fixtures/business/jobs/train-promote-merged-handmade.out?raw'
import pipelineStatusCyclePassRunning from '../../../../fixtures/business/reads/train-pipeline-status-cycle-pass-running-handmade.json?raw'
import pipelineStatusResumesListRunning from '../../../../fixtures/business/reads/train-pipeline-status-resumes-list-running-handmade.json?raw'
import promoteGateFailed from '../../../../fixtures/business/reads/train-promote-gate-failed-handmade.err?raw'
import type { Rules } from '../../../infrastructure/process/StandInChild'
import { trainingWorld as rulesOf } from './TrainingWorldRules'
import type { Lookup, TrainingWorld } from './TrainingWorldRules'

export * from './TrainingWorldRules'

/**
 * The Training place's recordings (design 1j), each a `?raw` import the
 * build inlines, named in `recordings` by its file name; the worlds that
 * answer with them are `TrainingWorldRules.ts`, which imports none, so the
 * fixture check lists what every preset names without loading a fixture.
 */
/** Every file the worlds answer with, by its name under its fixture root. */
const recordings: Readonly<Record<string, string>> = {
  'datasets-kits.json': datasetsKits,
  'datasets-list.json': datasetsList,
  'datasets-pull-handmade.txt': datasetsPullText,
  'train-cycle-halt.txt': cycleHalt,
  'train-cycle-halt-during-pass.txt': cycleHaltDuringPass,
  'train-cycle-resume.txt': cycleResume,
  'train-cycle-resume-after-kept.txt': cycleResumeAfterKept,
  'train-cycle-status-live-lock.json': cycleStatusLiveLock,
  'train-cycle-status-passed.json': cycleStatusPassed,
  'train-cycle-status-stale-lock.json': cycleStatusStaleLock,
  'train-cycle-status-unreadable.err': cycleStatusUnreadable,
  'train-pipeline-list.json': pipelineList,
  'train-pipeline-list-none.json': pipelineListNone,
  'train-pipeline-list-sweep.json': pipelineListSweep,
  'train-pipeline-resume-cycle-pass.err': pipelineResumeCyclePass,
  'train-pipeline-resume-held.err': pipelineResumeHeld,
  'train-pipeline-resume-no-spec.err': pipelineResumeNoSpec,
  'train-pipeline-status-aborted.json': pipelineStatusAborted,
  'train-pipeline-status-complete.json': pipelineStatusComplete,
  'train-pipeline-status-cycle-pass.json': pipelineStatusCyclePass,
  'train-pipeline-status-kept-failed.json': pipelineStatusKeptFailed,
  'train-pipeline-status-list.json': pipelineStatusList,
  'train-pipeline-status-named.json': pipelineStatusNamed,
  'train-pipeline-status-named-held.json': pipelineStatusNamedHeld,
  'train-pipeline-status-none.json': pipelineStatusNone,
  'train-pipeline-status-resumes-list.json': pipelineStatusResumesList,
  'train-pipeline-status-spec-file-gone.json': pipelineStatusSpecFileGone,
  'train-pipeline-status-stopped.json': pipelineStatusStopped,
  'train-promote-adapter-no-suite.err': promoteAdapterNoSuite,
  'train-promote-adapter.err': promoteAdapterErr,
  'train-promote-adapter.out': promoteAdapterOut,
  'train-promote-holds-merged.err': promoteHoldsMerged,
  'train-promote-names-adapter.err': promoteNamesAdapter,
  'train-regimes.json': trainRegimes,
  'train-run.err': trainRun,
  'train-status.json': trainStatus,
  'train-status-cycle.json': trainStatusCycle,
  'train-status-cycle-live-lock.json': trainStatusLiveLock,
  'train-status-cycle-stale-lock.json': trainStatusStaleLock,
  'train-status-cycle-unreadable.json': trainStatusUnreadable,
  'train-versions-adapters.json': trainVersions,
  'datasets-pull-handmade.jsonl': datasetsPull,
  'train-cycle-run-failed.jsonl': cycleRunFailed,
  'train-cycle-run-locked.jsonl': cycleRunLocked,
  'train-cycle-run-passed.jsonl': cycleRunPassed,
  'train-cycle-run-skipped.jsonl': cycleRunSkipped,
  'train-cycle-run-unconfigured.jsonl': cycleRunUnconfigured,
  'train-pipeline-resume.jsonl': pipelineResume,
  'train-pipeline-run.jsonl': pipelineRun,
  'train-pipeline-run-nightly.jsonl': pipelineRunNightly,
  'datasets-prepare-heat-notes-handmade.err': prepareHeatNotes,
  'train-promote-merged-handmade.err': promoteMergedErr,
  'train-promote-merged-handmade.out': promoteMergedOut,
  'train-pipeline-status-cycle-pass-running-handmade.json': pipelineStatusCyclePassRunning,
  'train-pipeline-status-resumes-list-running-handmade.json': pipelineStatusResumesListRunning,
  'train-promote-gate-failed-handmade.err': promoteGateFailed,
}

/** A file's text, by its name; a name no import holds is refused, naming it. */
export function recording(name: string): string {
  const text = recordings[name]
  if (text === undefined) throw new Error(`no fixture named ${name}`)
  return text
}

/** A world's rules over the recordings: `TrainingWorldRules.ts`'s, each file looked up here. */
export function trainingWorld(world: TrainingWorld, look: Lookup = recording): Rules {
  return rulesOf(world, look)
}

/** The place's answers on every other preset (design 1j): the `recorded` world, so Training opens whole from any screen. */
export function trainingPlaceRules(): Rules {
  return trainingWorld('recorded')
}

/** `rules` with a world's beside them, the world's later, so its answers win. */
export function withTrainingWorld(rules: Rules, world: TrainingWorld): Rules {
  const training = trainingWorld(world)
  return {
    ...rules,
    reads: [...(rules.reads ?? []), ...(training.reads ?? [])],
    commands: [...(rules.commands ?? []), ...(training.commands ?? [])],
    jobs: [...(rules.jobs ?? []), ...(training.jobs ?? [])],
  }
}
