import modelsInfoClaude from '@recorded/business/reads/Fixtures/models-info-switched-claude.json?raw'
import modelsInfoLocal from '@recorded/business/reads/Fixtures/models-info-switched-local.json?raw'
import suitesModelsInfoRoot from '@recorded/business/reads/Fixtures/suites-models-info-root.json?raw'
import suitesModelsList from '@recorded/business/reads/Fixtures/suites-models-list.json?raw'
import { describe, expect, test } from 'vitest'
import { Field, ModelInfo, ModelKind, ModelRow } from '../../../source/business/reads/Models'
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
