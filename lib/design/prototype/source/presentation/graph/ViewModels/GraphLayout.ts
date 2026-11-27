/**
 * Where each node of a graph sits (design 1o): the Mac app's `GraphLayout`
 * (`GraphLayout.swift`), a seeded Fruchterman-Reingold layout over a unit
 * square, a fixed number of passes, its start positions from SplitMix64 --
 * the same input, the same picture, bit for bit -- then two steps the Mac
 * has no need of, since it draws dots and the old prototype draws cards:
 * the unit square scaled onto the old canvas (820 x 520, each card 160 x 44),
 * and a separation pass that pushes overlapping cards apart, falling back to
 * the nearest free cell of a grid where pushing cannot clear them, so no
 * card covers another and every card is inside the canvas. Plain TypeScript.
 */

/** A position in the unit square. */
export interface Point {
  readonly x: number
  readonly y: number
}

/** A card's top-left corner on the canvas, in px. */
export interface Placed {
  readonly x: number
  readonly y: number
}

/** An edge as the old canvas draws it: from its source card's right middle to its target card's left middle. */
export interface EdgeLine {
  readonly x1: number
  readonly y1: number
  readonly x2: number
  readonly y2: number
}

const mask = (1n << 64n) - 1n

/** SplitMix64, as `GraphLayout.swift`'s: UInt64 arithmetic wrapping, here in BigInt masked to 64 bits. */
class SplitMix64 {
  private state: bigint

  constructor(seed: bigint) {
    this.state = seed & mask
  }

  next(): bigint {
    this.state = (this.state + 0x9e3779b97f4a7c15n) & mask
    let z = this.state
    z = ((z ^ (z >> 30n)) * 0xbf58476d1ce4e5b9n) & mask
    z = ((z ^ (z >> 27n)) * 0x94d049bb133111ebn) & mask
    return z ^ (z >> 31n)
  }

  /** In [0, 1). */
  unit(): number {
    return Number(this.next() >> 11n) / 2 ** 53
  }
}

/** The gaps the separation pass keeps between cards, and the grid's. */
const gap = { x: 12, y: 8 } as const
const gridGap = { x: 4, y: 4 } as const

type Mutable = { x: number; y: number }

/** The Mac's seed, `0x5EED`. */
const defaultSeed = 0x5eedn

