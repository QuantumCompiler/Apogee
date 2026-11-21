/**
 * What `models delete <name>` says it would do, asked without `--yes`: the
 * CLI's own plan -- what goes, and every backend pointing into it -- read
 * before the person is asked (GUI 4e). The Mac app's `DeletePlan`. The app
 * never works out what points into a set itself.
 */
export interface DeletePlan {
  /** The plan as the CLI printed it, its closing "re-run with --yes" left out: the confirm's buttons are the yes. */
  readonly lines: readonly string[]
  /** The backends the plan says point into what goes. */
  readonly backends: readonly string[]
}

export const DeletePlan = {
  /** What a screen says while the plan is read. */
  reading: 'Reading what the delete removes',

  /** The plan `models delete <name>` printed without `--yes`. */
  of(said: string): DeletePlan {
    const lines = said.split('\n')
    const trim = () => {
      while (lines.length > 0 && (lines.at(-1) ?? '').trim() === '') lines.pop()
    }
    trim()
    if ((lines.at(-1) ?? '').startsWith('re-run with --yes')) lines.pop()
    trim()
    return { lines, backends: lines.flatMap((line) => DeletePlan.backend(line) ?? []) }
  },

  /** The backend a plan's line says points into what goes. */
  backend(line: string): string | undefined {
    const opening = "backend '"
    const end = line.indexOf("' points into this")
    return line.startsWith(opening) && end > opening.length ? line.slice(opening.length, end) : undefined
  },

  /** The question a confirm asks: the plan as printed, then the command the yes runs. */
  question(plan: DeletePlan, command: string): string {
    return [...plan.lines, '', `${command} -- this can't be undone.`].join('\n')
  },
}

/**
 * What a delete said beyond what it removed -- the plan's warnings, a kept
 * backend and its two doors, the backends removed with it, the roles it
 * cleared -- kept, every line as the CLI printed it, until the person
 * dismisses it (GUI 4e). The Mac app's `DeleteNarration`; the prototype's
 * "Deleted …" card.
 */
export interface DeleteNarration {
  readonly title: string
  readonly lines: readonly string[]
}

function printed(said: string): string[] {
  const lines = said.split('\n')
  while (lines.length > 0 && (lines.at(-1) ?? '').trim() === '') lines.pop()
  return lines
}

export const DeleteNarration = {
  /** `said`, unless it says no more than the folders it removed (`will remove:`, each folder, `removed.`). */
  said(title: string, said: string): DeleteNarration | undefined {
    const lines = printed(said)
    let listing = false
    const more = lines.some((line) => {
      if (line === 'will remove:') {
        listing = true
        return false
      }
      if (listing && line.startsWith('  ')) return false
      listing = false
      return line.trim() !== '' && line !== 'removed.'
    })
    return more ? { title, lines } : undefined
  },

  /** `said`, where it says a role the CLI moved on the person's behalf (the CLI's M33): a default set, a role cleared. */
  movingRoles(title: string, said: string): DeleteNarration | undefined {
    const lines = printed(said)
    const moved = lines.some((line) => /^models\.\S+ (set to '.*' \(none was set\)|cleared: it named '.*')$/.test(line.trim()))
    return moved ? { title, lines } : undefined
  },
}
