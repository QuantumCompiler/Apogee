// The CLI's documents read tolerantly (the client invariant; the CLI's ADR 0006
// consumed): the recorded documents decode, an unknown field is ignored, a
// missing one is said, and a document that is not one is a stated condition,
// never a throw.
import { expect, test } from 'claude-code/testing'

import { decodeCheck } from '../../../src/data/documents/check'
import { decodeWholeValue } from '../../../src/data/documents/config'
import { decodeModelsList, decodeModelsStatus } from '../../../src/data/documents/models'
import { decodeTrainStatus } from '../../../src/data/documents/training'
import {
  CHECK_ADAPTERS,
  MODELS_LIST_ADAPTERS,
  MODELS_LIST_KINDS_REGISTERED,
  MODELS_LIST_SERVER_TOOLS,
  MODELS_STATUS_CHAT,
  MODELS_STATUS_UNSET,
  TRAIN_STATUS_CYCLE_ACTIVE,
  TRAIN_STATUS_NONE,
  TRAIN_STATUS_PIPELINE_RUNNING,
} from '../../support/recorded'

test('the check report decodes every row and its counts', () => {
  const decoded = decodeCheck(CHECK_ADAPTERS)

  expect(decoded.kind).toBe('read')
  if (decoded.kind !== 'read') return
  expect(decoded.value.rows.length).toBe(105)
  expect(decoded.value.rows[0]).toEqual({ section: 'Version', name: 'apogee', status: 'ok', detail: '0.2.0' })
  expect(decoded.value.rows.filter(row => row.status === 'fail').length).toBe(3)
  expect(decoded.value.rows.find(row => row.name === 'backend: based')?.remedy).toBe(
    're-download the model to /Users/someone/.apogee/models/tiny/gguf/bbbbbbbbbbbb/tiny-F16.gguf',
  )
  expect([decoded.value.isOk, decoded.value.failures, decoded.value.warnings]).toEqual([false, 3, 0])
  expect(decoded.missing).toEqual([])
})

test('a check row missing its fields is kept with what it has, the missing ones said', () => {
  const decoded = decodeCheck('{"rows":[{"section":"Config","name":"config.json","later_field":7},{"status":"warn"}],"ok":true}')

  expect(decoded).toEqual({
    kind: 'read',
    value: {
      rows: [
        { section: 'Config', name: 'config.json', status: '', detail: '' },
        { section: '', name: '', status: 'warn', detail: '' },
      ],
      isOk: true,
    },
    missing: ['rows[0].status', 'rows[0].detail', 'rows[1].section', 'rows[1].name', 'rows[1].detail', 'failures', 'warnings'],
  })
})

test('what is not a document is unreadable, said with what it began with', () => {
  expect(decodeCheck('error: no config\n')).toEqual({ kind: 'unreadable', reason: 'not a JSON document: "error: no config"' })
  expect(decodeCheck('[1, 2]')).toEqual({ kind: 'unreadable', reason: 'not a JSON object: "[1, 2]"' })
  expect(decodeCheck('')).toEqual({ kind: 'unreadable', reason: 'an empty document' })
})

test('models list keeps each row with its backend, its type, its section and its state', () => {
  const decoded = decodeModelsList(MODELS_LIST_ADAPTERS)

  expect(decoded.kind).toBe('read')
  if (decoded.kind !== 'read') return
  expect(decoded.value.rows.map(row => [row.backend, row.backendType, row.section, row.state])).toEqual([
    ['(not configured)', '-', 'local', 'safetensors'],
    ['based', 'llamacpp', 'local', 'unreadable'],
    ['q4', 'llamacpp', 'local', 'ok'],
    ['tuned', 'llamacpp', 'local', 'ok'],
  ])
  expect(decoded.value.rows[1]?.note).toBe('header runs past the end of the file (truncated or corrupt)')
  expect(decoded.missing).toEqual([])
})

test("models list keeps an MLX row's kind, and a row the CLI gives none has none, unsaid", () => {
  const kinds = decodeModelsList(MODELS_LIST_KINDS_REGISTERED)
  const adapters = decodeModelsList(MODELS_LIST_ADAPTERS)

  expect(kinds.kind === 'read' && kinds.value.rows.map(row => [row.backend, row.kind])).toEqual([
    ['(not configured)', 'drafter'],
    ['(not configured)', 'voice'],
    ['(not configured)', 'music'],
    ['(not configured)', 'video'],
    ['EmbeddingGemma-300M-bf16', 'embedding'],
    ['Gemma4-E4B-4bit', 'vision'],
    ['Qwen3-8B-4bit', 'chat'],
  ])
  expect(adapters.kind === 'read' && adapters.value.rows.some(row => 'kind' in row)).toBe(false)
  expect(adapters.kind === 'read' && adapters.missing).toEqual([])
})

