import modelsListServerTools from '@recorded/business/reads/Fixtures/models-list-server-tools.json?raw'
import executePlayRefused from '@recorded/data/protocol/Fixtures/execute-play-refused.jsonl?raw'
import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import type { ModelKind } from '../../../business/reads/Models'
import { useObserved } from '../../styles/Observation/useObserved'
import type { Specimen } from '../../styles/Specimens/Specimen'
import { ContextMeter } from '../ViewModels/SessionActions'
import type { RowFace, SessionFace, SessionViewModel } from '../ViewModels/SessionViewModel'
import { PermissionCardView } from '../Views/PermissionCardView'
import { QuestionCardView } from '../Views/QuestionCardView'
import { AttachmentChips, MeterView, ModelGlyph, ModelTag, SuggestionList } from '../Views/SessionExtras'
import { ModelMenu } from '../Views/SessionWindow'
import { ActivityView, noActs, SourcesRow, TranscriptRow } from '../Views/TranscriptRow'
import type { RowActs } from '../Views/TranscriptRow'
import { scenario } from './ScenarioStreams'
import { sessionSamples } from './SessionPresets'
import { useSessionPreview } from './SessionPreview'

/**
 * The session's specimens for the gallery's Components pages (design 1q):
 * its own views, each over a view-model replaying a recorded stream, as its
 * previews draw them -- the same recordings, scenarios and samples -- but
 * with no frame or theme of their own, so the board's theme is theirs. The
 * context meters alone draw numbers no recording holds (`Meter`, below).
 */

const column = { display: 'flex', flexDirection: 'column', gap: 12 } as const

/** A turn's rows of the kinds named, folded from the recording, with the acts the window hands them. */
function Rows({ model, kinds }: { readonly model: SessionViewModel; readonly kinds: readonly RowFace['kind'][] }) {
  useObserved(model)
  const acts: RowActs = { ...noActs, openCitation: (row, index) => model.openCitation(row, index), closeCitation: () => model.closeCitation() }
  return (
    <div style={column}>
      {model
        .face()
        .rows.filter((row) => kinds.includes(row.kind))
        .map((row) => (
          <TranscriptRow key={row.id} row={row} acts={acts} />
        ))}
    </div>
  )
}

/** A turn's activity alone: its thinking, the helpers and tools it ran. */
function Activity({ model }: { readonly model: SessionViewModel }) {
  useObserved(model)
  const activity = model.face().rows.find((row) => row.kind === 'activity')
  return activity?.kind === 'activity' ? <ActivityView activity={activity} /> : null
}

/** Each kind's badge, as the view-model faces the first backend of that kind the recorded `models list` names: its glyph and its tag. */
function Badges({ model, kinds }: { readonly model: SessionViewModel; readonly kinds: readonly ModelKind[] }) {
  useObserved(model)
  const items = model.menuGroups.flatMap((group) => group.items)
  const faces = kinds.flatMap((kind) => items.filter((item) => item.kind === kind).slice(0, 1).map((item) => model.modelFace(item.name)))
  return (
    <>
      {faces.map((face) => (
        <span key={face.name} style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
          <ModelGlyph kind={face.kind} />
          <ModelTag model={face} />
        </span>
      ))}
    </>
  )
}

/** The model menu as it opens under the header's model button: the recorded `models list`'s chat backends by kind. */
function Menu({ model }: { readonly model: SessionViewModel }) {
  useObserved(model)
  return (
    <div style={{ position: 'relative', width: 340, height: 540 }}>
      <ModelMenu model={model} />
    </div>
  )
}

/** The sources a recorded answer drew on, as its row lists them once it is finished. */
function Sources({ model }: { readonly model: SessionViewModel }) {
  useObserved(model)
  const answer = model.face().rows.find((row): row is Extract<RowFace, { kind: 'answer' }> => row.kind === 'answer' && row.finished && row.sources.length > 0)
  return answer ? <SourcesRow sources={answer.sources} open={(index) => model.openCitation(answer.id, index)} /> : null
}

/**
 * A context meter (the one exception to the recordings): `ContextMeter.of`,
 * the view-model's own reckoning, over the numbers 1u's previews take from
 * the old frames -- no recorded stream reports a turn's tokens, so none
 * yields a known meter.
 */
function Meter({ meter }: { readonly meter: ContextMeter }) {
  const face: SessionFace['header']['meter'] = {
    known: meter.percent !== undefined,
    text: meter.text,
    percent: meter.percent ?? 0,
    used: meter.used === undefined ? '' : ContextMeter.short(meter.used),
    window: meter.window === undefined ? '' : ContextMeter.short(meter.window),
    warns: meter.warns,
  }
  return <MeterView meter={face} open={() => {}} />
}

/** The standing card of a scenario, a permission or a question, asked or answered. */
function Permission({ model, shown = false }: { readonly model: SessionViewModel; readonly shown?: boolean }) {
  useObserved(model)
  const card = model.face().rows.find((row): row is Extract<RowFace, { kind: 'permission' }> => row.kind === 'permission')
  return card ? <PermissionCardView card={card} answer={(answer) => model.answer(card.id, [answer])} shown={shown} /> : null
}

function Question({ model }: { readonly model: SessionViewModel }) {
  useObserved(model)
  const card = model.face().rows.find((row): row is Extract<RowFace, { kind: 'question' }> => row.kind === 'question')
  return card ? <QuestionCardView card={card} answer={(answers) => model.answer(card.id, answers)} skip={() => model.skip(card.id)} /> : null
}

