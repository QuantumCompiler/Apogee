// Drives the pane as a surface and the person do: the documents its reads
// answer, the pane mounted on a surface, its words read back, its buttons
// pressed. Every test of the pane loops over the surfaces (reference.md: a
// mod writes one tree), so it never depends on one.
import type { Engine, Mounted } from 'claude-code/testing'

import { scripted } from './apogee'
import type { Answer, Machine, Script } from './machine'

/** The surfaces every pane test draws on. */
export const SURFACES = ['terminal', 'desktop'] as const

/** The four reads of the pane, by their words. */
export const MODELS_LIST = 'models list --output-format json'
export const MODELS_STATUS = 'models status --output-format json'
export const TRAIN_STATUS = 'train status --output-format json'
export const TRAIN_VERSIONS = 'train versions --output-format json'

/** The pane's documents as the binary answers them, each by its words; one left out is unscripted. */
export function paneReads(answers: { models?: Answer; roles?: Answer; training?: Answer; ledger?: Answer }): Script {
  return scripted({
    ...(answers.models === undefined ? {} : { [MODELS_LIST]: answers.models }),
    ...(answers.roles === undefined ? {} : { [MODELS_STATUS]: answers.roles }),
    ...(answers.training === undefined ? {} : { [TRAIN_STATUS]: answers.training }),
    ...(answers.ledger === undefined ? {} : { [TRAIN_VERSIONS]: answers.ledger }),
  })
}

/** How often each of `words` was run, in all. */
export function ranCount(made: Machine, words: string): number {
  return made.runs.filter(run => run.argv.slice(1).join(' ') === words).length
}

/** A drawing of the pane on one of the surfaces. */
export type MountedPane = Mounted<(typeof SURFACES)[number], 'Pane'>

/** The pane as a surface mounts it: docked (or `placement`), 80 columns. */
export async function mountPane($: Engine, surface: (typeof SURFACES)[number], placement: 'dock' | 'inline' | 'widget' = 'dock'): Promise<MountedPane> {
  return $.ui.mount({
    plugin: 'apogee',
    surface,
    component: 'Pane',
    requestId: 'apogee',
    props: { title: 'Apogee', isFocused: false, bodyColumns: 80, placement, scroll: { offset: 0, bodyRows: 60 }, view: {} },
  })
}

/** Every text the pane draws, in order: its words as the person reads them. */
export async function lines(ui: MountedPane): Promise<string[]> {
  return (await ui.findAll({ type: 'Text' })).map(text => text.text)
}

/** Every button the pane draws, by its key and label. */
export async function buttons(ui: MountedPane): Promise<{ key: string | undefined; label: unknown }[]> {
  return (await ui.findAll({ type: 'Button' })).map(button => ({ key: button.key, label: button.props.label }))
}
