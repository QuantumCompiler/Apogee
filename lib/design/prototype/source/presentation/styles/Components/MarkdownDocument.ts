/** A run of inline text and how it is set: the Mac app's `MarkdownSpan`. */
export interface MarkdownSpan {
  readonly text: string
  readonly strong?: boolean
  readonly emphasis?: boolean
  readonly code?: boolean
  /** A link's address. */
  readonly link?: string
  /** An image's address: drawn as its alt text and its address, never fetched. */
  readonly image?: string
}

export interface MarkdownListItem {
  readonly blocks: readonly MarkdownBlock[]
}

export type MarkdownAlignment = 'leading' | 'center' | 'trailing'

/** One block of an answer: the Mac app's `MarkdownBlock`. */
export type MarkdownBlock =
  | { readonly case: 'paragraph'; readonly spans: readonly MarkdownSpan[] }
  | { readonly case: 'heading'; readonly level: number; readonly spans: readonly MarkdownSpan[] }
  | { readonly case: 'list'; readonly ordered: boolean; readonly start: number; readonly items: readonly MarkdownListItem[] }
  | { readonly case: 'quote'; readonly blocks: readonly MarkdownBlock[] }
  /** A fence: its language as its info string names it, its text, and whether its closing fence has come (mid-stream it may not have). */
  | { readonly case: 'code'; readonly language: string | undefined; readonly text: string; readonly closed: boolean }
  | { readonly case: 'rule' }
  | {
      readonly case: 'table'
      readonly header: ReadonlyArray<readonly MarkdownSpan[]>
      readonly align: readonly MarkdownAlignment[]
      readonly rows: ReadonlyArray<ReadonlyArray<readonly MarkdownSpan[]>>
    }

/** Where a link may go. */
export const MarkdownLink = {
  /**
   * Whether a link is followed: only http, https and mailto, as the Mac app's
   * `LinkOpener` opens them. A `file:` link, or any other scheme, is drawn and
   * not followed (design 1u's decision).
   */
  follows(address: string): boolean {
    return /^(https?:\/\/|mailto:)/i.test(address)
  },
}

const fenceOpen = /^ {0,3}(`{3,}|~{3,})\s*([^\s`]*)[^`]*$/
const headingLine = /^ {0,3}(#{1,6})(?:\s+(.*?))?\s*#*\s*$/
const ruleLine = /^ {0,3}([-*_])(?:\s*\1){2,}\s*$/
const quoteLine = /^ {0,3}>\s?(.*)$/
const listLine = /^( *)([-*+]|\d{1,9}[.)])(?:\s+(.*)|$)/
const delimiterRow = /^\s*\|?\s*:?-+:?\s*(\|\s*:?-+:?\s*)*\|?\s*$/

/**
 * Markdown as a model writes it, read into blocks and spans: the Mac app's
 * `MarkdownDocument` (`PresentationStyles`, `MarkdownDocument.swift`), the
 * blocks an answer carries -- paragraphs, headings, nested and numbered
 * lists, quotes, fenced code, rules, tables -- and its spans: bold,
 * emphasis, inline code, links and images. Read tolerantly mid-stream: a
 * fence not yet closed is code all the same.
 */
export interface MarkdownDocument {
  readonly blocks: readonly MarkdownBlock[]
}

export const MarkdownDocument = {
  parse(text: string): MarkdownDocument {
    return { blocks: blocksOf(text.replace(/\r\n?/g, '\n').split('\n')) }
  },
}

function blocksOf(lines: readonly string[]): MarkdownBlock[] {
  const blocks: MarkdownBlock[] = []
  let index = 0
  let paragraph: string[] = []
  const endParagraph = () => {
    if (paragraph.length > 0) blocks.push({ case: 'paragraph', spans: spansOf(paragraph.map((line) => line.trim()).join(' ')) })
    paragraph = []
  }
  while (index < lines.length) {
    const line = lines[index] ?? ''
    if (line.trim() === '') {
      endParagraph()
      index += 1
      continue
    }
    const fence = fenceOpen.exec(line)
    if (fence) {
      endParagraph()
      const marker = fence[1] ?? '```'
      const body: string[] = []
      index += 1
      let closed = false
      while (index < lines.length) {
        const inner = lines[index] ?? ''
        if (inner.trim().startsWith(marker) && inner.trim().replace(new RegExp(`^${marker[0] === '`' ? '`' : '~'}+`), '').trim() === '') {
          closed = true
          index += 1
          break
        }
        body.push(inner)
        index += 1
      }
      const info = fence[2] ?? ''
      blocks.push({ case: 'code', language: info === '' ? undefined : info, text: body.join('\n'), closed })
      continue
    }
    const heading = headingLine.exec(line)
    if (heading) {
      endParagraph()
      blocks.push({ case: 'heading', level: (heading[1] ?? '#').length, spans: spansOf(heading[2] ?? '') })
      index += 1
      continue
    }
    if (ruleLine.test(line)) {
      endParagraph()
      blocks.push({ case: 'rule' })
      index += 1
      continue
    }
    if (quoteLine.test(line)) {
      endParagraph()
      const quoted: string[] = []
      while (index < lines.length && quoteLine.test(lines[index] ?? '')) {
        quoted.push(quoteLine.exec(lines[index] ?? '')?.[1] ?? '')
        index += 1
      }
      blocks.push({ case: 'quote', blocks: blocksOf(quoted) })
      continue
    }
    if (line.includes('|') && delimiterRow.test(lines[index + 1] ?? '') && (lines[index + 1] ?? '').includes('-')) {
      endParagraph()
      const header = cellsOf(line)
      const align = cellsOf(lines[index + 1] ?? '').map((cell): MarkdownAlignment => {
        const trimmed = cell.trim()
        if (trimmed.startsWith(':') && trimmed.endsWith(':')) return 'center'
        return trimmed.endsWith(':') ? 'trailing' : 'leading'
      })
      index += 2
      const rows: MarkdownSpan[][][] = []
      while (index < lines.length && (lines[index] ?? '').includes('|') && (lines[index] ?? '').trim() !== '') {
        rows.push(cellsOf(lines[index] ?? '').map((cell) => spansOf(cell)))
        index += 1
      }
      blocks.push({ case: 'table', header: header.map((cell) => spansOf(cell)), align, rows })
      continue
    }
    const item = listLine.exec(line)
    // A list may interrupt a paragraph: a bullet always, a numbered one from 1 (CommonMark's rule).
    if (item && (paragraph.length === 0 || !/\d/.test(item[2] ?? '') || /^1[.)]$/.test(item[2] ?? ''))) {
      endParagraph()
      const list = readList(lines, index)
      blocks.push(list.block)
      index = list.next
      continue
    }
    paragraph.push(line)
    index += 1
  }
  endParagraph()
  return blocks
}

