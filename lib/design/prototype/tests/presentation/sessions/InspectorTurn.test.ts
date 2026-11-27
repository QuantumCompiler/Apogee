import { describe, expect, test } from 'vitest'
import type { SessionEvent } from '../../../source/business/session/ChatSession'
import { EventDecoder } from '../../../source/data/protocol/EventDecoder'
import { InspectorTurn } from '../../../source/presentation/sessions/ViewModels/InspectorTurn'

/** A machine-mode line, as the session hands it on. */
const event = (line: string): SessionEvent => ({ case: 'event', event: EventDecoder.decode(line) })
const session = (nextTurn: number) => event(`{"model":"root","next_turn":${nextTurn},"protocol_version":1,"type":"session"}`)
const helper = (text: string, turn: number) => event(`{"text":"utility — ${text}","turn":${turn},"type":"tool_status"}`)

/** `events` folded from nothing, as the library folds the open session's. */
const fold = (events: readonly SessionEvent[]) => events.reduce<InspectorTurn | undefined>((latest, next) => InspectorTurn.apply(latest, next), undefined)

describe("THIS TURN, folded from the session's events (design 1g)", () => {
  test('a later turn starts it afresh; an earlier one is passed over', () => {
    const latest = fold([session(1), helper('one', 1), helper('two', 2), helper('late', 1)])
    expect(latest).toMatchObject({ turn: 2, helpers: ['utility — two'] })
  })

  test('a child whose session line numbers its turns from the one held starts it afresh: an unsaved conversation started again numbers from 1', () => {
    expect(InspectorTurn.apply(fold([session(1), helper('one', 1)]), session(1))).toBeUndefined()
    expect(InspectorTurn.apply(fold([session(1), helper('one', 1), helper('two', 2)]), session(1)), 'from below the turn held').toBeUndefined()
  })

  test('a resumed child that numbers on keeps the turn before, until its own turn speaks', () => {
    const held = fold([session(1), helper('one', 1), helper('two', 2)])
    expect(InspectorTurn.apply(held, session(3))).toBe(held)
    expect(fold([session(1), helper('two', 2), session(3), helper('three', 3)])).toMatchObject({ turn: 3, helpers: ['utility — three'] })
  })
})