const answered = (answer: string) => (model: SessionViewModel) => model.answer(model.transcript.standingCard ?? -1, [answer])
const asked = 'How do I keep the batch going?'

export const specimens: readonly Specimen[] = [
  // Model, context and sources.
  {
    name: 'Model badge · each kind, its glyph and its tag',
    section: 'model',
    module: 'session',
    item: '1u',
    render: () => (
      <div style={column}>
        <Badges model={useSessionPreview(newUnsent, { ticks: 2 })} kinds={['local', 'api']} />
        <Badges model={useSessionPreview(newUnsent, { ticks: 2, models: modelsListServerTools })} kinds={['subscription']} />
      </div>
    ),
  },
  { name: 'Model menu · ⇧⌘M or /model', section: 'model', module: 'session', item: '1u', render: () => <Menu model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.toggleMenu('model') })} /> },
  { name: 'Context meter · under 80%', section: 'model', module: 'session', item: '1u', render: () => <Meter meter={ContextMeter.of(2140, 32768)} /> },
  { name: 'Context meter · 80% and over', section: 'model', module: 'session', item: '1u', render: () => <Meter meter={ContextMeter.of(28210, 32768)} /> },
  { name: 'Context meter · unknown', section: 'model', module: 'session', item: '1u', render: () => <Meter meter={ContextMeter.of(undefined, undefined)} /> },
  { name: 'Attachment chips · in the message box', section: 'model', module: 'session', item: '1u', render: () => <AttachmentChips paths={sessionSamples.attachChoices.map((choice) => choice.path)} detach={() => {}} /> },
  {
    name: '@ mention · the folder’s files',
    section: 'model',
    module: 'session',
    item: '1u',
    render: () => (
      <div style={{ position: 'relative', height: 300 }}>
        <div style={{ position: 'absolute', left: 0, right: 0, bottom: 0, height: 1 }}>
          <SuggestionList suggestions={{ kind: 'files', folder: sessionSamples.listing.folder, items: sessionSamples.listing.files.filter((file) => file.path.includes('src')).slice(0, 7) }} active={0} pickCommand={() => {}} pickFile={() => {}} />
        </div>
      </div>
    ),
  },
  { name: 'Citation · a source opened', section: 'model', module: 'session', item: '1v', render: () => <Rows model={useSessionPreview(...scenario.sources())} kinds={['answer']} /> },
  { name: 'Sources · each excerpt, its place and the CLI’s score', section: 'model', module: 'session', item: '1v', render: () => <Sources model={useSessionPreview(...scenario.sources())} /> },
  // The turn.
  { name: 'Thinking section · live', section: 'turn', module: 'session', item: '1u', render: () => <Activity model={useSessionPreview(markdownAnswer, { sends: [asked], ticks: 2 })} /> },
  { name: 'Thinking section · done, closed', section: 'turn', module: 'session', item: '1u', render: () => <Activity model={useSessionPreview(markdownAnswer, { sends: [asked], ticks: 3 })} /> },
  { name: 'Thinking section · streaming, tools run', section: 'turn', module: 'session', item: '1v', render: () => <Rows model={useSessionPreview(...scenario.streaming('chat'))} kinds={['activity']} /> },
  { name: 'Recalled · a past chat named', section: 'turn', module: 'session', item: '1v', render: () => <Rows model={useSessionPreview(...scenario.streaming())} kinds={['recalled']} /> },
  { name: 'Notice line · a billed switch', section: 'turn', module: 'session', item: '1v', render: () => <Rows model={useSessionPreview(...scenario.sources())} kinds={['notice']} /> },
  { name: 'The answer · streaming', section: 'turn', module: 'session', item: '1u', render: () => <Rows model={useSessionPreview(markdownAnswer, { sends: [asked], ticks: 60 })} kinds={['answer']} /> },
  // Asking and failing.
  { name: 'Permission card · change a file', section: 'asking', module: 'session', item: '1v', render: () => <Permission model={useSessionPreview(...scenario.permission())} /> },
  { name: 'Permission card · the change shown', section: 'asking', module: 'session', item: '1v', render: () => <Permission model={useSessionPreview(...scenario.permission())} shown /> },
  {
    name: 'Permission card · allowed once, collapsed to its line',
    section: 'asking',
    module: 'session',
    item: '1v',
    render: () => {
      const [recording, options] = scenario.permission()
      return <Rows model={useSessionPreview(recording, { ...options, then: answered('yes') })} kinds={['permission']} />
    },
  },
  {
    name: 'Permission card · denied, the model told',
    section: 'asking',
    module: 'session',
    item: '1v',
    render: () => {
      const [recording, options] = scenario.permission()
      return <Rows model={useSessionPreview(recording, { ...options, then: answered('no') })} kinds={['permission']} />
    },
  },
  { name: 'Question card · asking', section: 'asking', module: 'session', item: '1v', render: () => <Question model={useSessionPreview(...scenario.question())} /> },
  {
    name: 'Question card · answered',
    section: 'asking',
    module: 'session',
    item: '1v',
    render: () => {
      const [recording, options] = scenario.question()
      return <Rows model={useSessionPreview(recording, { ...options, then: answered('Per batch') })} kinds={['question']} />
    },
  },
  { name: 'Error card · a local model didn’t load', section: 'asking', module: 'session', item: '1v', render: () => <Rows model={useSessionPreview(...scenario.error())} kinds={['error']} /> },
  { name: 'Error card · a turn that failed', section: 'asking', module: 'session', item: '1u', render: () => <Rows model={useSessionPreview(executePlayRefused, { launch: { execute: {} }, sends: ['/play nosuch'], ticks: 3 })} kinds={['error']} /> },
]
