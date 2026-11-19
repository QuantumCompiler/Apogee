import { chord, symbols } from '../../shell/ViewModels/KeyMap'
import type { KeyChord } from '../../shell/ViewModels/KeyMap'

/** What the Sessions place does at a key, wherever the focus is: the Mac app's `SessionCommands`. */
export type SessionCommand = 'newSession' | 'newPrivateSession' | 'newExecuteSession' | 'toggleInspector' | 'search'

/**
 * The Sessions place's keys (design 1g), as the design's keyboard list names
 * them (the shell's `KeyMap.reference`): ⌘N, ⇧⌘N, ⌥⌘N, ⌥⌘I and ⇧⌘F. The
 * shell hands the place every key it does not own itself.
 */
export const SessionKeys = {
  bindings: [
    { command: 'newSession', chord: chord('n', 'command') },
    { command: 'newPrivateSession', chord: chord('n', 'shift', 'command') },
    { command: 'newExecuteSession', chord: chord('n', 'option', 'command') },
    { command: 'toggleInspector', chord: chord('i', 'option', 'command') },
    { command: 'search', chord: chord('f', 'shift', 'command') },
  ] as ReadonlyArray<{ readonly command: SessionCommand; readonly chord: KeyChord }>,

  /** The command `keys` runs, if the place owns it. */
  commandFor(keys: KeyChord): SessionCommand | undefined {
    const wanted = symbols(keys)
    return SessionKeys.bindings.find((binding) => symbols(binding.chord) === wanted)?.command
  },
}
