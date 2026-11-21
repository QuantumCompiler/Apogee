/** How an install stands, as its card draws it. */
export type InstallCardState = 'running' | 'installed' | 'stopped' | 'failed'

/** What an install's card says: its view-model's words; the look is the card's. */
export interface InstallCardFace {
  readonly state: InstallCardState
  readonly headline: string
  /** The line it is on, or -- failed -- why, in the CLI's words. */
  readonly detail: string
  /** The install as typed: `apogee train setup --with video`. */
  readonly command: string
}

/** What an install's consent sheet says and its buttons, by the set it brings (GUI 1m, 1o, 4f; design 1k). */
export interface InstallSheetWords {
  readonly title: string
  readonly body: string
  readonly confirm: 'Install' | 'Rebuild'
  readonly cancel: 'Not now'
}

/** What installing does, as the old prototype's `rtc.body` and the Mac's sheet say it. */
export const installBody = "This downloads and installs Python packages into Apogee's own environment, once. Nothing is installed unless you say yes; the install runs here, with Stop, and what was refused runs again once it is in."

/**
 * The consent sheet's words: `Install what <set> needs?`, or -- where the
 * install replaces the environment (the CLI's M19) -- the rebuild's, its
 * cost said by `rebuild` (the Mac's `MediaLibrary.rebuildNote`, Business's to
 * say, handed in).
 */
export function installSheetWords(set: string, rebuild?: string): InstallSheetWords {
  return rebuild === undefined
    ? { title: `Install what ${set} needs?`, body: installBody, confirm: 'Install', cancel: 'Not now' }
    : { title: "Rebuild Apogee's Python environment?", body: rebuild, confirm: 'Rebuild', cancel: 'Not now' }
}
