import type { ColorName } from '../../styles/Tokens/Tokens'
import { processBoxes, processesPage, processFan, processLinks } from '../Models/processes'
import type { ProcessBox, ProcessFan, ProcessLine, ProcessLink } from '../Models/processes'

/** A box as the page draws it: where it sits, its words, and its colors, each a token. */
export interface ProcessBoxFace {
  readonly id: string
  readonly x: number
  readonly y: number
  readonly width: number
  readonly height: number
  readonly title: string
  readonly app: boolean
  readonly lines: readonly ProcessLine[]
  readonly fill: ColorName
  readonly border: ColorName
}

/** A straight connector, in px on the page: dashed for a box's files, an arrowhead at its end for a child. */
export interface ProcessLineFace {
  readonly x1: number
  readonly y1: number
  readonly x2: number
  readonly y2: number
  readonly dashed: boolean
  readonly arrow: boolean
}

/** A connector's label: beside a link, or centred over the fan's first stretch (`width`). */
export interface ProcessLabelFace {
  readonly text: string
  readonly x: number
  readonly y: number
  readonly width?: number
}

export interface BuildProcessesFace {
  readonly width: number
  readonly height: number
  readonly title: string
  readonly intro: string
  readonly boxes: readonly ProcessBoxFace[]
  readonly lines: readonly ProcessLineFace[]
  readonly labels: readonly ProcessLabelFace[]
  /** The page's surface, its text, its notes and its connectors. */
  readonly background: ColorName
  readonly ink: ColorName
  readonly note: ColorName
  readonly connector: ColorName
}

/** How far a link's label sits right of its line, and above its middle. */
const besideLine = { x: 12, y: 8 }

/** The fan's label: its room either side, inside the stretch from the app to the bus, and its height over the line. */
const overFan = { inset: 16, above: 28 }

/** An arrow ends a pixel short of its box's edge, so the head meets the border. */
const arrowGap = 1

/**
 * The processes page (design 1r): the old BuildProcesses diagram over typed
 * data (`Models/processes.ts`) -- the boxes where the board drew them, and the
 * connectors computed from them: a dashed line between each stacked pair at
 * its middle, and the app's fan, a line from its right edge to a bus halfway to
 * its children, the bus from the first child's middle to the last's, and an
 * arrow to each. Every color is a token, so it follows the theme. Plain
 * TypeScript: the view draws it.
 */
export class BuildProcessesViewModel {
  constructor(
    readonly boxes: readonly ProcessBox[] = processBoxes,
    readonly links: readonly ProcessLink[] = processLinks,
    readonly fan: ProcessFan = processFan,
  ) {}

  face(): BuildProcessesFace {
    const lines: ProcessLineFace[] = []
    const labels: ProcessLabelFace[] = []
    for (const link of this.links) {
      const from = this.box(link.from)
      const to = this.box(link.to)
      const x = from.x + from.width / 2
      const top = from.y + from.height
      lines.push({ x1: x, y1: top, x2: x, y2: to.y, dashed: true, arrow: false })
      labels.push({ text: link.label, x: x + besideLine.x, y: (top + to.y) / 2 - besideLine.y })
    }
    const app = this.box(this.fan.from)
    const children = this.fan.to.map((id) => this.box(id))
    const start = app.x + app.width
    const middle = app.y + app.height / 2
    const left = Math.min(...children.map((child) => child.x))
    const bus = (start + left) / 2
    const centres = children.map((child) => child.y + child.height / 2)
    lines.push({ x1: start, y1: middle, x2: bus, y2: middle, dashed: false, arrow: false })
    lines.push({ x1: bus, y1: Math.min(...centres), x2: bus, y2: Math.max(...centres), dashed: false, arrow: false })
    children.forEach((child, index) => lines.push({ x1: bus, y1: centres[index] ?? 0, x2: child.x - arrowGap, y2: centres[index] ?? 0, dashed: false, arrow: true }))
    labels.push({ text: this.fan.label, x: start + overFan.inset, y: middle - overFan.above, width: bus - start - 2 * overFan.inset })
    return {
      width: processesPage.width,
      height: processesPage.height,
      title: processesPage.title,
      intro: processesPage.intro,
      boxes: this.boxes.map((box) => ({
        id: box.id,
        x: box.x,
        y: box.y,
        width: box.width,
        height: box.height,
        title: box.title,
        app: box.app === true,
        lines: box.lines,
        fill: 'surface.card',
        border: box.app ? 'status.accent' : 'line.strong',
      })),
      lines,
      labels,
      background: 'surface.window',
      ink: 'text.primary',
      note: 'text.secondary',
      connector: 'text.tertiary',
    }
  }

  private box(id: string): ProcessBox {
    const found = this.boxes.find((box) => box.id === id)
    if (!found) throw new Error(`the processes page joins the box ${id}, which it does not draw`)
    return found
  }
}
