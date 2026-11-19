import modelsListServerTools from '@recorded/business/reads/Fixtures/models-list-server-tools.json?raw'
import chatsShowKv from '../../../../fixtures/business/reads/chats-show-kv-handmade.json?raw'
import chatErrorRetry from '../../../../fixtures/data/protocol/chat-error-retry-handmade.jsonl?raw'
import chatErrorSwitch from '../../../../fixtures/data/protocol/chat-error-switch-handmade.jsonl?raw'
import chatError from '../../../../fixtures/data/protocol/chat-error-handmade.jsonl?raw'
import permissionAlways from '../../../../fixtures/data/protocol/chat-permission-always.jsonl?raw'
import permissionCancel from '../../../../fixtures/data/protocol/chat-permission-cancel.jsonl?raw'
import permissionNo from '../../../../fixtures/data/protocol/chat-permission-no.jsonl?raw'
import permissionSession from '../../../../fixtures/data/protocol/chat-permission-session.jsonl?raw'
import permissionYes from '../../../../fixtures/data/protocol/chat-permission-yes.jsonl?raw'
import questionBatch from '../../../../fixtures/data/protocol/chat-question-batch-handmade.jsonl?raw'
import questionCancel from '../../../../fixtures/data/protocol/chat-question-cancel.jsonl?raw'
import questionTwo from '../../../../fixtures/data/protocol/chat-question-two-handmade.jsonl?raw'
import questionHost from '../../../../fixtures/data/protocol/chat-question-host-handmade.jsonl?raw'
import questionOwn from '../../../../fixtures/data/protocol/chat-question-own-handmade.jsonl?raw'
import questionSkip from '../../../../fixtures/data/protocol/chat-question-skip-handmade.jsonl?raw'
import questionUpload from '../../../../fixtures/data/protocol/chat-question-upload-handmade.jsonl?raw'
import sourcesStream from '../../../../fixtures/data/protocol/chat-sources.jsonl?raw'
import streaming from '../../../../fixtures/data/protocol/chat-streaming-handmade.jsonl?raw'
import streamingCancel from '../../../../fixtures/data/protocol/chat-streaming-cancel.jsonl?raw'
import resumedKvEnded from '../../../../fixtures/data/protocol/resumed-kv-ended-handmade.jsonl?raw'
import resumedKv from '../../../../fixtures/data/protocol/resumed-kv-handmade.jsonl?raw'
import resumedKvHeld from '../../../../fixtures/data/protocol/resumed-kv-held-handmade.jsonl?raw'
import resumedKvTurn from '../../../../fixtures/data/protocol/resumed-kv-turn.jsonl?raw'
import resumedKvPair from '../../../../fixtures/data/protocol/resumed-kv-pair-handmade.jsonl?raw'
import type { Answer } from '../../../infrastructure/process/StandInChild'
import type { SessionPreviewOptions } from './SessionPreview'
import { kvChat, streamingFrames } from './SessionPresets'

/**
 * The session's scenarios and states as previews replay them (design 1v):
 * each scenario's recording, its rule's branches and frames, and the turns
 * its preset writes -- the same files `SessionPresets.ts` names, so a
 * preview draws exactly what its preset does.
 */
export const turns = {
  streaming: ['Where do fetches start in this repo, and what cancels them?', 'So one slow host takes the whole batch down. I want the other fetches to finish and just log the timeouts. What’s the idiomatic fix on 3.12?'],
  retry: 'Make `_fetch()` retry timeouts and connection errors, using `settings.fetch.retries` (it’s already in config.toml). Go ahead and edit the file.',
  wal: 'Why does group commit make the WAL cheaper under load? Point me to the pages.',
  crash: 'What’s this stack trace telling me?',
  pr: 'Review the retry-budget change in PR 482 before I merge it.',
  kv: 'Would a 16-bit cache fit at 128K?',
} as const

