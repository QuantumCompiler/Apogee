// The pane's words (1c's layout, decided at build): its heading and Read
// again, the confirmed commands running, then three sections -- backends and
// their models, training, the version ledger -- each from its documents, every
// value from them and nothing invented. A document not read yet, one that
// failed, a field it lacked, and an empty one are each said in plain words.
// Pure: Presentation draws these lines (pane.tsx), Business decides what each
// offers.
import type { ApogeeModelsList, ApogeeModelsStatus, ApogeePaneRead, ApogeePaneState, ApogeeRunSummary, ApogeeTrainStatus, ApogeeTrainVersions, ApogeeVersionEntry } from '../../../types'
import type { FoundBinary } from '../../business/client/client'
import { canRollBack, isConfigured, isPromotable, promoteTargets } from '../../business/pane/pane'
import type { PaneShown } from '../../business/pane/pane'
import { count, trainingLines } from '../commands/render'
import { ageText } from '../status/line'

/** What a button of the pane does when pressed. */
export type PaneAct =
  | { kind: 'reread' }
  | { kind: 'pick'; run: string }
  | { kind: 'unpick' }
  | { kind: 'promote'; run: string; backend: string }
  | { kind: 'rollback'; backend: string }

/** A button, by its key (unique in the pane) and label, and what it does. */
export type PaneButton = { key: string; label: string; act: PaneAct }

/** One line of the pane: its words, how they are set, how deep, and the buttons after them. */
export type PaneLine = { text: string; tone?: 'heading' | 'subheading' | 'dim'; indent?: number; buttons?: PaneButton[] }

/** The words before the session has looked for the binary. */
export const NOT_LOOKED_TEXT = 'Apogee has not been looked for yet: the session has not started.'

/** The pane, line by line, over the binary found. */
export function paneLines(binary: FoundBinary, { state, actions, at }: PaneShown): PaneLine[] {
  const lines: PaneLine[] = [{ text: `Apogee ${binary.version} · ${channelWords(binary)}`, tone: 'heading', buttons: [{ key: 'reread', label: 'Read again', act: { kind: 'reread' } }] }]
  for (const job of actions) {
    lines.push({ text: `Running ${job.id}: ${job.title}`, tone: 'dim' })
  }
  lines.push({ text: 'Backends and models', tone: 'heading' })
  lines.push(...documentLines(state.roles, 'models status', at, rolesLines))
  lines.push(...documentLines(state.models, 'models list', at, modelsLines))
  lines.push({ text: 'Training', tone: 'heading' })
  lines.push(...documentLines(state.training, 'train status', at, training => trainLines(training, state)))
  lines.push({ text: 'Version ledger', tone: 'heading' })
  lines.push(...documentLines(state.ledger, 'train versions', at, ledgerLines))

  return lines
}

/** The pane as a widget (the desktop app's small form): what runs and each active version, at a glance. */
export function glanceText({ state, at }: PaneShown): string {
  const running = glancePart(state.training, 'training', at, training => (training.running.length === 0 ? 'nothing running' : `${count(training.running.length, 'run')} training`))
  const versions = glancePart(state.ledger, 'ledger', at, ledger =>
    ledger.ledgers.length === 0
      ? 'no version promoted'
      : ledger.ledgers.map(one => `${one.backend} ${one.activeVersion === undefined ? '(no active version said)' : `v${one.activeVersion}`}`).join(', '),
  )

  return `Apogee: ${running} · ${versions}`
}

/** One document at a glance: not read yet, failed, or its words, with their age once a later read failed. */
function glancePart<T>(read: ApogeePaneRead<T> | undefined, what: string, at: number, said: (value: T) => string): string {
  if (read?.value === undefined) return read?.failure === undefined ? `${what} unread` : `${what} read failed`
  if (read.failure === undefined) return said(read.value)

  return `${said(read.value)} (read ${ageText(at - (read.readAt ?? at))} ago; the latest read failed)`
}

