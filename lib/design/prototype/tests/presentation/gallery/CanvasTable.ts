import { existsSync, readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import type { BoardEntry, BoardHeading } from '../../../source/presentation/gallery/Models/Boards'

/** The old prototype's spec folder, while 1t has not deleted it: this file is tests/presentation/gallery/CanvasTable.ts. */
export const specFolder = resolve(dirname(fileURLToPath(import.meta.url)), '../../../../spec')

/** The canvas's index, `lib/design/spec/json/canvas.json`. */
export const canvasFile = resolve(specFolder, 'json/canvas.json')

/** Whether the old files are still there to check the table against (1t deletes them). */
export const oldFilesExist = existsSync(canvasFile)

/** canvas.json, as far as the table reads it. */
export interface Canvas {
  readonly boards: Readonly<Record<string, { readonly page: string; readonly x: number; readonly y: number; readonly w: number; readonly h: number; readonly title: string }>>
  readonly notes: Readonly<Record<string, { readonly page: string; readonly y: number; readonly text: string; readonly kind: string }>>
}

export function readCanvas(): Canvas {
  return JSON.parse(readFileSync(canvasFile, 'utf8')) as Canvas
}

/** The pages the gallery ports: Exploration is not (the item's default). */
const ported = ['build', 'foundations']

/**
 * Where the table and the canvas disagree, each as a sentence naming the
 * board or heading; none when every Build and Foundations board has its
 * entry with the canvas's page, row, height and title, and their headings
 * match. A row is the board's place among its page's distinct `y`s, top to
 * bottom; a heading's is the row 300 px below it.
 */
export function againstCanvas(table: readonly BoardEntry[], headings: readonly BoardHeading[], canvas: Canvas): string[] {
  const found: string[] = []
  const rows = new Map<string, number[]>()
  for (const board of Object.values(canvas.boards)) {
    if (!ported.includes(board.page)) continue
    rows.set(board.page, [...new Set([...(rows.get(board.page) ?? []), board.y])].sort((a, b) => a - b))
  }
  const byId = new Map(table.map((entry) => [entry.id, entry]))
  for (const [file, board] of Object.entries(canvas.boards)) {
    if (!ported.includes(board.page)) continue
    const id = file.replace(/\.dc\.html$/, '')
    const entry = byId.get(id)
    if (!entry) {
      found.push(`${id} is on the canvas's ${board.page} page but not in the gallery's table`)
      continue
    }
    const row = (rows.get(board.page) ?? []).indexOf(board.y)
    if (entry.page !== board.page) found.push(`${id} is on the ${entry.page} page in the table, and on the canvas's ${board.page} page`)
    if (entry.row !== row) found.push(`${id} is in row ${entry.row} in the table, and in row ${row} on the canvas`)
    if (entry.height !== board.h) found.push(`${id} is ${entry.height} px tall in the table, and ${board.h} px on the canvas`)
    if (entry.title !== board.title) found.push(`${id}'s title is "${entry.title}" in the table, and the canvas says "${board.title}"`)
  }
  const onCanvas = new Set(Object.keys(canvas.boards).map((file) => file.replace(/\.dc\.html$/, '')))
  for (const entry of table) {
    if (!entry.noOldBoard && !onCanvas.has(entry.id)) found.push(`${entry.id} is in the gallery's table but on no page of the canvas`)
  }
  const said = (heading: BoardHeading) => `"${heading.text}" over ${heading.page} row ${heading.row}`
  const canvasHeadings = Object.values(canvas.notes)
    .filter((note) => note.kind === 'title1' && ported.includes(note.page))
    .map((note): BoardHeading => ({ text: note.text, page: note.page as BoardHeading['page'], row: (rows.get(note.page) ?? []).indexOf(note.y + 300) }))
  const theirs = new Set(canvasHeadings.map(said))
  const ours = new Set(headings.map(said))
  for (const heading of canvasHeadings) if (!ours.has(said(heading))) found.push(`the canvas's heading ${said(heading)} is not in the table`)
  for (const heading of headings) if (!theirs.has(said(heading))) found.push(`the table's heading ${said(heading)} is not on the canvas`)
  return found
}

/** A Screen board's start preset, as its file names it: its one varying line, `<dc-import name="App" start="…">`. */
export function boardStart(id: string): string | undefined {
  const file = resolve(specFolder, `html/${id}.dc.html`)
  if (!existsSync(file)) return undefined
  return /<dc-import name="App" start="([^"]*)"/.exec(readFileSync(file, 'utf8'))?.[1]
}

/**
 * Whether each preset of the stand-in's table has exactly one frame, each a
 * sentence naming the preset; none when it does. A frame naming a preset the
 * table lacks is said too.
 */
export function framesOf(presets: readonly string[], table: readonly BoardEntry[]): string[] {
  const found: string[] = []
  for (const preset of presets) {
    const frames = table.filter((entry) => entry.start === preset).map((entry) => entry.id)
    if (frames.length === 0) found.push(`the preset ${preset} has no frame in the gallery's board table`)
    if (frames.length > 1) found.push(`the preset ${preset} has ${frames.length} frames: ${frames.join(', ')}`)
  }
  for (const entry of table) {
    if (entry.start !== undefined && !presets.includes(entry.start)) found.push(`${entry.id} names the preset ${entry.start}, which the stand-in's table does not hold`)
  }
  return found
}
