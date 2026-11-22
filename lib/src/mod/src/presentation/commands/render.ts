// The `/apogee` set's words: each document formatted as the person reads it
// (the item's recorded default, "the command's own document, formatted"),
// every value from the document and nothing invented; a field the document
// lacked is said under it.
import type { ApogeeToolsState } from '../../../types'
import type { CheckReport } from '../../data/documents/check'
import type { ModelsList } from '../../data/documents/models'
import type { TrainStatus } from '../../data/documents/training'
import type { FoundBinary, Read } from '../../business/client/client'
import { USAGE } from '../../business/commands/plan'
import type { ReadDone } from '../../business/commands/reads'

/** The set, one line a word, as `/apogee` with nothing after it lists it. */
export function usageText(): string {
  const width = Math.max(...USAGE.map(one => one.words.length)) + 8

  return USAGE.map(one => `/apogee ${one.words}`.padEnd(width) + one.does).join('\n')
}

/** Which binary the mod drives, where, on which channel, and the places passed over. */
export function binaryText(binary: FoundBinary): string {
  const channel = binary.channel === undefined ? 'its channel not said' : `${binary.channel} channel`
  const lines = [`Apogee ${binary.version} at ${binary.path} (${channel})`]
  if (binary.skipped.length > 0) {
    lines.push(`Passed over: ${binary.skipped.map(place => `${place.path} (${place.reason})`).join(', ')}`)
  }

  return lines.join('\n')
}

/** What Claude was given: the served tools and apogee_complete, or why not. */
export function toolsText(tools: ApogeeToolsState): string {
  const served =
    tools.served.length === 0 && tools.servedRefusal !== undefined
      ? `Apogee's served tools: none registered: ${tools.servedRefusal}`
      : `Apogee's served tools: ${tools.served.length} registered (mcp__apogee__<name>, each call one run of apogee mcp serve)${tools.servedRefusal === undefined ? '' : `; ${tools.servedRefusal}`}`
  const complete =
    tools.complete === undefined
      ? `apogee_complete: not offered: ${tools.completeRefusal ?? 'not looked at yet'}`
      : `apogee_complete: on ${tools.complete.backends.map(one => `${one.name} (${one.isBoundUnsaid === true ? 'its own bound, ' : ''}at most ${one.maxTokens} tokens)`).join(', ')}, by ${tools.complete.plumbing === 'served' ? "Apogee's served complete" : 'the one-shot apogee complete'}; local and unmetered`

  return `${served}\n${complete}`
}

/** A read of the set, done: its document formatted, or the command and why it gave none. */
export function readText(done: ReadDone, shown: string): string {
  if (done.document.kind === 'failed') {
    return failureText(shown, done.document)
  }
  const body = done.read === 'check' ? checkText(done.document.value) : done.read === 'models' ? modelsText(done.document.value) : trainText(done.document.value)

  return withMissing(body, done.document.missing)
}

/** Why a command gave no document: it did not run, or answered none. */
export function failureText(shown: string, failed: Extract<Read<unknown>, { kind: 'failed' }>): string {
  return failed.stage === 'run' ? `${shown} could not run: ${failed.reason}` : `${shown} answered no document: ${failed.reason}`
}

/** `text`, then the fields the document lacked, when it lacked any. */
export function withMissing(text: string, missing: readonly string[]): string {
  return missing.length === 0 ? text : `${text}\n\nThe document lacked: ${missing.join(', ')}`
}

function checkText(report: CheckReport): string {
  const failures = report.failures ?? report.rows.filter(row => row.status === 'fail').length
  const warnings = report.warnings ?? report.rows.filter(row => row.status === 'warn').length
  const sections = new Map<string, string[]>()
  for (const row of report.rows) {
    const lines = sections.get(row.section) ?? []
    lines.push(`  ${mark(row.status).padEnd(4)}  ${row.name}${row.detail === '' ? '' : `: ${row.detail}`}`)
    if (row.remedy !== undefined) {
      lines.push(`        fix: ${row.remedy}`)
    }
    sections.set(row.section, lines)
  }
  const blocks = [...sections].map(([section, lines]) => [section === '' ? '(no section)' : section, ...lines].join('\n'))

  return [`apogee check: ${count(failures, 'failure')}, ${count(warnings, 'warning')}`, ...blocks].join('\n\n')
}

