import { describe, expect, test } from 'vitest'
import { presets } from '../../../source/presentation/app/Previews/Presets'
import { boardHeadings, boards, boardWidth, galleryPages } from '../../../source/presentation/gallery/Models/Boards'
import type { BoardEntry } from '../../../source/presentation/gallery/Models/Boards'
import { againstCanvas, boardStart, framesOf, oldFilesExist, readCanvas } from './CanvasTable'

describe("the board table against the canvas, while canvas.json exists (design 1q's guardrail)", () => {
  test.skipIf(!oldFilesExist)('every Build and Foundations board in canvas.json has its entry, with the same page, row, height and title, and their six headings match', () => {
    expect(againstCanvas(boards, boardHeadings, readCanvas())).toEqual([])
  })

  test.skipIf(!oldFilesExist)('a planted missing board fails, naming it', () => {
    const planted = boards.filter((entry) => entry.id !== 'ScreenModelsStore')
    expect(againstCanvas(planted, boardHeadings, readCanvas())).toEqual(["ScreenModelsStore is on the canvas's build page but not in the gallery's table"])
  })

  test.skipIf(!oldFilesExist)('a planted changed title fails, naming the board; so do a moved row, a changed height and a heading lost', () => {
    const planted = boards.map((entry): BoardEntry => {
      if (entry.id === 'ScreenConsole') return { ...entry, title: 'Console' }
      if (entry.id === 'ColorTokens') return { ...entry, row: 1, height: 1500 }
      return entry
    })
    expect(againstCanvas(planted, boardHeadings.slice(1), readCanvas())).toEqual([
      'ColorTokens is in row 1 in the table, and in row 0 on the canvas',
      'ColorTokens is 1500 px tall in the table, and 1520 px on the canvas',
      `ScreenConsole's title is "Console" in the table, and the canvas says "Console · any command, as typed"`,
      `the canvas's heading "Build — the contract, the measurements, and the app itself" over build row 0 is not in the table`,
    ])
  })

  test.skipIf(!oldFilesExist)("each Screen board's entry names the start preset its file names", () => {
    const screens = boards.filter((entry) => entry.draws === 'screen' && !entry.noOldBoard)
    expect(screens).toHaveLength(66)
    for (const entry of screens) expect(entry.start, entry.id).toBe(boardStart(entry.id))
  })
})

describe('the board table', () => {
  test('two pages, Build and Foundations: Exploration is not ported', () => {
    expect(galleryPages.map((page) => page.name)).toEqual(['Build', 'Foundations'])
    expect(new Set(boards.map((entry) => entry.page))).toEqual(new Set(['build', 'foundations']))
  })

  test("Build's first row is Contract, Layout, App and Processes; App is the live entry; the three build boards draw their pages (1r)", () => {
    expect(boards.filter((entry) => entry.page === 'build' && entry.row === 0).map((entry) => [entry.id, entry.draws, entry.item ?? ''])).toEqual([
      ['BuildContract', 'contract', ''],
      ['BuildLayout', 'layout', ''],
      ['App', 'app', ''],
      ['BuildProcesses', 'processes', ''],
    ])
  })

  test('Foundations holds the color, type, four components and logo boards, in the canvas order', () => {
    expect(boards.filter((entry) => entry.page === 'foundations').map((entry) => [entry.id, entry.draws, entry.section ?? ''])).toEqual([
      ['ColorTokens', 'colors', ''],
      ['TypeSpace', 'type', ''],
      ['ComponentsModel', 'components', 'model'],
      ['ComponentsTurn', 'components', 'turn'],
      ['ComponentsPrompts', 'components', 'asking'],
      ['ComponentsMedia', 'components', 'media'],
      ['Logo', 'logo', ''],
    ])
  })

  test('ids are unique, and every board is 1440 wide', () => {
    expect(new Set(boards.map((entry) => entry.id)).size).toBe(boards.length)
    expect(boardWidth).toBe(1440)
  })

  test("a board-less preset's frame sits at the end of its row, titled Place · state and marked as having no old board", () => {
    for (const page of galleryPages) {
      const rows = [...new Set(boards.filter((entry) => entry.page === page.id).map((entry) => entry.row))]
      for (const row of rows) {
        const inRow = boards.filter((entry) => entry.page === page.id && entry.row === row)
        const first = inRow.findIndex((entry) => entry.noOldBoard)
        if (first < 0) continue
        expect(inRow.slice(first).every((entry) => entry.noOldBoard), `${page.id} row ${row}`).toBe(true)
      }
    }
    const boardless = boards.filter((entry) => entry.noOldBoard)
    expect(boardless.map((entry) => entry.start)).toEqual([
      'chat:compacting',
      'models:notran',
      'training:unreadable',
      'training:passrunning',
      'training:regimes',
      'library:unsaved',
      'knowledge:mixed',
      'tasks:more',
      'tasks:resuming',
      'tasks:resumerefused',
      'settings:fixing',
      'settings:fixfailed',
      'settings:nosearch',
      'settings:roles',
      'settings:web',
      'settings:memory',
      'settings:keyboard',
    ])
    for (const entry of boardless) {
      expect(entry.title, entry.id).toMatch(/^[A-Z][a-z]+ · \S/)
      expect([entry.id, entry.draws, entry.height]).toEqual([entry.start, 'screen', 900])
    }
  })
})

describe("every preset has a frame (design 1q's guardrail, which 1s keeps)", () => {
  test("each preset in the stand-in's table appears in exactly one entry", () => {
    expect(framesOf(Object.keys(presets), boards)).toEqual([])
    expect(boards.filter((entry) => entry.start !== undefined)).toHaveLength(Object.keys(presets).length)
  })

  test('a planted new preset with no entry fails, naming it; so do a preset in two entries and a frame naming no preset', () => {
    expect(framesOf([...Object.keys(presets), 'models:planted'], boards)).toEqual(['the preset models:planted has no frame in the gallery\'s board table'])
    const twice: BoardEntry = { id: 'Planted', start: 'chat', title: 'Session · planted', page: 'build', row: 1, height: 900, draws: 'screen', noOldBoard: true }
    const stray: BoardEntry = { id: 'Stray', start: 'chat:stray', title: 'Session · stray', page: 'build', row: 1, height: 900, draws: 'screen', noOldBoard: true }
    expect(framesOf(Object.keys(presets), [...boards, twice, stray])).toEqual([
      'the preset chat has 2 frames: ScreenSessionStreaming, Planted',
      "Stray names the preset chat:stray, which the stand-in's table does not hold",
    ])
  })
})
