// What `/apogee <words>` means (the roster is the item's recorded default:
// check, models, train status, promote, rollback): each word the real command
// it runs, read from the CLI's own arguments
// (lib/src/cli/source/presentation/cli/check.cpp, models.cpp, train.cpp). The
// two that change things are planned, never run here: they run only after
// the person's confirmation, and neither CLI command has a --yes to skip one.

/** The reads of the set. */
export type ReadName = 'check' | 'models' | 'train status'

/** The two commands that change the store, each confirmed before it runs. */
export type ActionName = 'promote' | 'rollback'

/** What the words ask for. */
export type Plan =
  | { kind: 'overview' }
  | { kind: 'pane' }
  | { kind: 'read'; read: ReadName; args: string[] }
  | { kind: 'action'; action: ActionName; args: string[] }
  | { kind: 'refused'; reason: string }

/** Each read's arguments: the command's own JSON document. */
export const READ_ARGS: Readonly<Record<ReadName, readonly string[]>> = {
  check: ['check', '--output-format', 'json'],
  models: ['models', 'list', '--output-format', 'json'],
  'train status': ['train', 'status', '--output-format', 'json'],
}

/** How each word of the set is typed, and what it does, in the set's order. */
export const USAGE: readonly { words: string; does: string }[] = [
  { words: 'check', does: 'the install check, every row with its remedy' },
  { words: 'models', does: 'every configured backend and stored model' },
  { words: 'train status', does: 'the runs, the pipeline, the cycle and each active version' },
  { words: 'promote <run> --as <backend>', does: "registers a run as the backend's next version; asks first" },
  { words: 'rollback <backend>', does: 'repoints the backend at its previous version; asks first' },
  { words: 'pane', does: 'opens the pane beside the chat: backends and models, training, the version ledger' },
]

/** The plan for the words typed after `/apogee`. */
export function planApogee(typed: string): Plan {
  const words = typed.trim().split(/\s+/).filter(word => word !== '')
  const said = words.join(' ')
  if (said === '' || said === 'help') {
    return { kind: 'overview' }
  }
  if (said === 'check') return read('check')
  if (said === 'models' || said === 'models list') return read('models')
  if (said === 'train status') return read('train status')
  if (said === 'pane') return { kind: 'pane' }
  if (words[0] === 'promote') {
    const [, run, as, backend, ...rest] = words
    if (as !== '--as' || rest.length > 0) {
      return { kind: 'refused', reason: PROMOTE_USAGE }
    }

    return promotePlan(run, backend)
  }
  if (words[0] === 'rollback') {
    const [, backend, ...rest] = words

    return rest.length > 0 ? { kind: 'refused', reason: ROLLBACK_USAGE } : rollbackPlan(backend)
  }

  return { kind: 'refused', reason: `'${said}' is not one of /apogee's commands` }
}

/** `train promote <run> --as <backend>`, planned: what the slash command and the pane's Promote both run. */
export function promotePlan(run: string | undefined, backend: string | undefined): Plan {
  if (run === undefined || backend === undefined || isWordless(run) || isWordless(backend)) {
    return { kind: 'refused', reason: PROMOTE_USAGE }
  }

  return { kind: 'action', action: 'promote', args: ['train', 'promote', run, '--as', backend] }
}

/** `train rollback <backend>`, planned: what the slash command and the pane's Roll back both run. */
export function rollbackPlan(backend: string | undefined): Plan {
  if (backend === undefined || isWordless(backend)) {
    return { kind: 'refused', reason: ROLLBACK_USAGE }
  }

  return { kind: 'action', action: 'rollback', args: ['train', 'rollback', backend] }
}

const PROMOTE_USAGE = 'promote takes a run and the backend: /apogee promote <run> --as <backend>'
const ROLLBACK_USAGE = 'rollback takes the backend: /apogee rollback <backend>'

/** A name that is no one word the CLI reads as a name: empty, holding a space, or a flag. */
function isWordless(word: string): boolean {
  return word === '' || /\s/.test(word) || isFlag(word)
}

/** The command as the person would type it in a shell: what runs, word for word. */
export function shownCommand(path: string, args: readonly string[]): string {
  return [path, ...args].map(quoted).join(' ')
}

function read(name: ReadName): Plan {
  return { kind: 'read', read: name, args: [...READ_ARGS[name]] }
}

/** A word the CLI would read as an option rather than the name it stands for. */
function isFlag(word: string): boolean {
  return word.startsWith('-')
}

/** A word quoted for a POSIX shell where it must be. */
function quoted(word: string): string {
  return /^[A-Za-z0-9_./:@%+=,-]+$/.test(word) ? word : `'${word.replaceAll("'", "'\\''")}'`
}
