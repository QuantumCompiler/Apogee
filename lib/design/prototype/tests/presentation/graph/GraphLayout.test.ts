import { describe, expect, test } from 'vitest'
import { GraphLayout } from '../../../source/presentation/graph/ViewModels/GraphLayout'
import type { Placed } from '../../../source/presentation/graph/ViewModels/GraphLayout'

/** Whether two cards overlap: their rectangles share any area. */
function overlapping(a: Placed, b: Placed): boolean {
  return a.x < b.x + GraphLayout.card.width && b.x < a.x + GraphLayout.card.width && a.y < b.y + GraphLayout.card.height && b.y < a.y + GraphLayout.card.height
}

function problems(placed: ReadonlyMap<string, Placed>): string[] {
  const cards = [...placed.entries()]
  const found: string[] = []
  for (const [name, at] of cards) {
    if (at.x < 0 || at.y < 0 || at.x + GraphLayout.card.width > GraphLayout.canvas.width || at.y + GraphLayout.card.height > GraphLayout.canvas.height) found.push(`${name} outside the canvas at ${at.x},${at.y}`)
  }
  for (let i = 0; i < cards.length; i += 1) for (let j = i + 1; j < cards.length; j += 1) if (overlapping(cards[i]![1], cards[j]![1])) found.push(`${cards[i]![0]} over ${cards[j]![0]}`)
  return found
}

/** A planted document of `count` nodes: a chain with a few cross links, as a call graph of that size is. */
function planted(count: number): { nodes: string[]; edges: Array<[string, string]> } {
  const nodes = Array.from({ length: count }, (_, index) => `node${index}`)
  const edges: Array<[string, string]> = []
  for (let index = 1; index < count; index += 1) edges.push([`node${Math.floor((index - 1) / 2)}`, `node${index}`])
  for (let index = 0; index + 7 < count; index += 5) edges.push([`node${index}`, `node${index + 7}`])
  return { nodes, edges }
}

describe("GraphLayout (design 1o): the Mac app's seeded Fruchterman-Reingold layout (GraphLayout.swift), then a separation pass for the prototype's cards", () => {
  test("the Mac's layout test: the same graph the same picture whatever its order, another seed another, inside its inset, no two nodes on top of each other", () => {
    const nodes = ['a', 'b', 'c', 'd', 'e']
    const edges: Array<[string, string]> = [
      ['a', 'b'],
      ['b', 'c'],
      ['c', 'a'],
      ['d', 'e'],
    ]
    const one = GraphLayout.unit(nodes, edges)
    expect(GraphLayout.unit([...nodes].reverse(), edges)).toEqual(one)
    expect(GraphLayout.unit(nodes, edges, 7n)).not.toEqual(one)
    for (const point of one.values()) {
      expect(point.x).toBeGreaterThanOrEqual(GraphLayout.inset - 1e-9)
      expect(point.x).toBeLessThanOrEqual(1 - GraphLayout.inset + 1e-9)
      expect(point.y).toBeGreaterThanOrEqual(GraphLayout.inset - 1e-9)
      expect(point.y).toBeLessThanOrEqual(1 - GraphLayout.inset + 1e-9)
    }
    const points = [...one.values()]
    for (let i = 0; i < points.length; i += 1) for (let j = i + 1; j < points.length; j += 1) expect(Math.hypot(points[i]!.x - points[j]!.x, points[i]!.y - points[j]!.y)).toBeGreaterThan(0.05)
    expect(GraphLayout.unit(['only'], [])).toEqual(new Map([['only', { x: 0.5, y: 0.5 }]]))
    expect(GraphLayout.unit([], []).size).toBe(0)
  })

  test("SplitMix64 is the Mac's generator, bit for bit: its first draws from the layout's seed", () => {
    // Computed apart, in Python's unbounded integers masked to 64 bits, as GraphLayout.swift's `next()` and `unit()` compute them.
    expect(GraphLayout.draws(0x5eedn, 3)).toEqual([0.038848734697185194, 0.3328011087394298, 0.3646818563781382])
  })

  test('the guardrail: a planted 40-node document places with no card over another, every one inside the 820 x 520 canvas', () => {
    const { nodes, edges } = planted(40)
    const placed = GraphLayout.place(nodes, edges)
    expect(placed.size).toBe(40)
    expect(problems(placed)).toEqual([])
    expect(GraphLayout.place(nodes, edges), 'the same document, the same picture').toEqual(placed)
  })

  test('the separation pass is what keeps them apart: the Mac’s layout scaled alone overlaps 40 cards', () => {
    const { nodes, edges } = planted(40)
    const scaled = GraphLayout.scaled(GraphLayout.unit(nodes, edges))
    expect(problems(scaled).length).toBeGreaterThan(0)
  })

  test('every size up to 40 places clean, and a small picture keeps the Mac’s shape where nothing overlaps', () => {
    for (let count = 1; count <= 40; count += 1) {
      const { nodes, edges } = planted(count)
      expect(problems(GraphLayout.place(nodes, edges)), `${count} nodes`).toEqual([])
    }
    const small = planted(3)
    const scaled = GraphLayout.scaled(GraphLayout.unit(small.nodes, small.edges))
    if (problems(scaled).length === 0) expect(GraphLayout.place(small.nodes, small.edges)).toEqual(scaled)
  })

  test("an edge runs from its source card's right middle to its target card's left middle, as the old canvas draws it, and between the facing sides where the layout puts the target elsewhere", () => {
    expect(GraphLayout.edge({ x: 30, y: 230 }, { x: 230, y: 60 })).toEqual({ x1: 190, y1: 252, x2: 230, y2: 82 })
    expect(GraphLayout.edge({ x: 616, y: 438 }, { x: 444, y: 438 }), 'a target to the left: left middle to right middle').toEqual({ x1: 616, y1: 460, x2: 604, y2: 460 })
    expect(GraphLayout.edge({ x: 53, y: 216 }, { x: 53, y: 409 }), 'a target below: bottom middle to top middle').toEqual({ x1: 133, y1: 260, x2: 133, y2: 409 })
    expect(GraphLayout.edge({ x: 53, y: 216 }, { x: 100, y: 38 }), 'a target above: top middle to bottom middle').toEqual({ x1: 133, y1: 216, x2: 180, y2: 82 })
  })
})