function channelWords(binary: FoundBinary): string {
  return binary.channel === undefined ? 'its channel not said' : `${binary.channel} channel`
}

/**
 * One document's lines: reading it, why it gave none, or its body -- after
 * the failure of a later read, with the age of what is shown -- and the
 * fields it lacked.
 */
function documentLines<T>(read: ApogeePaneRead<T> | undefined, words: string, at: number, body: (value: T) => PaneLine[]): PaneLine[] {
  if (read?.value === undefined) {
    return [read?.failure === undefined ? { text: `Reading ${words}…`, tone: 'dim' } : { text: read.failure }]
  }
  const lines: PaneLine[] = []
  if (read.failure !== undefined) {
    lines.push({ text: `Shown as read ${ageText(at - (read.readAt ?? at))} ago; the latest read failed: ${read.failure}`, tone: 'dim' })
  }
  lines.push(...body(read.value))
  if (read.missing.length > 0) {
    lines.push({ text: `${words} lacked: ${read.missing.join(', ')}`, tone: 'dim' })
  }

  return lines
}

function rolesLines(roles: ApogeeModelsStatus): PaneLine[] {
  if (roles.fields.length === 0) return []

  return [{ text: 'Roles', tone: 'subheading' }, ...roles.fields.map(role => ({ text: `${role.field}: ${role.value.trim().replace(/\s+/g, ' ')}`, indent: 1 }))]
}

function modelsLines(list: ApogeeModelsList): PaneLine[] {
  if (list.rows.length === 0) {
    return [{ text: 'No backends configured and no models stored.' }]
  }
  const lines: PaneLine[] = list.rows.some(row => isConfigured(row.backend)) ? [] : [{ text: 'No backend is configured.' }]
  const sections = new Map<string, PaneLine[]>()
  for (const row of list.rows) {
    const title = row.section === 'local' ? 'Local' : row.section === 'cloud' ? 'Cloud' : 'Other'
    const rows = sections.get(title) ?? []
    const type = row.kind === undefined ? row.backendType : `${row.backendType} (${row.kind})`
    rows.push({ text: [row.backend, type, row.model === '' ? '-' : row.model, row.state].join(' · '), indent: 1 })
    if (row.note !== undefined) {
      rows.push({ text: row.note, tone: 'dim', indent: 2 })
    }
    sections.set(title, rows)
  }
  for (const title of ['Local', 'Cloud', 'Other']) {
    const rows = sections.get(title)
    if (rows !== undefined) lines.push({ text: title, tone: 'subheading' }, ...rows)
  }

  return lines
}

function trainLines(training: ApogeeTrainStatus, state: ApogeePaneState): PaneLine[] {
  if (
    training.running.length === 0 &&
    training.recentRuns.length === 0 &&
    training.latestPipeline === undefined &&
    training.cycle === undefined &&
    training.cycleError === undefined &&
    training.staleLock === undefined
  ) {
    return [{ text: 'Nothing trained yet: no runs, no pipeline, no cycle.' }]
  }
  const lines: PaneLine[] = trainingLines(training).map(text => ({ text, indent: 1 }))
  if (training.recentRuns.length === 0) {
    lines.push({ text: 'Recent runs: none', indent: 1 })

    return lines
  }
  lines.push({ text: 'Recent runs', tone: 'subheading' })
  for (const run of training.recentRuns) {
    lines.push({ text: runText(run), indent: 1, buttons: isPromotable(run) ? [{ key: `promote:${run.id}`, label: 'Promote', act: { kind: 'pick', run: run.id } }] : [] })
    if (state.promoting === run.id) {
      lines.push(...pickLines(run.id, state.models))
    }
  }

  return lines
}

