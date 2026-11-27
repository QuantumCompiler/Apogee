import executeProject from '@recorded/data/protocol/Fixtures/execute-project.jsonl?raw'
import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import mockActor from '@recorded/data/protocol/Fixtures/mock-actor.jsonl?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import stoppedTurn from '@recorded/data/protocol/Fixtures/stopped-turn.jsonl?raw'
import executePlay from '../../../../fixtures/data/protocol/execute-play-summarize-verify.jsonl?raw'
import { useObserved } from '../../styles/Observation/useObserved'
import type { RowFace, SessionViewModel } from '../ViewModels/SessionViewModel'
import executePlayRefused from '@recorded/data/protocol/Fixtures/execute-play-refused.jsonl?raw'
import { ActivityView, EndedCard, noActs, QuietLine, SourcesRow, TranscriptRow } from '../Views/TranscriptRow'
import type { RowActs } from '../Views/TranscriptRow'
import { scenario } from './ScenarioStreams'
import type { SessionPreviewOptions } from './SessionPreview'
import { useSessionPreview } from './SessionPreview'

/**
 * The conversation's rows on their own (design 1u), each folded from a
 * recorded stream: the person's line, a notice, a turn's activity live and
 * closed, a play's card, an answer streaming and answered, a stop, the quiet
 * line for what this app does not draw, and an end.
 */
const frame = { width: 820, padding: 24, display: 'flex', flexDirection: 'column', gap: 16, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Rows({ model, kinds }: { readonly model: SessionViewModel; readonly kinds?: readonly RowFace['kind'][] }) {
  useObserved(model)
  const rows = model.face().rows.filter((row) => kinds === undefined || kinds.includes(row.kind))
  return (
    <div className="ap" data-theme="dark" style={frame}>
      {rows.map((row) => (
        <TranscriptRow key={row.id} row={row} />
      ))}
    </div>
  )
}

function useRows(recording: string, options: SessionPreviewOptions) {
  return useSessionPreview(recording, options)
}

/** execute-project.jsonl: the launch's notices, the person's line, the turn's own notices, its thought and its answer. */
export const OpenedInAProject = () => <Rows model={useRows(executeProject, { launch: { execute: {} }, sends: ['hi'], ticks: 12 })} />

/** The person's line: the bubble on the right. */
export const UserLine = () => <Rows model={useRows(executeProject, { launch: { execute: {} }, sends: ['hi'], ticks: 12 })} kinds={['user']} />

/** A notice between its rules. */
export const Notice = () => <Rows model={useRows(executeProject, { launch: { execute: {} }, ticks: 4 })} kinds={['notice']} />

/** A turn thinking: its activity live. */
export const ThinkingLive = () => <Rows model={useRows(markdownAnswer, { sends: ['How do I keep the batch going?'], ticks: 2 })} kinds={['activity']} />

/** A turn that thought and said nothing of it: closed to its line. */
export const Thought = () => <Rows model={useRows(markdownAnswer, { sends: ['How do I keep the batch going?'], ticks: 3 })} kinds={['activity']} />

/** A play: its card, each stage marked where it stands. */
export const PlayCard = () => (
  <Rows
    model={useRows(executePlay, {
      launch: { execute: { suite: 'coding' } },
      sends: ['/play summarize-verify The board met on 14 October. It agreed to move the release to 3 November, and Ana Ruiz will own the checklist.'],
      ticks: 15,
    })}
    kinds={['activity', 'answer']}
  />
)

/** An answer streaming. */
export const AnswerStreaming = () => <Rows model={useRows(markdownAnswer, { sends: ['How do I keep the batch going?'], ticks: 60 })} kinds={['answer']} />

/** An answer finished: the model its result names, in its footer. */
export const AnswerFinished = () => <Rows model={useRows(markdownAnswer, { sends: ['How do I keep the batch going?'], ticks: 200 })} kinds={['answer']} />

/** A turn stopped mid-answer: what it said, then the quiet stopped line. */
export const StoppedTurn = () => <Rows model={useRows(stoppedTurn, { sends: ['Which port does the dev server use?', 'When is the launch?'], ticks: 14 })} />

/** mock-actor.jsonl's question, the Mac app's own recording: its card (1v), where 1u drew the quiet line. */
export const MockActorQuestion = () => <Rows model={useRows(mockActor, { sends: ['Write me a note.'], ticks: 10 })} kinds={['question']} />

/** A quiet line on its own. */
export const Quiet = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <QuietLine text="stopped" />
  </div>
)

