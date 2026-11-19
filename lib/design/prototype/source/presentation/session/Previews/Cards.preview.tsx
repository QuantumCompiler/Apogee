import { useObserved } from '../../styles/Observation/useObserved'
import type { RowFace, SessionViewModel } from '../ViewModels/SessionViewModel'
import { DiffView, PermissionCardView } from '../Views/PermissionCardView'
import { QuestionCardView } from '../Views/QuestionCardView'
import { scenario } from './ScenarioStreams'
import { useSessionPreview } from './SessionPreview'

/**
 * The question and permission cards on their own (design 1v), each over its
 * scenario's replay: the gate asking, its change shown, each answer, and
 * closed unanswered; the question asking, an option or one's own answer
 * picked, answered, skipped.
 */
const frame = { width: 760, padding: 24, display: 'flex', flexDirection: 'column', gap: 16, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Permission({ model, shown = false, closed = false }: { readonly model: SessionViewModel; readonly shown?: boolean; readonly closed?: boolean }) {
  useObserved(model)
  const found = model.face().rows.find((row): row is Extract<RowFace, { kind: 'permission' }> => row.kind === 'permission')
  // Closed: its turn ended before an answer -- the face the transcript gives a card a cancel or a death closed.
  const card = found && closed ? { ...found, closed: true } : found
  return (
    <div className="ap" data-theme="dark" style={frame}>
      {card ? <PermissionCardView card={card} answer={(answer) => model.answer(card.id, [answer])} shown={shown} /> : null}
    </div>
  )
}

function Question({ model, picked, closed = false }: { readonly model: SessionViewModel; readonly picked?: ReadonlyArray<number | 'own' | undefined>; readonly closed?: boolean }) {
  useObserved(model)
  const found = model.face().rows.find((row): row is Extract<RowFace, { kind: 'question' }> => row.kind === 'question')
  const card = found && closed ? { ...found, closed: true } : found
  return (
    <div className="ap" data-theme="dark" style={frame}>
      {card ? <QuestionCardView card={card} answer={(answers) => model.answer(card.id, answers)} skip={() => model.skip(card.id)} picked={picked} /> : null}
    </div>
  )
}

const answered = (answer: string) => (model: SessionViewModel) => model.answer(model.transcript.standingCard ?? -1, [answer])

/** `chat:retry`: the gate asks to change a file, its change folded. */
export const PermissionAsking = () => <Permission model={useSessionPreview(...scenario.permission())} />

/** Show changes: the diff the CLI's detail carries, each line in its color. */
export const PermissionChangesShown = () => <Permission model={useSessionPreview(...scenario.permission())} shown />

/** Allowed once: the card collapsed to its line. */
export const PermissionAllowedOnce = () => {
  const [recording, options] = scenario.permission()
  return <Permission model={useSessionPreview(recording, { ...options, then: answered('yes') })} />
}

/** Denied: the card collapsed to its line, and the old note under it -- the model was told and carried on. */
export const PermissionDenied = () => {
  const [recording, options] = scenario.permission()
  return <Permission model={useSessionPreview(recording, { ...options, then: answered('no') })} />
}

/** Closed unanswered: its turn ended first -- a stop while it asked, or its child gone. */
export const PermissionClosed = () => <Permission model={useSessionPreview(...scenario.permission())} closed />

/** The change on its own: the CLI's unified diff. */
export const TheChange = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <DiffView diff={'--- a/src/ingest/runner.py\n+++ b/src/ingest/runner.py\n@@ -71,3 +71,4 @@\n-    async def _fetch(self, url: str) -> bytes:\n+    async def _fetch(self, url: str) -> bytes | None:\n+        retries = self.settings.fetch.retries\n'} />
  </div>
)

/** `chat:pr`: Claude asks, nothing picked yet, Answer off. */
export const QuestionAsking = () => <Question model={useSessionPreview(...scenario.question())} />

/** An option picked: Answer on. */
export const QuestionPicked = () => <Question model={useSessionPreview(...scenario.question())} picked={[1]} />

/** One's own answer picked, nothing typed yet: Answer off until it has words. */
export const QuestionOwnAnswer = () => <Question model={useSessionPreview(...scenario.question())} picked={['own']} />

/** `chat:pr` as its preset opens it: Per batch picked, as the old frame's state (`pr: { choice: 'batch' }`), Answer on; nothing written yet. */
export const QuestionPreselected = () => {
  const [recording, options] = scenario.question()
  return <Question model={useSessionPreview(recording, { ...options, before: (model) => model.preselect('Per batch') })} />
}

/** Two items asked at once: a fieldset for each, Answer on only once each has an answer (one answer line per item). */
export const QuestionTwoItems = () => <Question model={useSessionPreview(...scenario.questionTwoItems())} picked={[1]} />

/** Answered: what was asked and what was said, with no "change answer" -- the CLI reads one answer per question. */
export const QuestionAnswered = () => {
  const [recording, options] = scenario.question()
  return <Question model={useSessionPreview(recording, { ...options, then: answered('Per batch') })} />
}

/** Closed unanswered: its turn ended before it was answered. */
export const QuestionClosed = () => <Question model={useSessionPreview(...scenario.question())} closed />

/** Skipped: an empty answer, said as skipped. */
export const QuestionSkipped = () => {
  const [recording, options] = scenario.question()
  return <Question model={useSessionPreview(recording, { ...options, then: (model) => model.skip(model.transcript.standingCard ?? -1) })} />
}
