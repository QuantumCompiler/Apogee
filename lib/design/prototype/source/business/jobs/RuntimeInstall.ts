import { JobProgress } from './JobProgress'
import type { JobUpdate } from './SupervisedJob'

/** How a runtime install stands. */
export type RuntimeInstallPhase = 'running' | 'installed' | 'stopped' | 'failed'

/**
 * A runtime install the person said yes to -- `train setup --with video`,
 * the command a media verb's refusal named -- as it runs and once it has
 * ended, folded from its job's updates by `JobProgress` and nothing else:
 * what a screen's install card draws of it. The Mac app's `RuntimeInstall`
 * (`BusinessJobs`, `RuntimeInstall.swift`; GUI 4f).
 *
 * `train setup` speaks prose, never the 52b stream: `installing the 'video'
 * set: torch>=2.6 …` while pip runs, `installed` at its end, and on a
 * failure stderr's `apogee train: pip install for the 'video' set failed
 * (exit code 1):` with pip's own last line under it.
 */
export class RuntimeInstall {
  readonly progress = new JobProgress()

  /** What it runs: `train setup --with video`. */
  constructor(readonly args: readonly string[]) {}

  apply(update: JobUpdate): void {
    this.progress.apply(update)
  }

  get phase(): RuntimeInstallPhase {
    switch (this.progress.status.case) {
      case 'running':
        return 'running'
      case 'done':
        return 'installed'
      case 'stopped':
        return 'stopped'
      case 'failed':
        return 'failed'
    }
  }

  /** The command as a person would type it: `apogee train setup --with video`. */
  get command(): string {
    return ['apogee', ...this.args].join(' ')
  }

  /** The set it installs, as the CLI names it: `video`. */
  get set(): string {
    const at = this.args.indexOf('--with')
    return at >= 0 ? (this.args[at + 1] ?? 'runtime') : 'runtime'
  }

  /** The card's headline, as the install stands. */
  get headline(): string {
    switch (this.phase) {
      case 'running':
        return `Installing the ${this.set} set`
      case 'installed':
        return `Installed the ${this.set} set`
      case 'stopped':
        return `The ${this.set} set's install was stopped before it finished`
      case 'failed':
        return `The ${this.set} set did not install`
    }
  }

  /** What it says now: the line it is on while it runs; its last line once installed or stopped; why, once it failed. */
  get detail(): string | undefined {
    switch (this.phase) {
      case 'failed':
        return this.failure
      case 'running':
        return this.progress.busy ?? this.progress.notices.at(-1)
      case 'installed':
      case 'stopped':
        return this.progress.notices.at(-1)
    }
  }

  /** Why it failed, in the CLI's words: stderr's own `apogee …` line, and the line under it last (pip's), when they differ; `undefined` unless it failed. */
  get failure(): string | undefined {
    const status = this.progress.status
    if (status.case !== 'failed') return undefined
    const lines = this.progress.stderr
      .split(/\r?\n/)
      .map((line) => line.trim())
      .filter((line) => line !== '')
    const own = lines.filter((line) => line.startsWith('apogee ')).at(-1)
    if (own === undefined) return status.message
    const tail = lines.at(-1) ?? own
    return own === tail ? own : `${own}\n${tail}`
  }
}