/** The session's end: a launch the CLI refused, in its words. */
export const RefusedAtLaunch = () => <Rows model={useRows(newUnsent, { apogee: 'config-typo', ticks: 2 })} kinds={['ended']} />

/** The activity on its own, closed to its line, its reasoning a click away: mock-actor.jsonl's, closed by its question. */
export const ActivityClosed = () => <Activity model={useRows(mockActor, { sends: ['Write me a note.'], ticks: 10 })} />

function Activity({ model }: { readonly model: SessionViewModel }) {
  useObserved(model)
  const activity = model.face().rows.find((row) => row.kind === 'activity')
  return (
    <div className="ap" data-theme="dark" style={frame}>
      {activity?.kind === 'activity' ? <ActivityView activity={activity} /> : null}
    </div>
  )
}

// MARK: - 1v's rows

/** Rows of a scenario's frame, with the acts the window hands them. */
function ScenarioRows({ model, kinds }: { readonly model: SessionViewModel; readonly kinds: readonly RowFace['kind'][] }) {
  useObserved(model)
  const rows = model.face().rows.filter((row) => kinds.includes(row.kind))
  const acts: RowActs = {
    ...noActs,
    openCitation: (row, index) => model.openCitation(row, index),
    closeCitation: () => model.closeCitation(),
    resend: (text) => model.resend(text),
    retry: () => model.retry(),
    askInstead: (name) => model.askInstead(name),
  }
  return (
    <div className="ap" data-theme="dark" style={frame}>
      {rows.map((row) => (
        <TranscriptRow key={row.id} row={row} acts={acts} />
      ))}
    </div>
  )
}

/** `chat`: what turn 2 recalled, the past chat named and openable. */
export const Recalled = () => <ScenarioRows model={useSessionPreview(...scenario.streaming())} kinds={['recalled']} />

/** `chat:wal`: the switch to the billed claude, the CLI's notice marked billed per use from here. */
export const BilledSwitch = () => <ScenarioRows model={useSessionPreview(...scenario.sources())} kinds={['notice']} />

/** `chat:wal`: the cited answer, citation 1 open under it, its SOURCES after it. */
export const CitationOpen = () => <ScenarioRows model={useSessionPreview(...scenario.sources())} kinds={['answer']} />

/** The cited answer with no citation open. */
export const Cited = () => {
  const [recording, options] = scenario.sources()
  return <ScenarioRows model={useSessionPreview(recording, { ...options, then: (model) => model.closeCitation() })} kinds={['answer']} />
}

/** The SOURCES list on its own: each excerpt's file, its place and the CLI's score. */
export const Sources = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <SourcesRow
      sources={[
        { index: 1, file: 'storage-engine-design.pdf', place: 'pages 185–189', score: 'score 0.94 · lexical' },
        { index: 2, file: 'postgresql.conf', place: 'lines 209–219', score: 'score 0.91 · lexical' },
      ]}
      open={() => {}}
    />
  </div>
)

/** `chat:crash`: vast didn't load -- the CLI's words, Unload and retry, Ask claude instead · billed. */
export const FailedLoad = () => <ScenarioRows model={useSessionPreview(...scenario.error())} kinds={['error']} />

/** A turn that failed otherwise: the Mac app's card, The turn failed and Try again (execute-play-refused.jsonl). */
export const TurnFailed = () => <Rows model={useRows(executePlayRefused, { launch: { execute: {} }, sends: ['/play nosuch'], ticks: 3 })} kinds={['error']} />

/** `chat:restarting`: the kv chat read back -- the loader's warning, its turn, the divider where the live session takes over. */
export const ReadBack = () => <ScenarioRows model={useSessionPreview(...scenario.restarting())} kinds={['warning', 'user', 'answer', 'divider', 'notice']} />

/** `chat:notsent`: Not sent, Send again enabled: the session is back. */
export const NotSent = () => <ScenarioRows model={useSessionPreview(...scenario.notSent())} kinds={['user', 'notSent']} />

/** Not sent while the child is still to come back: Send again off. */
export const NotSentWaiting = () => {
  const [recording, options] = scenario.notSent()
  return <ScenarioRows model={useSessionPreview(recording, { ...options, ticks: 0 })} kinds={['notSent']} />
}

/** `chat:ended`: Session ended, the CLI's words, Details closed, Start again. */
export const SessionEnded = () => <ScenarioRows model={useSessionPreview(...scenario.ended())} kinds={['ended']} />

/** Session ended, its Details open: the child's stderr tail. */
export const EndedDetails = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <EndedCard text="apogee exited with status 2: fatal: the model went away" details={'loading\nfatal: the model went away'} canStartAgain startAgain={() => {}} open />
  </div>
)