function mark(status: string): string {
  if (status === 'fail') return 'FAIL'
  if (status === 'skipped') return 'skip'

  return status === '' ? '?' : status
}

function modelsText(list: ModelsList): string {
  const header = `apogee models list: ${count(list.rows.length, 'row')}`
  if (list.rows.length === 0) {
    return `${header}\n\nNo backends configured and no models stored.`
  }
  const cells = list.rows.map(row => [row.backend, row.backendType, row.model === '' ? '-' : row.model, row.state])
  const widths = [0, 1, 2].map(column => Math.max(...cells.map(cell => (cell[column] ?? '').length)))
  const line = (cell: readonly string[]): string =>
    `  ${cell
      .slice(0, 3)
      .map((one, column) => one.padEnd(widths[column] ?? 0))
      .join('  ')}  ${cell[3] ?? ''}`
  const sections = new Map<string, string[]>()
  list.rows.forEach((row, index) => {
    const title = row.section === 'local' ? 'Local' : row.section === 'cloud' ? 'Cloud' : 'Other'
    const lines = sections.get(title) ?? []
    lines.push(line(cells[index] ?? []))
    if (row.note !== undefined) {
      lines.push(`    note: ${row.note}`)
    }
    sections.set(title, lines)
  })
  const order = ['Local', 'Cloud', 'Other'].filter(title => sections.has(title))

  return [header, ...order.map(title => [title, ...(sections.get(title) ?? [])].join('\n'))].join('\n\n')
}

/**
 * The training status's headline lines, as `/apogee train status` and the
 * pane both say them: what runs, the newest pipeline, the cycle (each only
 * where the document has it). A history that could not be read is said in
 * the CLI's words, never drawn as none; a lock a dead pass left is said once,
 * as the cycle's state or on its own line, as `train status` says it
 * (train.cpp), less the process id the document does not carry.
 */
export function trainingLines(status: TrainStatus): string[] {
  const lines = [status.running.length === 0 ? 'Running: none' : `Running: ${count(status.running.length, 'run')} (${status.running.join(', ')})`]
  const pipeline = status.latestPipeline
  if (pipeline !== undefined) {
    const of = pipeline.stages === undefined ? '' : `, ${pipeline.passed ?? 0} of ${pipeline.stages} stages passed`
    lines.push(`Pipeline: ${pipeline.id} ${pipeline.status ?? '(status not said)'} (${pipeline.specName ?? 'unnamed'}${of})`)
  }
  const lock = status.staleLock === undefined ? undefined : `stopped -- cycle.lock is left; remove ${status.staleLock} to run again`
  let isLockSaid = false
  if (status.cycle !== undefined) {
    const state =
      status.cycle.isHalted === true
        ? `halted${status.cycle.haltReason ? `: ${status.cycle.haltReason}` : ''}`
        : status.isCycleActive === true
          ? 'a pass running'
          : (lock ?? 'at rest')
    isLockSaid = state === lock
    lines.push(`Cycle: ${status.cycle.backend ?? '(backend not said)'}, ${state}`)
  } else if (status.cycleError !== undefined) {
    lines.push(`Cycle: history unreadable -- ${status.cycleError}`)
  } else if (lock !== undefined) {
    lines.push(`Cycle: ${lock}`)
    isLockSaid = true
  }
  if (lock !== undefined && !isLockSaid) {
    lines.push(`Cycle lock: ${lock}`)
  }

  return lines
}

function trainText(status: TrainStatus): string {
  const lines = ['apogee train status', ...trainingLines(status)]
  lines.push(
    status.versions.length === 0
      ? 'Versions: none promoted'
      : `Versions: ${status.versions
          .map(version => `${version.backend} ${version.activeVersion === undefined ? '(active version not said)' : `v${version.activeVersion}`}${version.kept !== undefined && version.total !== undefined ? ` (${version.kept} kept of ${version.total})` : ''}`)
          .join(', ')}`,
  )
  if (status.recentRuns.length > 0) {
    lines.push('Recent runs:')
    for (const run of status.recentRuns) {
      lines.push(`  ${[run.id, run.status, run.method ?? '-', run.startedAt ?? '-'].join('  ')}`)
    }
  }

  return lines.join('\n')
}

/** `n` and its noun, plural past one: `1 run`, `3 runs`. */
export function count(n: number, noun: string): string {
  return `${n} ${noun}${n === 1 ? '' : 's'}`
}
