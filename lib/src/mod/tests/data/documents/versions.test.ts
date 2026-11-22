// `train versions` read tolerantly: each ledger as the document lists it, in
// its order, each version with the fields it carries and none it does not;
// an empty ledger list is none, and a missing field is said.
import { expect, test } from 'claude-code/testing'

import { decodeTrainVersions } from '../../../src/data/documents/versions'
import { TRAIN_VERSIONS_EMPTY_HANDMADE, TRAIN_VERSIONS_TWO_HANDMADE } from '../../support/handmade'
import { TRAIN_VERSIONS_ADAPTERS } from '../../support/recorded'

test('the recorded ledgers read in their order, an adapter version with its base pin', () => {
  const decoded = decodeTrainVersions(TRAIN_VERSIONS_ADAPTERS)

  expect(decoded.kind).toBe('read')
  if (decoded.kind !== 'read') return
  expect(decoded.missing).toEqual([])
  expect(decoded.value.ledgers.map(ledger => [ledger.backend, ledger.activeVersion])).toEqual([
    ['based', 1],
    ['q4', 1],
    ['tuned', 1],
  ])
  expect(decoded.value.ledgers[0]?.versions).toEqual([
    {
      version: 1,
      runId: '20261026-065637',
      weights: { kind: 'adapter', path: '/Users/someone/.apogee/training/versions/based/v1.lora.gguf' },
      baseSha256: 'ce5065b7f3dc1bcd98592fa57a11f0e9670f16f33f21abf043dd7c903ad0c98c',
      promotedAt: '2026-10-26T06:56:37Z',
      evalScore: 1,
      evalPassed: true,
    },
  ])
  expect(decoded.value.ledgers[1]?.versions[0]?.evalScoredOn).toBe('tiny-Q4_K_M.gguf (Q4_K_M)')
  expect(decoded.value.ledgers[2]?.versions[0]?.weights).toEqual({ kind: 'gguf', path: '/Users/someone/.apogee/models/tiny/gguf/cf368ccf9b45/tuned-v1.gguf' })
})

test('an MLX version, a pruned one and one with no eval keep only what they carry', () => {
  const decoded = decodeTrainVersions(TRAIN_VERSIONS_TWO_HANDMADE)

  expect(decoded.kind === 'read' && decoded.value.ledgers[1]).toEqual({
    backend: 'helper',
    activeVersion: 2,
    versions: [
      { version: 1, runId: '20261019-101500', weights: { kind: 'mlx', path: '/Users/someone/.apogee/models/helper/mlx/1a2b3c4d5e6f' }, promotedAt: '2026-10-19T10:30:00Z', prunedAt: '2026-10-20T09:00:00Z' },
      {
        version: 2,
        runId: '20261020-111500',
        weights: { kind: 'mlx', path: '/Users/someone/.apogee/models/helper/mlx/6f5e4d3c2b1a' },
        promotedAt: '2026-10-20T11:30:00Z',
        evalScore: 0.5,
        evalPassed: false,
        evalScoredOn: 'helper-Q4_K_M.gguf (Q4_K_M)',
      },
    ],
  })
})

test('no ledger reads as none, unsaid', () => {
  expect(decodeTrainVersions(TRAIN_VERSIONS_EMPTY_HANDMADE)).toEqual({ kind: 'read', value: { ledgers: [] }, missing: [] })
})

test('a ledger or a version missing its fields is kept with what it has, the missing ones said', () => {
  const decoded = decodeTrainVersions('{"data":[{"backend_name":"x","versions":[{"version":3,"extra":1},{"run_id":"r","promoted_at":"t","fused_path":"/f","gguf_path":"/g"}]},{"active_version":1}]}')

  expect(decoded).toEqual({
    kind: 'read',
    value: {
      ledgers: [
        {
          backend: 'x',
          versions: [{ version: 3, runId: '', promotedAt: '' }, { runId: 'r', weights: { kind: 'gguf', path: '/g' }, promotedAt: 't', fusedPath: '/f' }],
        },
        { backend: '', activeVersion: 1, versions: [] },
      ],
    },
    missing: [
      'data[0].active_version',
      'data[0].versions[0].run_id',
      'data[0].versions[0].gguf_path|mlx_path|adapter_path',
      'data[0].versions[0].promoted_at',
      'data[0].versions[1].version',
      'data[1].backend_name',
      'data[1].versions',
    ],
  })
})

test('a document without its data says so, and what is no document is unreadable', () => {
  expect(decodeTrainVersions('{"object":"list"}')).toEqual({ kind: 'read', value: { ledgers: [] }, missing: ['data'] })
  expect(decodeTrainVersions('error: no such command').kind).toBe('unreadable')
})