export const GraphLayout = {
  iterations: 300,
  inset: 0.08,
  /** The old prototype's canvas and card (`main.html` at `b375fac9`, `ui.isGraph`). */
  canvas: { width: 820, height: 520 },
  card: { width: 160, height: 44 },
  seed: defaultSeed,

  /** The generator's first `count` draws from `seed`: what the layout starts from. */
  draws(seed: bigint, count: number): number[] {
    const rng = new SplitMix64(seed)
    return Array.from({ length: count }, () => rng.unit())
  },

  /** The Mac's layout: each node's position in the unit square, by name, inset from its edge. */
  unit(nodes: readonly string[], edges: ReadonlyArray<readonly [string, string]>, seed: bigint = defaultSeed): Map<string, Point> {
    const names = [...new Set(nodes)].sort()
    const count = names.length
    if (count === 0) return new Map()
    if (count === 1) return new Map([[names[0] as string, { x: 0.5, y: 0.5 }]])
    const rng = new SplitMix64(seed)
    const x = names.map(() => rng.unit())
    const y = names.map(() => rng.unit())
    const index = new Map(names.map((name, at) => [name, at]))
    const links = edges.flatMap(([from, to]) => {
      const a = index.get(from)
      const b = index.get(to)
      return a === undefined || b === undefined || a === b ? [] : [[a, b] as const]
    })
    const k = Math.sqrt(1 / count)
    let temperature = 0.1
    for (let pass = 0; pass < GraphLayout.iterations; pass += 1) {
      const dx = new Array<number>(count).fill(0)
      const dy = new Array<number>(count).fill(0)
      for (let i = 0; i < count; i += 1) {
        for (let j = i + 1; j < count; j += 1) {
          let ddx = x[i]! - x[j]!
          let ddy = y[i]! - y[j]!
          let distance = Math.sqrt(ddx * ddx + ddy * ddy)
          if (distance < 1e-6) {
            ddx = 1e-3 * (i - j)
            ddy = 1e-3
            distance = Math.sqrt(ddx * ddx + ddy * ddy)
          }
          const force = (k * k) / distance
          dx[i]! += (ddx / distance) * force
          dy[i]! += (ddy / distance) * force
          dx[j]! -= (ddx / distance) * force
          dy[j]! -= (ddy / distance) * force
        }
      }
      for (const [a, b] of links) {
        const ddx = x[a]! - x[b]!
        const ddy = y[a]! - y[b]!
        const distance = Math.max(Math.sqrt(ddx * ddx + ddy * ddy), 1e-6)
        const force = (distance * distance) / k
        dx[a]! -= (ddx / distance) * force
        dy[a]! -= (ddy / distance) * force
        dx[b]! += (ddx / distance) * force
        dy[b]! += (ddy / distance) * force
      }
      for (let i = 0; i < count; i += 1) {
        const length = Math.max(Math.sqrt(dx[i]! * dx[i]! + dy[i]! * dy[i]!), 1e-9)
        const step = Math.min(length, temperature)
        x[i] = Math.min(1, Math.max(0, x[i]! + (dx[i]! / length) * step))
        y[i] = Math.min(1, Math.max(0, y[i]! + (dy[i]! / length) * step))
      }
      temperature = Math.max(temperature * 0.98, 0.002)
    }
    const fit = (value: number, low: number, high: number) => (high - low < 1e-9 ? 0.5 : GraphLayout.inset + ((value - low) / (high - low)) * (1 - 2 * GraphLayout.inset))
    const [minX, maxX, minY, maxY] = [Math.min(...x), Math.max(...x), Math.min(...y), Math.max(...y)]
    return new Map(names.map((name, at) => [name, { x: fit(x[at]!, minX, maxX), y: fit(y[at]!, minY, maxY) }]))
  },

  /** The unit square onto the canvas: each card's top-left, the square spanning the room a card leaves. */
  scaled(unit: ReadonlyMap<string, Point>): Map<string, Placed> {
    const room = { x: GraphLayout.canvas.width - GraphLayout.card.width, y: GraphLayout.canvas.height - GraphLayout.card.height }
    return new Map([...unit.entries()].map(([name, point]) => [name, { x: Math.round(point.x * room.x), y: Math.round(point.y * room.y) }]))
  },

  /**
   * The cards placed: the Mac's layout scaled onto the canvas, then pushed
   * apart where they overlap (each pair along the axis it overlaps least,
   * kept inside the canvas), and where pushing cannot clear them, each moved
   * to the nearest free cell of a grid of cards, in the order the layout
   * reads top to bottom. More cards than the grid holds are left as pushed.
   */
  place(nodes: readonly string[], edges: ReadonlyArray<readonly [string, string]>, seed: bigint = defaultSeed): Map<string, Placed> {
    const scaled = GraphLayout.scaled(GraphLayout.unit(nodes, edges, seed))
    const names = [...scaled.keys()]
    const cards: Mutable[] = names.map((name) => ({ ...(scaled.get(name) as Placed) }))
    const { width: w, height: h } = GraphLayout.card
    const maxX = GraphLayout.canvas.width - w
    const maxY = GraphLayout.canvas.height - h
    const clear = () => cards.every((a, i) => cards.every((b, j) => j <= i || Math.abs(a.x - b.x) >= w + gap.x || Math.abs(a.y - b.y) >= h + gap.y))
    for (let pass = 0; pass < 400 && !clear(); pass += 1) {
      for (let i = 0; i < cards.length; i += 1) {
        for (let j = i + 1; j < cards.length; j += 1) {
          const a = cards[i]!
          const b = cards[j]!
          const overX = w + gap.x - Math.abs(a.x - b.x)
          const overY = h + gap.y - Math.abs(a.y - b.y)
          if (overX <= 0 || overY <= 0) continue
          if (overY / (h + gap.y) <= overX / (w + gap.x)) {
            const sign = a.y < b.y || (a.y === b.y && i < j) ? -1 : 1
            a.y += (sign * overY) / 2
            b.y -= (sign * overY) / 2
          } else {
            const sign = a.x < b.x || (a.x === b.x && i < j) ? -1 : 1
            a.x += (sign * overX) / 2
            b.x -= (sign * overX) / 2
          }
        }
      }
      for (const card of cards) {
        card.x = Math.min(maxX, Math.max(0, card.x))
        card.y = Math.min(maxY, Math.max(0, card.y))
      }
    }
    if (!clear()) {
      const columns = Math.floor((GraphLayout.canvas.width + gridGap.x) / (w + gridGap.x))
      const rows = Math.floor((GraphLayout.canvas.height + gridGap.y) / (h + gridGap.y))
      if (cards.length <= columns * rows) {
        const cells: Mutable[] = []
        for (let row = 0; row < rows; row += 1) for (let column = 0; column < columns; column += 1) cells.push({ x: column * (w + gridGap.x), y: row * (h + gridGap.y) })
        const taken = new Set<number>()
        const order = cards.map((card, at) => ({ card, at })).sort((a, b) => a.card.y - b.card.y || a.card.x - b.card.x || a.at - b.at)
        for (const { card } of order) {
          let best = -1
          let bestDistance = Number.POSITIVE_INFINITY
          cells.forEach((cell, at) => {
            if (taken.has(at)) return
            const distance = Math.hypot(cell.x - card.x, cell.y - card.y)
            if (distance < bestDistance) [best, bestDistance] = [at, distance]
          })
          taken.add(best)
          card.x = cells[best]!.x
          card.y = cells[best]!.y
        }
      }
    }
    return new Map(names.map((name, at) => [name, { x: Math.round(cards[at]!.x), y: Math.round(cards[at]!.y) }]))
  },

  /**
   * An edge from `from`'s card to `to`'s: from its right middle to the
   * target's left middle, as the old canvas draws its left-to-right
   * picture; where the layout puts the target to the left, from left to
   * right; and where the two share a column, from the facing top or bottom.
   */
  edge(from: Placed, to: Placed): EdgeLine {
    const { width: w, height: h } = GraphLayout.card
    if (to.x >= from.x + w) return { x1: from.x + w, y1: from.y + h / 2, x2: to.x, y2: to.y + h / 2 }
    if (to.x + w <= from.x) return { x1: from.x, y1: from.y + h / 2, x2: to.x + w, y2: to.y + h / 2 }
    return to.y >= from.y ? { x1: from.x + w / 2, y1: from.y + h, x2: to.x + w / 2, y2: to.y } : { x1: from.x + w / 2, y1: from.y, x2: to.x + w / 2, y2: to.y + h }
  },
}
