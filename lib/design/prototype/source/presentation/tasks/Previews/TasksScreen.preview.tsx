import { TaskDetail } from '../Views/TaskDetail'
import { TaskList } from '../Views/TaskList'
import { TasksScreen } from '../Views/TasksScreen'
import { recordedRun, reportRun, tasks } from './TasksWorlds'
import { useTasksPreview } from './TasksPreview'

/** The ledger, read: its first task shown, done. */
export const LedgerRead = () => <TasksScreen model={useTasksPreview()} />

/** A live task in round 1, as `tasks` opens: the recorded run, its stops offered. */
export const LiveRound = () => <TasksScreen model={useTasksPreview({ ticks: 16, then: (place) => place.startRun(recordedRun) })} />

/** The live task asked to stop after its round: said so until round 1 ends. */
export const Halting = () => <TasksScreen model={useTasksPreview({ ticks: 14, then: (place) => place.startRun(recordedRun), after: (place) => place.halt() })} />

/** The recorded run played to its end: exhausted, Run again with twice its rounds. */
export const Exhausted = () => <TasksScreen model={useTasksPreview({ ticks: 40, then: (place) => place.startRun(recordedRun) })} />

/** The recorded uses run, as `tasks:denied` opens: a call its grant let through, one refused with nobody present. */
export const Denied = () => <TasksScreen model={useTasksPreview({ ticks: 40, then: (place) => place.startRun(reportRun) })} />

/** A task killed mid-round: Resume, where, and the old line. */
export const Interrupted = () => <TasksScreen model={useTasksPreview({ then: (place) => place.select(tasks.interrupted) })} />

/** A resume starting up, as `tasks:resuming` opens: Resuming… and the stops. */
export const Resuming = () => (
  <TasksScreen
    model={useTasksPreview({
      world: 'resuming',
      ticks: 1,
      then: async (place) => {
        await place.select(tasks.stalled)
        place.resume()
      },
    })}
  />
)

/** A halt sent while that resume started up, kept: once its `task_started` is out, the page says it halts before round 3, nothing run. */
export const HaltKept = () => (
  <TasksScreen
    model={useTasksPreview({
      world: 'resuming',
      ticks: 1,
      then: async (place) => {
        await place.select(tasks.stalled)
        place.resume()
      },
      after: (place) => place.halt(),
      afterTicks: 4,
    })}
  />
)

/** A resume the config refused, as `tasks:resumerefused` opens: its line kept on the page. */
export const ResumeRefused = () => (
  <TasksScreen
    model={useTasksPreview({
      world: 'resumerefused',
      ticks: 1,
      then: async (place) => {
        await place.select(tasks.stalledGranted)
        place.resume()
      },
    })}
  />
)

/** The list alone, the ledger of 60: the newest 50 and Show all, as `tasks:more` opens. */
export const NewestFifty = () => <TaskList model={useTasksPreview({ world: 'sweep' })} />

/** No apogee: nothing read, nothing drawn, as the old place draws no missing state. */
export const Missing = () => <TasksScreen model={useTasksPreview({ apogee: 'missing' })} />

/** A config apogee cannot read: its refusal said in the list. */
export const ConfigTypo = () => <TasksScreen model={useTasksPreview({ apogee: 'config-typo' })} />

/** The page alone: the exhausted say hello, read. */
export const DetailExhausted = () => <TaskDetail model={useTasksPreview({ then: (place) => place.select(tasks.exhausted) })} />
