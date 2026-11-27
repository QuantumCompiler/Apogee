import { JSONValue } from '../../data/protocol/JSONValue'

/**
 * Whether this machine's ffmpeg burns a composition's captions into its
 * frames, as `check`'s `ffmpeg captions` row says it (the CLI's 52e): `ok`
 * when ffmpeg has the `subtitles` filter (libass), `skipped` -- with the
 * command that brings it -- when it has not, and the composer keeps the
 * captions beside the video as an SRT instead. What the new composition says
 * before it runs (design 1l); the CLI decides, the app only says it. The Mac
 * app's `CaptionsAbility` (`BusinessMedia`, `CaptionsAbility.swift`).
 */
export interface CaptionsAbility {
  /** Whether the captions burn in. */
  readonly burns: boolean
  /** The row's detail, in the CLI's words. */
  readonly detail: string
  /** What brings the ability, where the row names it. */
  readonly remedy: string | undefined
}

export const CaptionsAbility = {
  /** The row's name in `check`'s `Video` section. */
  row: 'ffmpeg captions',

  /** The ability `check --output-format json`'s document states; `undefined` where it has no such row -- a binary before the CLI's 52e. */
  of(document: JSONValue): CaptionsAbility | undefined {
    const row = (JSONValue.array(JSONValue.object(document)?.rows) ?? []).map((entry) => JSONValue.object(entry)).find((entry) => JSONValue.string(entry?.section) === 'Video' && JSONValue.string(entry?.name) === CaptionsAbility.row)
    if (!row) return undefined
    return { burns: JSONValue.string(row.status) === 'ok', detail: JSONValue.string(row.detail) ?? '', remedy: JSONValue.string(row.remedy) }
  },

  /** Why, in the CLI's words before its own consequence: `no subtitles filter (this ffmpeg was built without libass)`. */
  reason(ability: CaptionsAbility): string {
    return ability.detail.split(' -- ')[0] ?? ability.detail
  },
}
