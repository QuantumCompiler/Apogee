import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import privateUnsent from '@recorded/data/protocol/Fixtures/private-unsent.jsonl?raw'
import type { SessionViewModel } from '../ViewModels/SessionViewModel'
import { InputBar } from '../Views/InputBar'
import { useSessionPreview } from './SessionPreview'

/** The composer on its own (design 1u), one preview per state a person sees differently. */
const frame = { position: 'relative', width: 920, paddingTop: 360, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Bar({ model }: { readonly model: SessionViewModel }) {
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <InputBar model={model} />
    </div>
  )
}

/** Ready, empty: the model it messages, Send off. */
export const Ready = () => <Bar model={useSessionPreview(newUnsent, { ticks: 2 })} />

/** A message typed: Send on. */
export const Typed = () => <Bar model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.setDraft('Where do fetches start in this repo?') })} />

/** Private: the input says so. */
export const Private = () => <Bar model={useSessionPreview(privateUnsent, { launch: { privately: true }, ticks: 3 })} />

/** A turn running: its status, and Stop in Send's place. */
export const Running = () => <Bar model={useSessionPreview(markdownAnswer, { sends: ['How do I keep the batch going?'], ticks: 30 })} />

/** Attached: a chip a path, each removable. */
export const WithChips = () => <Bar model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.attach('~/code/ingest/') })} />

/** The `/` menu over it. */
export const Commands = () => <Bar model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.setDraft('/de') })} />

/** A `/` that names no command. */
export const NoCommand = () => <Bar model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.setDraft('/zz') })} />

/** The `@` menu over it. */
export const Files = () => <Bar model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.setDraft('@runner') })} />

/** An `@` that matches no file. */
export const NoFile = () => <Bar model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.setDraft('@zzz') })} />

/** The attach menu over it. */
export const Attach = () => <Bar model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.toggleMenu('attach') })} />

/** The session ended at launch: the input off, its words in the error color. */
export const Ended = () => <Bar model={useSessionPreview(newUnsent, { apogee: 'config-typo', ticks: 2 })} />

/** No binary found: the input off. */
export const Missing = () => <Bar model={useSessionPreview(newUnsent, { apogee: 'missing' })} />
