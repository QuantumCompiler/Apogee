import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import privateUnsent from '@recorded/data/protocol/Fixtures/private-unsent.jsonl?raw'
import { useObserved } from '../../styles/Observation/useObserved'
import { ContextMeter } from '../ViewModels/SessionActions'
import type { SessionFace, SessionViewModel } from '../ViewModels/SessionViewModel'
import { AttachmentChips, EmptySessionView, InspectorPlaceholder, MeterView, ModelGlyph, ModelTag, SuggestionList } from '../Views/SessionExtras'
import { sessionSamples } from './SessionPresets'
import { useSessionPreview } from './SessionPreview'

/** The session's smaller views on their own (design 1u), one preview per state a person sees differently. */
const frame = { position: 'relative', width: 820, padding: 24, display: 'flex', flexDirection: 'column', gap: 16, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const
const noActs = { attachFolder: () => {}, chooseModel: () => {}, chooseSuite: () => {}, privateInstead: () => {} }

function Empty({ model }: { readonly model: SessionViewModel }) {
  useObserved(model)
  const face = model.face()
  return (
    <div className="ap" data-theme="dark" style={{ ...frame, height: 420 }}>
      <EmptySessionView face={face.empty} model={face.header.model} acts={noActs} />
    </div>
  )
}

/** A new session: what are we working on, the model it runs on, the first acts. */
export const EmptyNew = () => <Empty model={useSessionPreview(newUnsent, { ticks: 2 })} />

/** A private one: what is and is not kept, and the CLI's notice under it. */
export const EmptyPrivate = () => <Empty model={useSessionPreview(privateUnsent, { launch: { privately: true }, ticks: 3 })} />

/** Not launched (the monitor's, until 1v): no model named yet, and the acts that need a session drawn off. */
export const EmptyNotLaunched = () => <Empty model={useSessionPreview(newUnsent, { unlaunched: true })} />

function Meter({ meter }: { readonly meter: ContextMeter }) {
  const face: SessionFace['header']['meter'] = {
    known: meter.percent !== undefined,
    text: meter.text,
    percent: meter.percent ?? 0,
    used: meter.used === undefined ? '' : ContextMeter.short(meter.used),
    window: meter.window === undefined ? '' : ContextMeter.short(meter.window),
    warns: meter.warns,
  }
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <MeterView meter={face} open={() => {}} />
    </div>
  )
}

/** The window unknown: models info states none (claude's). */
export const MeterUnknown = () => <Meter meter={ContextMeter.of(undefined, undefined)} />

/** The window known, no turn's tokens reported: root's 8192. */
export const MeterNotReported = () => <Meter meter={ContextMeter.of(undefined, 8192)} />

/** A turn's tokens against the window: the old chat:play frame's 2,140 of 32,768. */
export const MeterLow = () => <Meter meter={ContextMeter.of(2140, 32768)} />

/** Past 80%, warning: the old TaskGroup frame's 28,210 of 32,768. */
export const MeterWarning = () => <Meter meter={ContextMeter.of(28210, 32768)} />

/** Attached, each removable where the session takes detach: the attach menu's six samples, each kind's glyph read from its path. */
export const ChipsDetachable = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <AttachmentChips paths={sessionSamples.attachChoices.map((choice) => choice.path)} detach={() => {}} />
  </div>
)

/** Attached, on a session that takes no detach. */
export const ChipsFixed = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <AttachmentChips paths={['~/code/ingest/']} detach={undefined} />
  </div>
)

function Suggestions({ suggestions }: { readonly suggestions: Parameters<typeof SuggestionList>[0]['suggestions'] }) {
  return (
    <div className="ap" data-theme="dark" style={{ ...frame, height: 420, justifyContent: 'flex-end' }}>
      <div style={{ position: 'relative', height: 1 }}>
        <SuggestionList suggestions={suggestions} active={0} pickCommand={() => {}} pickFile={() => {}} />
      </div>
    </div>
  )
}

/** The commands `/de` names. */
export const SuggestCommands = () => (
  <Suggestions
    suggestions={{
      kind: 'commands',
      query: '/de',
      items: [
        { name: 'detach', args: '<name>', summary: 'Detach an attachment', play: false },
        { name: 'deny', args: '<tool|website>', summary: 'Refuse a tool or website for this session, without asking', play: false },
      ],
    }}
  />
)

/** A `/` that names none. */
export const SuggestNoCommand = () => <Suggestions suggestions={{ kind: 'commands', query: '/zz', items: [] }} />

/** The folder's files `@src` names. */
export const SuggestFiles = () => (
  <Suggestions suggestions={{ kind: 'files', folder: sessionSamples.listing.folder, items: sessionSamples.listing.files.filter((file) => file.path.includes('src')).slice(0, 7) }} />
)

/** An `@` that matches none. */
export const SuggestNoFile = () => <Suggestions suggestions={{ kind: 'files', folder: sessionSamples.listing.folder, items: [] }} />

/** The inspector's column, reserved until 1g draws the pane. */
export const Inspector = () => (
  <div className="ap" data-theme="dark" style={{ ...frame, height: 420, flexDirection: 'row', padding: 0 }}>
    <div style={{ flex: '1 1 0' }} />
    <InspectorPlaceholder />
  </div>
)

/** Each kind's glyph and tag: local, API billed per use, subscription. */
export const ModelKinds = () => (
  <div className="ap" data-theme="dark" style={frame}>
    {(['local', 'api', 'subscription'] as const).map((kind) => (
      <span key={kind} style={{ display: 'flex', alignItems: 'center', gap: 8 }}>
        <ModelGlyph kind={kind} />
        <ModelTag model={{ name: kind, kind, tag: kind === 'local' ? 'local · this Mac' : kind === 'api' ? 'API · billed per use' : 'subscription · claude-cli', where: '' }} />
      </span>
    ))}
  </div>
)
