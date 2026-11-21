import type { ColorName } from '../../styles/Tokens/Tokens'
import { contractCalls, contractDecisions, contractGaps, contractParts, contractRows, contractWords, startingUp } from '../Models/contract'
import type { ContractPart, ContractRow, ContractStatus, Gap, GapId, GapWhen, Lead } from '../Models/contract'

/** A stat card: what it counts, the count, its words and the tone of its number. */
export interface ContractCard {
  readonly status: ContractStatus
  readonly count: number
  readonly words: string
  readonly tone: ColorName
}

/** A row's status chip: its words, its tone, whether it is dashed, and for a gap the gap it links to. */
export interface ContractChip {
  readonly label: string
  readonly tone: ColorName
  readonly dashed: boolean
  readonly gap?: GapId
  /** Where the gap's own row is on the page, for the chip's link. */
  readonly anchor?: string
}

export interface ContractRowFace {
  readonly shows: string
  readonly how: string
  readonly chip: ContractChip
}

export interface ContractPartFace {
  readonly part: ContractPart
  readonly rows: readonly ContractRowFace[]
}

/** A gap as the page draws it: a closed one dimmed, in the accent. */
export interface GapFace {
  readonly id: GapId
  readonly anchor: string
  readonly title: string
  readonly change: string
  /** "Closed: …" or "Until then: …". */
  readonly until: string
  readonly when: GapWhen
  readonly closed: boolean
}

/** The chip legend in the map's introduction: each status's chip and what it means. */
export interface LegendFace {
  readonly chip: ContractChip
  readonly words: string
}

const statuses: readonly ContractStatus[] = ['ready', 'app', 'gap', 'verify']

/** Each status's chip, as the board draws it: its tone, and dashed for what is only assumed. */
const chipTone: Readonly<Record<ContractStatus, { readonly tone: ColorName; readonly dashed: boolean }>> = {
  ready: { tone: 'status.accent', dashed: false },
  app: { tone: 'text.secondary', dashed: false },
  gap: { tone: 'status.warning', dashed: false },
  verify: { tone: 'text.tertiary', dashed: true },
}

/** Each card's number, in the board's tones: ready in the accent, the gaps in the warning, the assumed muted. */
const cardTone: Readonly<Record<ContractStatus, ColorName>> = {
  ready: 'status.accent',
  app: 'text.primary',
  gap: 'status.warning',
  verify: 'text.tertiary',
}

/** Where a gap's row is on the page. */
export function gapAnchor(id: GapId): string {
  return `contract-gap-${id}`
}

/**
 * The contract page (design 1r): the old BuildContract board over typed data
 * (`Models/contract.ts`). It groups the rows by part, counts the stat cards
 * -- the rows by their status, and the gaps still open -- links each gap
 * chip to its gap, and says where a row and the gaps disagree. Plain
 * TypeScript: the view draws it.
 */
export class BuildContractViewModel {
  constructor(
    readonly rows: readonly ContractRow[] = contractRows,
    readonly gaps: readonly Gap[] = contractGaps,
  ) {}

  readonly words = contractWords
  readonly startingUp: readonly Lead[] = startingUp
  readonly decisions: readonly Lead[] = contractDecisions
  readonly calls: readonly Lead[] = contractCalls

  /**
   * The four stat cards, counted, never typed: the parts ready, the parts the
   * app does itself, the gaps still open (not `Closed`), and the parts
   * assumed.
   */
  cards(): ContractCard[] {
    return statuses.map((status) => ({
      status,
      count: status === 'gap' ? this.gaps.filter((gap) => gap.when !== 'Closed').length : this.rows.filter((row) => row.status === status).length,
      words: contractWords.cards[status],
      tone: cardTone[status],
    }))
  }

  /** The chip legend: each status's chip and what it means. */
  legend(): LegendFace[] {
    return statuses.map((status) => ({ chip: { label: status, ...chipTone[status] }, words: contractWords.legend[status] }))
  }

  /** A row's chip: `ready`, `app`, `verify`, or `gap G<n>` linked to its gap. */
  chip(row: ContractRow): ContractChip {
    if (row.status !== 'gap') return { label: row.status, ...chipTone[row.status] }
    return { label: `gap ${row.gap}`, ...chipTone.gap, gap: row.gap, anchor: gapAnchor(row.gap) }
  }

  /** The map: each part in the board's order, its rows in table order. */
  parts(): ContractPartFace[] {
    return contractParts.map((part) => ({
      part,
      rows: this.rows.filter((row) => row.part === part).map((row) => ({ shows: row.shows, how: row.how, chip: this.chip(row) })),
    }))
  }

  /** The gaps, in order: a closed one says how it closed, an open one what the app does until then. */
  gapRows(): GapFace[] {
    return this.gaps.map((gap) => ({
      id: gap.id,
      anchor: gapAnchor(gap.id),
      title: gap.title,
      change: gap.change,
      until: gap.when === 'Closed' ? `Closed: ${gap.note}` : `Until then: ${gap.fallback}`,
      when: gap.when,
      closed: gap.when === 'Closed',
    }))
  }

  /**
   * Where the rows and the gaps disagree, each a sentence (design 1r's
   * guardrail): a gap row naming a gap that does not exist or is closed, and
   * an open gap no row names. None on the page as it is.
   */
  problems(): string[] {
    const found: string[] = []
    for (const row of this.rows) {
      if (row.status !== 'gap') continue
      const gap = this.gaps.find((each) => each.id === row.gap)
      if (!gap) found.push(`the row "${row.shows}" names gap ${row.gap}, which no gap is`)
      else if (gap.when === 'Closed') found.push(`the row "${row.shows}" names gap ${row.gap}, which is closed`)
    }
    for (const gap of this.gaps) {
      if (gap.when === 'Closed') continue
      if (!this.rows.some((row) => row.status === 'gap' && row.gap === gap.id)) found.push(`gap ${gap.id} is open, and no row names it`)
    }
    return found
  }
}
