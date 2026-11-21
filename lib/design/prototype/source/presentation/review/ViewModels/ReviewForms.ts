import type { ReviewTarget } from '../../../business/reads/Review'
import { reviewStartingTarget } from './ReviewWords'

/** `review mr <ref>`, as a person types it: the Mac app's `ReviewViewModel.RunForm` (`ReviewViewModel.swift:35-55`). */
export interface RunForm {
  readonly ref: string
  /** `--skills a,b`; empty lets the plan decide by the change's files. */
  readonly skills: readonly string[]
  /** `-m`; empty is the chat role. */
  readonly model: string
}

const sortedLenses = (lenses: readonly string[]) => [...lenses].sort()

export const RunForm = {
  empty: { ref: '', skills: [], model: '' } as RunForm,

  /** The run's argv: `--dry-run`, or `--yes` on a consented post; `undefined` with no merge request. */
  arguments(form: RunForm, posting: boolean): string[] | undefined {
    const ref = form.ref.trim()
    if (ref === '') return undefined
    const args = ['review', 'mr', ref]
    if (form.skills.length > 0) args.push('--skills', sortedLenses(form.skills).join(','))
    const model = form.model.trim()
    if (model !== '') args.push('-m', model)
    args.push(posting ? '--yes' : '--dry-run')
    return [...args, '--output-format', 'json']
  },

  /** The command as the tab draws it under its form; `''` with no merge request. */
  command(form: RunForm, posting: boolean): string {
    const args = RunForm.arguments(form, posting)
    return args === undefined ? '' : `apogee ${args.join(' ')}`
  },

  /** The lenses line: those pinned, or -- none pinned -- the plan's choice by each change's files (the Mac's `lensesLabel`). */
  lensesLabel(form: RunForm): string {
    return lensesLabel(form.skills)
  },

  modelLabel(form: RunForm): string {
    return `Model: ${form.model.trim() === '' ? 'the chat role' : form.model.trim()}`
  },
}

/** A lenses menu's title: the lenses pinned, sorted, or -- none pinned -- the plan's choice by each change's files (`ReviewViewModel.swift:241-243`). */
export function lensesLabel(lenses: readonly string[]): string {
  return lenses.length === 0 ? "Lenses: as each change's files say" : `Lenses: ${sortedLenses(lenses).join(', ')}`
}

/**
 * `review targets add <name> --repo|--owner …`: the Mac app's `TargetForm`
 * (`ReviewViewModel.swift:58-89`), with the old sheet's fields -- the
 * repository and the owner kept apart, so switching Watch keeps each as
 * typed, and the lenses menu open or shut.
 */
export interface TargetForm {
  readonly name: string
  readonly watch: 'repo' | 'owner'
  readonly repo: string
  readonly owner: string
  /** `github` (the CLI's default) or `gitlab`. */
  readonly forge: string
  readonly host: string
  readonly lenses: readonly string[]
  readonly lensOpen: boolean
  /** `--post`: the target's own grant, asked for by name. */
  readonly post: boolean
  readonly drafts: boolean
  readonly model: string
}

export const TargetForm = {
  /** The sheet as the old prototype opens it (`rv.addTarget`): `gadget`, watching the repository `acme/gadget`. */
  starting(): TargetForm {
    return { name: reviewStartingTarget.name, watch: 'repo', repo: reviewStartingTarget.repo, owner: '', forge: 'github', host: '', lenses: [], lensOpen: false, post: false, drafts: false, model: '' }
  },

  /** The repository or the owner, whichever it watches. */
  place(form: TargetForm): string {
    return (form.watch === 'repo' ? form.repo : form.owner).trim()
  },

  /** Add is on once it has a name and a repository or an owner. */
  canAdd(form: TargetForm): boolean {
    return form.name.trim() !== '' && TargetForm.place(form) !== ''
  },

  /** The argv Add runs, the lenses sorted; `undefined` until it can be added. */
  arguments(form: TargetForm): string[] | undefined {
    if (!TargetForm.canAdd(form)) return undefined
    return TargetForm.words(form, form.name.trim(), TargetForm.place(form))
  },

  /** The command the sheet draws, live from its fields: what is missing named in angle brackets, as the old sheet draws it. */
  command(form: TargetForm): string {
    const name = form.name.trim() === '' ? '<name>' : form.name.trim()
    const place = TargetForm.place(form) === '' ? (form.watch === 'repo' ? '<owner/repo>' : '<owner>') : TargetForm.place(form)
    return `apogee ${TargetForm.words(form, name, place).join(' ')}`
  },

  words(form: TargetForm, name: string, place: string): string[] {
    const args = ['review', 'targets', 'add', name, form.watch === 'owner' ? '--owner' : '--repo', place]
    if (form.forge !== 'github') args.push('--forge', form.forge)
    const host = form.host.trim()
    if (host !== '') args.push('--host', host)
    if (form.lenses.length > 0) args.push('--skills', sortedLenses(form.lenses).join(','))
    if (form.post) args.push('--post')
    if (form.drafts) args.push('--drafts')
    const model = form.model.trim()
    if (model !== '') args.push('-m', model)
    return args
  },

  lensesLabel(form: TargetForm): string {
    return lensesLabel(form.lenses)
  },

  /** The target the CLI said it added, as `review targets` would list it: kept beside the read, since the stand-in keeps no state. */
  target(form: TargetForm): ReviewTarget {
    const host = form.host.trim()
    const model = form.model.trim()
    return {
      name: form.name.trim(),
      forge: form.forge,
      repo: form.watch === 'repo' ? TargetForm.place(form) : undefined,
      owner: form.watch === 'owner' ? TargetForm.place(form) : undefined,
      host: host === '' ? undefined : host,
      skills: sortedLenses(form.lenses),
      post: form.post,
      drafts: form.drafts,
      model: model === '' ? undefined : model,
      cli: undefined,
    }
  },
}
