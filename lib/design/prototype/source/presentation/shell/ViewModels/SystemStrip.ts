/**
 * What the sidebar's SYSTEM strip says (design 1e): the old prototype's
 * strip, whose numbers are the monitor's. The shell draws it and never
 * imports the monitor: `app` hands it the monitor's summary, as it hands the
 * shell each place's screen.
 */
export interface SystemStrip {
  /** The line beside SYSTEM while the strip is closed: `mem 26.0/128.0 GB · cpu 2%`. */
  readonly summary: string
  /** Memory past 85% of the machine's: the summary drawn as a warning. */
  readonly warning: boolean
  /** The open strip's lines, each a label, a bar when it has a share, and its value. */
  readonly rows: readonly SystemStripRow[]
}

export interface SystemStripRow {
  readonly label: string
  /** 0-100, when the line has a bar. */
  readonly percent?: number
  readonly value: string
  readonly warning?: boolean
}
