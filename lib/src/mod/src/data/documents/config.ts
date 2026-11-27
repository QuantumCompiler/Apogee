// `apogee config get <key>`: one config value, the bare line the CLI prints
// (lib/src/cli/source/presentation/cli/config_cmd.cpp, bind_get: the value
// and a newline, an unset value an empty line). No JSON document holds a
// single value, so that line is the value, read as the macOS app reads it.
import type { Decoded } from './document'

/** A whole-number value as `config get` prints it: the number, or undefined for one unset. */
export function decodeWholeValue(text: string): Decoded<number | undefined> {
  const trimmed = text.trim()
  if (trimmed === '') {
    return { kind: 'read', value: undefined, missing: [] }
  }
  if (!/^-?\d+$/.test(trimmed)) {
    return { kind: 'unreadable', reason: `not a whole number: "${trimmed.length > 60 ? `${trimmed.slice(0, 60)}…` : trimmed}"` }
  }

  return { kind: 'read', value: Number(trimmed), missing: [] }
}