function runText(run: ApogeeRunSummary): string {
  const parts = [run.id, run.status]
  if (run.method !== undefined) parts.push(run.method)
  if (run.startedAt !== undefined) parts.push(`started ${run.startedAt}`)
  if (run.evalPassed !== undefined) parts.push(`gate ${run.evalPassed ? 'passed' : 'failed'}${run.evalScore === undefined ? '' : ` (${run.evalScore.toFixed(2)})`}`)
  else if (run.evalScore !== undefined) parts.push(`eval ${run.evalScore.toFixed(2)}`)

  return parts.join(' · ')
}

/**
 * The pick of a backend to promote `run` into: each one `models list` offers,
 * and Cancel. With no document read yet, or none at all, that is said, never
 * that the document names no backend; a good read kept beside a later
 * failure still offers its backends (the section above says its age).
 */
function pickLines(run: string, models: ApogeePaneRead<ApogeeModelsList> | undefined): PaneLine[] {
  const cancel: PaneButton = { key: 'promote-cancel', label: 'Cancel', act: { kind: 'unpick' } }
  const targets = models?.value === undefined ? undefined : promoteTargets(models.value)
  const pick: PaneLine =
    targets === undefined
      ? {
          text: `Promote ${run}: ${models?.failure === undefined ? 'models list is still being read, so no backend can be picked yet' : 'models list gave no document, so no backend can be picked'}.`,
          indent: 2,
          buttons: [cancel],
        }
      : targets.length === 0
        ? { text: `Promote ${run}: models list names no configured llamacpp or mlx backend to promote into.`, indent: 2, buttons: [cancel] }
        : { text: `Promote ${run} as:`, indent: 2, buttons: [...targets.map((backend): PaneButton => ({ key: `promote-as:${backend}`, label: backend, act: { kind: 'promote', run, backend } })), cancel] }

  return [pick, { text: `Or into a backend not listed: /apogee promote ${run} --as <backend>`, tone: 'dim', indent: 2 }]
}

function ledgerLines(versions: ApogeeTrainVersions): PaneLine[] {
  if (versions.ledgers.length === 0) {
    return [{ text: 'No version promoted yet.' }]
  }
  const lines: PaneLine[] = []
  for (const ledger of versions.ledgers) {
    const active = ledger.activeVersion
    lines.push({
      text: `${ledger.backend} · ${active === undefined ? 'its active version not said' : `active v${active}`}`,
      tone: 'subheading',
      buttons: canRollBack(ledger) ? [{ key: `rollback:${ledger.backend}`, label: 'Roll back', act: { kind: 'rollback', backend: ledger.backend } }] : [],
    })
    for (const version of ledger.versions) {
      lines.push({ text: versionText(version, active), indent: 1 })
      const detail = detailText(version)
      if (detail !== '') lines.push({ text: detail, tone: 'dim', indent: 2 })
    }
  }

  return lines
}

function versionText(version: ApogeeVersionEntry, active: number | undefined): string {
  const name = `v${version.version ?? '?'}${version.version !== undefined && version.version === active ? ' (active)' : ''}`
  const gate = [
    version.evalScore === undefined ? undefined : version.evalScore.toFixed(2),
    version.evalPassed === undefined ? undefined : version.evalPassed ? 'passed' : 'failed',
    version.evalScoredOn === undefined ? undefined : `scored on ${version.evalScoredOn}`,
  ].filter(part => part !== undefined)

  return [name, `run ${version.runId}`, `promoted ${version.promotedAt}`, ...(gate.length === 0 ? [] : [`eval ${gate.join(', ')}`])].join(' · ')
}

function detailText(version: ApogeeVersionEntry): string {
  return [
    version.weights === undefined ? undefined : `${version.weights.kind} ${version.weights.path}`,
    version.baseSha256 === undefined ? undefined : `base sha256 ${version.baseSha256}`,
    version.prunedAt === undefined ? undefined : `pruned ${version.prunedAt}`,
    version.fusedPath === undefined ? undefined : `fused ${version.fusedPath}`,
  ]
    .filter(part => part !== undefined)
    .join(' · ')
}
