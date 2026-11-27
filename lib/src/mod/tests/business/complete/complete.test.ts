// apogee_complete's decisions: which backends it may name (local and
// unmetered, or it is not offered), the bound, the one-shot's argv, and a
// request checked before anything runs.
import { expect, test } from 'claude-code/testing'

import { checkRequest, cliOffer, completeArgs, entryBound, servedOffer } from '../../../src/business/complete/complete'
import { decodeModelsList } from '../../../src/data/documents/models'
import type { ModelsList } from '../../../src/data/documents/models'
import { MODELS_LIST_MIXED_HANDMADE, SERVED_COMPLETE_TOOL_HANDMADE, servedCompleteToolHandmade } from '../../support/handmade'
import { MODELS_LIST_ADAPTERS, MODELS_LIST_KINDS_REGISTERED, MODELS_LIST_SERVER_TOOLS } from '../../support/recorded'

function list(text: string): ModelsList {
  const decoded = decodeModelsList(text)

  return decoded.kind === 'read' ? decoded.value : { rows: [] }
}

test('the CLI path offers each configured llama.cpp backend, at most 1024 tokens a call', () => {
  expect(cliOffer(list(MODELS_LIST_ADAPTERS))).toEqual({
    plumbing: 'cli',
    backends: [
      { name: 'based', maxTokens: 1024 },
      { name: 'q4', maxTokens: 1024 },
      { name: 'tuned', maxTokens: 1024 },
    ],
  })
})

test('the CLI path offers MLX too, once a backend, and never Ollama, a cloud backend or a stored set no backend runs', () => {
  expect(cliOffer(list(MODELS_LIST_MIXED_HANDMADE))).toEqual({ plumbing: 'cli', backends: [{ name: 'gemma-mlx', maxTokens: 1024 }] })
  expect(cliOffer(list(MODELS_LIST_SERVER_TOOLS))).toBeUndefined()
  expect(cliOffer({ rows: [] })).toBeUndefined()
})

test('the CLI path offers an MLX backend only when its kind chats (chat or vision), and a row of no kind as before kinds', () => {
  // As the macOS app reads a row (RowsThatChatTests): chat and vision chat, a
  // row with no kind chats, and every other word, one unknown included, does not.
  expect(cliOffer(list(MODELS_LIST_KINDS_REGISTERED))).toEqual({
    plumbing: 'cli',
    backends: [
      { name: 'Gemma4-E4B-4bit', maxTokens: 1024 },
      { name: 'Qwen3-8B-4bit', maxTokens: 1024 },
    ],
  })
  const kinded = (kind: string) => ({ rows: [{ backend: 'one', backendType: 'mlx', model: 'm', section: 'local', state: 'ok', kind }] })
  expect(cliOffer(kinded('embedding'))).toBeUndefined()
  expect(cliOffer(kinded('a word not known yet'))).toBeUndefined()
  expect(cliOffer(kinded('chat'))?.backends.map(one => one.name)).toEqual(['one'])
})

test("the served path offers the backends the server's complete names, each at the bound the server says it holds", () => {
  expect(servedOffer({ ...SERVED_COMPLETE_TOOL_HANDMADE, isReadOnly: false })).toEqual({ plumbing: 'served', backends: [{ name: 'q4', maxTokens: 1024 }] })
  expect(servedOffer({ name: 'complete', description: '', isReadOnly: false, inputSchema: { type: 'object' } })).toBeUndefined()
  // Two backends whose bounds differ: the schema's maximum is only the widest.
  const two = servedCompleteToolHandmade([
    { name: 'q4', bound: 256 },
    { name: 'big', bound: 1024 },
  ])
  expect(servedOffer({ ...two, isReadOnly: false })).toEqual({
    plumbing: 'served',
    backends: [
      { name: 'q4', maxTokens: 256 },
      { name: 'big', maxTokens: 1024 },
    ],
  })
})

test("a served backend whose bound the server's words do not say is held to the widest, said as its own bound unsaid", () => {
  expect(
    servedOffer({
      name: 'complete',
      description: 'backend may name: xtiny (at most 64 tokens).',
      isReadOnly: false,
      inputSchema: { type: 'object', properties: { backend: { enum: ['tiny', 7] }, max_tokens: { maximum: 256 } } },
    }),
  ).toEqual({ plumbing: 'served', backends: [{ name: 'tiny', maxTokens: 256, isBoundUnsaid: true }] })
  expect(
    servedOffer({ name: 'complete', description: '', isReadOnly: false, inputSchema: { type: 'object', properties: { backend: { enum: ['a.b'] } } } }),
  ).toEqual({ plumbing: 'served', backends: [{ name: 'a.b', maxTokens: 1024, isBoundUnsaid: true }] })
})

test("a CLI backend's bound is the served rule: 1024, or the entry's own max_tokens when that is lower", () => {
  // served_max_tokens (lib/src/cli/source/data/contracts/mcp_serve.cpp).
  expect([200, 1023, 1024, 4096, 0, -5, undefined].map(entryBound)).toEqual([200, 1023, 1024, 1024, 1024, 1024, 1024])
})

test("the one-shot's argv: the backend, the bound, retrieval off and the prompt after --, so a prompt never reads as a flag", () => {
  expect(completeArgs('q4', 1024, '--help me')).toEqual(['complete', '-m', 'q4', '-n', '1024', '--rag', '', '--', '--help me'])
})

test('a request is checked before anything runs: the prompt, a backend offered, a bound in range', () => {
  const offer = { plumbing: 'cli' as const, backends: [{ name: 'q4', maxTokens: 1024 }, { name: 'tuned', maxTokens: 512 }] }

  expect(checkRequest(offer, { prompt: 'Hi', backend: 'q4' })).toEqual({ kind: 'request', prompt: 'Hi', backend: 'q4', maxTokens: 1024 })
  expect(checkRequest(offer, { prompt: 'Hi', backend: 'tuned', max_tokens: 64 })).toEqual({ kind: 'request', prompt: 'Hi', backend: 'tuned', maxTokens: 64 })
  expect(checkRequest(offer, { backend: 'q4' })).toEqual({ kind: 'refused', reason: 'prompt is required: what the model is asked' })
  expect(checkRequest(offer, { prompt: 'Hi', backend: 'claude' })).toEqual({
    kind: 'refused',
    reason: "'claude' is not a backend apogee_complete runs on here: it may name q4, tuned",
  })
  expect(checkRequest(offer, { prompt: 'Hi', backend: 'tuned', max_tokens: 513 })).toEqual({
    kind: 'refused',
    reason: 'max_tokens must be a whole number from 1 to 512 for tuned',
  })
  expect(checkRequest(offer, { prompt: 'Hi', backend: 'q4', max_tokens: 0.5 }).kind).toBe('refused')
})

test('on the served path a bound left out is left to the server, which holds each backend to its own', () => {
  const offer = { plumbing: 'served' as const, backends: [{ name: 'q4', maxTokens: 256 }, { name: 'big', maxTokens: 1024 }] }

  expect(checkRequest(offer, { prompt: 'Hi', backend: 'q4' })).toEqual({ kind: 'request', prompt: 'Hi', backend: 'q4' })
  expect(checkRequest(offer, { prompt: 'Hi', backend: 'q4', max_tokens: 256 })).toEqual({ kind: 'request', prompt: 'Hi', backend: 'q4', maxTokens: 256 })
  expect(checkRequest(offer, { prompt: 'Hi', backend: 'q4', max_tokens: 257 })).toEqual({
    kind: 'refused',
    reason: 'max_tokens must be a whole number from 1 to 256 for q4',
  })
})
