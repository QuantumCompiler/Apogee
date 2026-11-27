import statusCloud from '@recorded/business/reads/Fixtures/suites-models-status-cloud.json?raw'
import statusFast from '@recorded/business/reads/Fixtures/suites-models-status-fast.json?raw'
import statusPair from '@recorded/business/reads/Fixtures/suites-models-status-pair.json?raw'
import statusShared from '@recorded/business/reads/Fixtures/suites-models-status-shared.json?raw'
import statusVast from '@recorded/business/reads/Fixtures/suites-models-status-vast.json?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import executePlay from '../../../../fixtures/data/protocol/execute-play-summarize-verify.jsonl?raw'
import { SuiteFootprint } from '../../../business/reads/SuiteFootprint'
import { ReadDocument } from '../../../data/protocol/ReadDocument'
import type { SuiteChoice } from '../ViewModels/SessionActions'
import type { SessionViewModel } from '../ViewModels/SessionViewModel'
import { SuiteSheet } from '../Views/SuiteSheet'
import { useSessionPreview } from './SessionPreview'

/**
 * The suite chooser on its own (design 1u): the recorded suites, each with
 * the CLI's verdict (`models status --suite <name>`), the default `fast`
 * (`suites-default-suite.txt`). The sheet's frame is the shell's.
 */
const frame = { width: 560, padding: '10px 20px', boxSizing: 'border-box', display: 'flex', flexDirection: 'column', gap: 12, fontSize: 'var(--t-small)', background: 'var(--surface-card)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', lineHeight: 1.5 } as const

const footprint = (text: string) => SuiteFootprint.of(ReadDocument.from(text) as ReadDocument)
const choices: readonly SuiteChoice[] = [
  { name: 'cloud', footprint: footprint(statusCloud), isDefault: false },
  { name: 'fast', footprint: footprint(statusFast), isDefault: true },
  { name: 'pair', footprint: footprint(statusPair), isDefault: false },
  { name: 'shared', footprint: footprint(statusShared), isDefault: false },
  { name: 'vast', footprint: footprint(statusVast), isDefault: false },
]

function Sheet({ model }: { readonly model: SessionViewModel }) {
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <SuiteSheet model={model} />
    </div>
  )
}

/** `chat:suite`'s: open on vast, which the CLI prices over this machine's memory, its total in the CLI's words, not forced. */
export const OverBudget = () => <Sheet model={useSessionPreview(newUnsent, { ticks: 1, then: (model) => model.presentSuites(choices, 'fast', 'vast') })} />

/** The same, forced: Switch on. */
export const Forced = () => <Sheet model={useSessionPreview(newUnsent, { ticks: 1, then: (model) => model.presentSuites(choices, 'fast', 'vast', true) })} />

/** A suite that fits, chosen. */
export const Fits = () => <Sheet model={useSessionPreview(newUnsent, { ticks: 1, then: (model) => model.presentSuites(choices, 'fast', 'shared') })} />

/** An execute session: named suites only, the one it runs under marked now. */
export const Execute = () => (
  <Sheet model={useSessionPreview(executePlay, { launch: { execute: { suite: 'coding' } }, ticks: 1, then: (model) => model.presentSuites(choices, 'fast') })} />
)

/** No suites configured: how to make one. */
export const NoSuites = () => <Sheet model={useSessionPreview(newUnsent, { ticks: 1, then: (model) => model.presentSuites([], '') })} />
