import storeListJson from '@recorded/business/reads/Fixtures/store-list.json?raw'
import rowsDraftsList from '@recorded/business/reads/Fixtures/rows-drafts-list.json?raw'
import storeInfoQ4 from '@recorded/business/reads/Fixtures/store-info-q4.json?raw'
import storeInfoConverts from '@recorded/business/reads/Fixtures/store-info-snapshot-converts.json?raw'
import storeInfoDraftHead from '@recorded/business/reads/Fixtures/store-info-draft-head.json?raw'
import suitesModelsStatus from '@recorded/business/reads/Fixtures/suites-models-status.json?raw'
import modelsInfoClaude from '@recorded/business/reads/Fixtures/models-info-switched-claude.json?raw'
import modelsInfoLocal from '@recorded/business/reads/Fixtures/models-info-switched-local.json?raw'
import suitesModelsInfoRoot from '@recorded/business/reads/Fixtures/suites-models-info-root.json?raw'
import suitesModelsList from '@recorded/business/reads/Fixtures/suites-models-list.json?raw'
import { describe, expect, test } from 'vitest'
import { Field, MLXKind, ModelInfo, ModelKind, ModelRow, RoleAssignment } from '../../../source/business/reads/Models'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'

const document = (text: string) => {
  const read = ReadDocument.from(text)
  if (!read) throw new Error('a recorded document')
  return read
}

describe("the model facts, as models list and models info state them (the Mac app's ModelKind, ModelRow)", () => {
  test("each backend's kind from its type: local, api, subscription, or none for a type that is none of them (the mock)", () => {
    expect(ModelKind.of('llamacpp')).toBe('local')
    expect(ModelKind.of('mlx')).toBe('local')
    expect(ModelKind.of('anthropic')).toBe('api')
    expect(ModelKind.of('claude-cli')).toBe('subscription')
    expect(ModelKind.of('mock')).toBeUndefined()
    expect(ModelKind.byBackend(document(suitesModelsList))).toEqual({ claude: 'api', helper: 'local', root: 'local', scribe: 'local', vast: 'local' })
  })

  test("suites-models-list.json's rows, in its order, and the backends a chat runs on", () => {
    const rows = ModelRow.list(document(suitesModelsList))
    expect(rows.map((row) => row.backend)).toEqual(['claude', 'fasthelper', 'fastroot', 'helper', 'plain', 'root', 'scribe', 'vast'])
    expect(rows[0]).toMatchObject({ backendType: 'anthropic', model: 'claude-sonnet-4-5', state: 'no key', group: 'anthropic', section: 'cloud' })
    expect(rows[0]?.note).toBe("no API key found; run 'apogee auth add anthropic'")
    expect(ModelRow.chatBackends(rows)).toEqual(['claude', 'fasthelper', 'fastroot', 'helper', 'plain', 'root', 'scribe', 'vast'])
  })

  test("a model's window: the leading count of its window field, and none where models info states none (an API model's) -- never guessed", () => {
    expect(ModelInfo.window(document(suitesModelsInfoRoot))).toBe(8192)
    expect(ModelInfo.window(document(modelsInfoLocal))).toBe(8192)
    expect(ModelInfo.window(document(modelsInfoClaude))).toBeUndefined()
    expect(ModelInfo.window(document('{"data":{"window":{"tokens":32768}}}'))).toBe(32768)
    expect(Field.list(document(modelsInfoClaude)).find((field) => field.label === 'type')?.value).toBe('anthropic')
  })
})

