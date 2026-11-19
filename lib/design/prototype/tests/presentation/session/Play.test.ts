import executePlay from '@recorded/data/protocol/Fixtures/execute-play.jsonl?raw'
import { describe, expect, test } from 'vitest'
import { EventDecoder } from '../../../source/data/protocol/EventDecoder'
import { LineFramer } from '../../../source/infrastructure/process/LineFramer'
import { PlayNarration } from '../../../source/presentation/session/ViewModels/Play'

/** The `tool_status` lines a recording carries, as the CLI wrote them. */
const statusLines = (recording: string) =>
  LineFramer.lines(recording).flatMap((line) => {
    const kind = EventDecoder.decode(line).kind
    return kind.case === 'toolStatus' ? [kind.text] : []
  })

describe("a play's narration, read from its stage lines (the Mac app's PlayNarration)", () => {
  test("execute-play.jsonl's two stages: each line kept whole, its index, count, stage, role and backend read", () => {
    const play = PlayNarration.of(statusLines(executePlay), '/play echo2 hello')
    expect(play?.symphony).toBe('echo2')
    expect(play?.stages.map((stage) => [stage.index, stage.count, stage.name, stage.role, stage.backend])).toEqual([
      [1, 2, 'first', 'utility', 'helper'],
      [2, 2, 'second', 'chat', 'root'],
    ])
    expect(play?.stages[0]?.line).toBe('stage 1/2 first — asking utility (helper): One: hello')
    expect(play?.summary(false)).toBe('played echo2 · 2 stages')
    expect(play?.summary(true)).toBe('playing echo2 · stage 2 of 2')
  })

  test("a chain's stage names where it sits, and the model's own choice names the symphony", () => {
    const play = PlayNarration.of(
      ['play — the model chose digest: 3 member calls, utility → chat → extraction', 'digest → summarize-verify, stage 1/2 summarize — asking utility (helper): Summarize…'],
      undefined,
    )
    expect(play?.symphony).toBe('digest')
    expect(play?.chosen).toBe('play — the model chose digest: 3 member calls, utility → chat → extraction')
    expect(play?.stages[0]?.chain).toBe('digest → summarize-verify')
  })

  test('lines that narrate no play are none', () => {
    expect(PlayNarration.of(['[tool] read_file notes.md'], '/play echo2 hello')).toBeUndefined()
    expect(PlayNarration.of([], undefined)).toBeUndefined()
  })
})