/** A list starting at `start`: its items, each its lines dedented and read as blocks, until a line that belongs to none. */
function readList(lines: readonly string[], start: number): { block: MarkdownBlock; next: number } {
  const first = listLine.exec(lines[start] ?? '')
  const indent = (first?.[1] ?? '').length
  const marker = first?.[2] ?? '-'
  const ordered = /\d/.test(marker)
  const items: MarkdownListItem[] = []
  let index = start
  while (index < lines.length) {
    const match = listLine.exec(lines[index] ?? '')
    if (!match || (match[1] ?? '').length !== indent || /\d/.test(match[2] ?? '') !== ordered) break
    const contentIndent = (match[1] ?? '').length + (match[2] ?? '').length + 1
    const body: string[] = [match[3] ?? '']
    index += 1
    while (index < lines.length) {
      const next = lines[index] ?? ''
      if (next.trim() === '') {
        // A blank line ends the item unless the next line is indented under it.
        const after = lines[index + 1] ?? ''
        if (after.trim() !== '' && after.length - after.trimStart().length >= contentIndent) {
          body.push('')
          index += 1
          continue
        }
        break
      }
      const leading = next.length - next.trimStart().length
      if (leading > indent) {
        body.push(next.slice(Math.min(leading, contentIndent)))
        index += 1
        continue
      }
      break
    }
    items.push({ blocks: blocksOf(body) })
    // A blank line between items of the same list keeps the list going.
    if ((lines[index] ?? '').trim() === '' && listLine.exec(lines[index + 1] ?? '')?.[1]?.length === indent) index += 1
  }
  return { block: { case: 'list', ordered, start: ordered ? Number(/\d+/.exec(marker)?.[0] ?? '1') : 1, items }, next: index }
}

/** A table row's cells, its outer pipes dropped. */
function cellsOf(line: string): string[] {
  const trimmed = line.trim().replace(/^\|/, '').replace(/\|$/, '')
  return trimmed.split('|').map((cell) => cell.trim())
}

/** A line's spans: code first (nothing inside it is read), then images, links, bold and emphasis. */
function spansOf(text: string, style: Omit<MarkdownSpan, 'text'> = {}): MarkdownSpan[] {
  const spans: MarkdownSpan[] = []
  let plain = ''
  const flush = () => {
    if (plain !== '') spans.push({ text: plain, ...style })
    plain = ''
  }
  let at = 0
  while (at < text.length) {
    const rest = text.slice(at)
    const code = /^(`+)([\s\S]*?[^`])\1(?!`)/.exec(rest)
    if (code) {
      flush()
      spans.push({ text: (code[2] ?? '').trim() === '' ? (code[2] ?? '') : (code[2] ?? '').replace(/^ (.*) $/, '$1'), ...style, code: true })
      at += code[0].length
      continue
    }
    const image = /^!\[([^\]]*)\]\(([^)\s]+)(?:\s+"[^"]*")?\)/.exec(rest)
    if (image) {
      flush()
      spans.push({ text: image[1] ?? '', ...style, image: image[2] ?? '' })
      at += image[0].length
      continue
    }
    const link = /^\[([^\]]+)\]\(([^)\s]+)(?:\s+"[^"]*")?\)/.exec(rest)
    if (link) {
      flush()
      spans.push(...spansOf(link[1] ?? '', { ...style, link: link[2] ?? '' }))
      at += link[0].length
      continue
    }
    const strong = /^(\*\*|__)(?=\S)([\s\S]*?\S)\1/.exec(rest)
    if (strong) {
      flush()
      spans.push(...spansOf(strong[2] ?? '', { ...style, strong: true }))
      at += strong[0].length
      continue
    }
    const emphasis = /^(\*|_)(?=\S)([\s\S]*?\S)\1(?![*_])/.exec(rest)
    if (emphasis && (emphasis[1] === '*' || !/\w/.test(text[at - 1] ?? ''))) {
      flush()
      spans.push(...spansOf(emphasis[2] ?? '', { ...style, emphasis: true }))
      at += emphasis[0].length
      continue
    }
    plain += text[at] ?? ''
    at += 1
  }
  flush()
  return spans
}