describe("the store's rows and documents (design 1h: the Mac's ModelRow, MLXKind, ModelInfo, RoleAssignment)", () => {
  test("store-list.json: stored weights are named by their handle, a backend by its name; each row's format, precision, roles and the list's fold", () => {
    const rows = ModelRow.list(document(storeListJson))
    expect(rows.map((row) => [row.name, ModelRow.isStored(row)])).toEqual([
      ['Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41', true],
      ['Gemma4-26B-Instruct/mlx/a88b9f386aff', true],
      ['Kokoro-82M/voice/5f3a2c91d0e4', true],
      ['llama3.2-1b-Q4KM', false],
    ])
    expect(rows[1]).toMatchObject({ format: 'mlx', quant: 'mixed 4/8-bit (group 64)', source: 'converted', roles: '' })
    expect(ModelRow.folded(document(storeListJson))).toBe(2)
  })

  test("an MLX row's kind as the CLI writes it, a drafter's and a voice model's chatting nothing; a row's stated drafter", () => {
    const rows = ModelRow.list(document(rowsDraftsList))
    expect(rows.map((row) => [row.name, row.mlxKind, ModelRow.chats(row)])).toEqual([
      ['Gemma4-E4B-Assistant/mlx/4d5e6f708192', 'drafter', false],
      ['Gemma4-12B-4bit', 'vision', true],
      ['Gemma4-E4B-4bit', 'vision', true],
      ['Qwen3-8B-4bit', 'chat', true],
    ])
    expect(rows[1]?.draft).toEqual({ path: '/Users/someone/.apogee/models/Gemma4-12B-Assistant/mlx/8192a3b4c5d6', canDraft: false, refusal: "there is no drafter at /Users/someone/.apogee/models/Gemma4-12B-Assistant/mlx/8192a3b4c5d6 -- 'apogee models convert <a draft head> --mlx' makes one" })
    expect(rows[2]?.draft).toEqual({ path: '/Users/someone/.apogee/models/Gemma4-E4B-Assistant/mlx/4d5e6f708192', canDraft: true, refusal: undefined })
    expect(MLXKind.registers('embedding')).toBe(true)
    expect(MLXKind.registers('drafter')).toBe(false)
    expect(MLXKind.reading(undefined)).toBe('chat')
  })

  test("store-info-q4.json: its typed facts -- format, handle, bytes, the cache, the window, the lineage -- and the memory a conversation holds, never an estimate", () => {
    const info = ModelInfo.read(document(storeInfoQ4))
    expect(info.name, 'asked by its file, as the recording was').toBe('/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf')
    expect(info.data).toMatchObject({ format: 'gguf', handle: 'Llama3.2-1B-Instruct/gguf/8d51e6c04a27', bytes: 807694988, cache: { bytes: 570425344, type: 'q8_0' } })
    expect(info.data?.window).toEqual({ tokens: 32768, source: 'default', trained: 131072 })
    expect(info.data?.lineage.map((step) => [step.kind, step.from])).toEqual([
      ['quantized', 'Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41'],
      ['converted', 'Llama3.2-1B-Instruct/safetensors/edaba290ce7f'],
      ['pulled', 'meta-llama/Llama-3.2-1B-Instruct'],
    ])
    expect(ModelInfo.memory(info)).toBe(807694988 + 570425344)
    expect(ModelInfo.field(info, 'model_path')).toBe('/Users/someone/.apogee/models/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf')
  })

  test('a snapshot that converts, and one that converts to nothing; a backend’s document states no typed facts', () => {
    expect(ModelInfo.read(document(storeInfoConverts)).data?.convertsTo).toEqual(['gguf', 'mlx'])
    expect(ModelInfo.read(document(storeInfoDraftHead)).data?.convertsTo).toEqual([])
    const backend = ModelInfo.read(document(suitesModelsInfoRoot))
    expect(backend.data).toBeUndefined()
    expect(ModelInfo.memory(backend)).toBeUndefined()
  })

  test("suites-models-status.json: the six roles the CLI resolves, never its suite or footprint, and the suite they resolve under", () => {
    const roles = RoleAssignment.list(document(suitesModelsStatus))
    expect(roles.map((role) => [role.role, RoleAssignment.backend(role), RoleAssignment.isSet(role)])).toEqual([
      ['chat', 'fastroot', false],
      ['embedding', 'plain', false],
      ['extraction', 'plain', false],
      ['vision', undefined, false],
      ['transcription', undefined, false],
      ['utility', 'fasthelper', false],
    ])
    expect(RoleAssignment.suite(document(suitesModelsStatus))).toBe('fast')
    expect(RoleAssignment.verb({ role: 'chat', said: '' })).toEqual(['config', 'set-default'])
    expect(RoleAssignment.verb({ role: 'vision', said: '' })).toEqual(['config', 'set-default-vision'])
    expect(RoleAssignment.isSet({ role: 'chat', said: 'root' })).toBe(true)
  })
})