const permission: Answer = { stdout: permissionYes, continuations: { after: 'question', by: { yes: permissionYes, session: permissionSession, always: permissionAlways, no: permissionNo } }, cancelled: permissionCancel }
const question: Answer = {
  stdout: questionBatch,
  continuations: { after: 'question', by: { 'Per upload': questionUpload, 'Per batch': questionBatch, 'Per host': questionHost, 'Per endpoint, shared by every upload to it': questionOwn, '': questionSkip } },
  cancelled: questionCancel,
}
const error: Answer = { stdout: chatError, continuations: { after: 'error', by: { 'set model=claude': chatErrorSwitch, user: chatErrorRetry } } }

/** Each scenario's options, as its preset sets it up; a preview adds what the person does. */
export const scenario = {
  streaming: (frame: keyof typeof streamingFrames = 'chat'): [string, SessionPreviewOptions] => [
    streaming,
    { answers: [{ stdout: streaming, freeze: streamingFrames[frame], cancelled: streamingCancel }], before: (model) => model.attach('.'), sends: turns.streaming, ticks: 1 },
  ],
  permission: (): [string, SessionPreviewOptions] => [permissionYes, { answers: [permission], sends: [turns.retry], ticks: 60 }],
  sources: (): [string, SessionPreviewOptions] => [
    sourcesStream,
    {
      before: (model) => {
        model.apply({ model: 'claude' })
        model.attach('storage-engine-design.pdf')
        model.attach('postgresql.conf')
        model.citeTurn(1, 1)
      },
      sends: [turns.wal],
      ticks: 120,
    },
  ],
  error: (): [string, SessionPreviewOptions] => [chatError, { answers: [error], before: (model) => model.attach('crash-0912.png'), sends: [turns.crash], ticks: 10 }],
  question: (): [string, SessionPreviewOptions] => [questionBatch, { answers: [question], models: modelsListServerTools, sends: [turns.pr], ticks: 60 }],
  /** A question of two items, hand-made from the batch recording (`chat-question-two-handmade.jsonl`): what one answer line per item draws. */
  questionTwoItems: (): [string, SessionPreviewOptions] => [questionTwo, { answers: [{ stdout: questionTwo }], models: modelsListServerTools, sends: [turns.pr], ticks: 60 }],
  restarting: (): [string, SessionPreviewOptions] => [
    resumedKv,
    {
      launch: { resume: kvChat },
      earlier: chatsShowKv,
      answers: [{ stdout: resumedKv, exitAfter: 5 }, { stdout: resumedKvPair }],
      before: async (model) => {
        model.inspectorOpen = false
        model.presentSuites([{ name: 'pair', footprint: undefined, isDefault: false }], undefined, 'pair')
        await model.switchSuite()
        model.attach('storage-engine-design.pdf')
      },
      ticks: 3,
    },
  ],
  notSent: (): [string, SessionPreviewOptions] => [
    resumedKv,
    {
      launch: { resume: kvChat },
      earlier: chatsShowKv,
      answers: [{ stdout: resumedKv, exitAfter: 2 }, { stdout: resumedKvTurn }],
      before: async (model) => {
        model.inspectorOpen = false
        await model.restart()
      },
      sends: [turns.kv],
      ticks: 6,
    },
  ],
  ended: (): [string, SessionPreviewOptions] => [
    resumedKvEnded,
    {
      launch: { resume: kvChat },
      earlier: chatsShowKv,
      answers: [{ stdout: resumedKvEnded, exit: 2, stderr: 'loading\nfatal: the model went away\n' }, { stdout: resumedKv, continuations: { after: 'session', by: { 'set temperature=0.4': resumedKvHeld } } }],
      // A setting and a compact made while its turn ran: held for Start again once the child dies.
      before: (model) => {
        model.inspectorOpen = false
        model.writeAhead(turns.kv)
        model.apply({ temperature: 0.4 })
        model.compact()
      },
      ticks: 24,
    },
  ],
}