test('models list reads cloud rows as cloud', () => {
  const decoded = decodeModelsList(MODELS_LIST_SERVER_TOOLS)

  expect(decoded.kind === 'read' && decoded.value.rows.slice(0, 2).map(row => [row.backend, row.backendType, row.section])).toEqual([
    ['claude', 'claude-cli', 'cloud'],
    ['claude-api', 'anthropic', 'cloud'],
  ])
})

test('models list without its data says so and reads as no rows', () => {
  expect(decodeModelsList('{"object":"list"}')).toEqual({ kind: 'read', value: { rows: [] }, missing: ['data'] })
})

test('models status keeps its fields in order, each label and value', () => {
  const decoded = decodeModelsStatus(MODELS_STATUS_CHAT)

  expect(decoded.kind === 'read' && decoded.value.fields.slice(0, 2)).toEqual([
    { field: 'chat', value: 'EmbeddingGemma-300M-bf16' },
    { field: 'embedding', value: 'EmbeddingGemma-300M-bf16   (via models.default)' },
  ])
  expect(decodeModelsStatus(MODELS_STATUS_UNSET).kind).toBe('read')
})

test('train status reads the runs, the versions, the pipeline and the cycle', () => {
  const decoded = decodeTrainStatus(TRAIN_STATUS_CYCLE_ACTIVE)

  expect(decoded.kind).toBe('read')
  if (decoded.kind !== 'read') return
  expect(decoded.value.versions).toEqual([{ backend: 'nightly-model', activeVersion: 1, kept: 1, total: 1 }])
  expect(decoded.value.running).toEqual(['cycle-20261026-074121-s0'])
  expect([decoded.value.activePipeline, decoded.value.isCycleActive, decoded.value.runs, decoded.value.pipelines]).toEqual([
    'cycle-20261026-074121',
    true,
    expect.any(Number),
    7,
  ])
  expect(decoded.value.cycle).toEqual({ backend: 'nightly-model', isHalted: false, haltReason: '' })
  expect(decoded.value.latestPipeline).toEqual({ id: 'cycle-20261026-074121', specName: 'nightly', status: 'running', stages: 1, passed: 0 })
  expect(decoded.missing).toEqual([])
})

test('train status with nothing trained reads as nothing, and a running pipeline as running', () => {
  const none = decodeTrainStatus(TRAIN_STATUS_NONE)
  expect(none.kind === 'read' && [none.value.running, none.value.versions, none.value.activePipeline, none.value.cycle]).toEqual([
    [],
    [],
    undefined,
    undefined,
  ])
  const pipeline = decodeTrainStatus(TRAIN_STATUS_PIPELINE_RUNNING)
  expect(pipeline.kind === 'read' && [pipeline.value.activePipeline, pipeline.value.running, pipeline.value.recentRuns[0]?.status]).toEqual([
    'pipe-20261026-074116',
    ['pipe-20261026-074116-s1'],
    'running',
  ])
})

test('train status missing its fields says which, and reads them as unknown', () => {
  const decoded = decodeTrainStatus('{"versions":[{"backend":"q4"}],"newer":{"x":1}}')

  expect(decoded).toEqual({
    kind: 'read',
    value: { running: [], recentRuns: [], versions: [{ backend: 'q4' }] },
    missing: ['running', 'recent_runs', 'versions[0].active_version', 'active_pipeline', 'cycle_active'],
  })
})

test("a config value read as a whole number: config get's bare line, an unset key's empty line none, anything else unreadable", () => {
  // `apogee config get backends.<name>.max_tokens` prints the value and a
  // newline, an unset one an empty line (config_cmd.cpp: lookup, whole); as
  // recorded for another key at config-get-adapters-none.txt.
  expect(decodeWholeValue('200\n')).toEqual({ kind: 'read', value: 200, missing: [] })
  expect(decodeWholeValue('-5\n')).toEqual({ kind: 'read', value: -5, missing: [] })
  expect(decodeWholeValue('\n')).toEqual({ kind: 'read', value: undefined, missing: [] })
  expect(decodeWholeValue('two hundred\n')).toEqual({ kind: 'unreadable', reason: 'not a whole number: "two hundred"' })
})
