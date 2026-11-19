import executeProject from '@recorded/data/protocol/Fixtures/execute-project.jsonl?raw'
import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import mockActor from '@recorded/data/protocol/Fixtures/mock-actor.jsonl?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import stoppedTurn from '@recorded/data/protocol/Fixtures/stopped-turn.jsonl?raw'
import executePlay from '../../../../fixtures/data/protocol/execute-play-summarize-verify.jsonl?raw'
import { useObserved } from '../../styles/Observation/useObserved'
import type { RowFace, SessionViewModel } from '../ViewModels/SessionViewModel'
import { ActivityView, QuietLine, TranscriptRow } from '../Views/TranscriptRow'
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

/** What this app does not draw yet: mock-actor.jsonl's question, a quiet line naming its type until 1v draws its card, never dropped. */
export const NotDrawnYet = () => <Rows model={useRows(mockActor, { sends: ['Write me a note.'], ticks: 10 })} kinds={['quiet']} />

/** A quiet line on its own. */
export const Quiet = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <QuietLine text="stopped" />
  </div>
)

/** The session's end: a launch the CLI refused, in its words. */
export const Ended = () => <Rows model={useRows(newUnsent, { apogee: 'config-typo', ticks: 2 })} kinds={['ended']} />

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
