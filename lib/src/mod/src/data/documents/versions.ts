// `apogee train versions --output-format json` (machine-mode.md; byte for byte
// GET /v1/admin/training/versions, http-api.md): every backend's version
// ledger, each version as the document lists it -- its weights (a GGUF file,
// an MLX directory, or an adapter over its pinned base), when it was
// promoted, its gate, and whether retention pruned it.
import type { ApogeeLedger, ApogeeTrainVersions, ApogeeVersionEntry } from '../../../types'
import { decode } from './document'
import type { Decoded, Reader } from './document'

/** One promoted version (the contract's). */
export type VersionEntry = ApogeeVersionEntry

/** One backend's ledger. */
export type Ledger = ApogeeLedger

/** Every ledger, in the document's order. */
export type TrainVersions = ApogeeTrainVersions

/** The fields a version names its weights by, and what each one is. */
const WEIGHTS = [
  ['gguf_path', 'gguf'],
  ['mlx_path', 'mlx'],
  ['adapter_path', 'adapter'],
] as const

/** `train versions`' document, read tolerantly. */
export function decodeTrainVersions(text: string): Decoded<TrainVersions> {
  return decode(text, top => ({ ledgers: top.list('data', ledgerOf) }))
}

function ledgerOf(ledger: Reader): Ledger {
  const backend = ledger.text('backend_name')
  const activeVersion = ledger.number('active_version')
  const versions = ledger.list('versions', versionOf)

  return { backend, ...(activeVersion === undefined ? {} : { activeVersion }), versions }
}

function versionOf(entry: Reader): VersionEntry {
  const version = entry.number('version')
  const runId = entry.text('run_id')
  const weights = weightsOf(entry)
  const promotedAt = entry.text('promoted_at')
  const baseSha256 = entry.optionalText('base_sha256')
  const evalScore = entry.optionalNumber('eval_score')
  const evalPassed = entry.optionalFlag('eval_passed')
  const evalScoredOn = entry.optionalText('eval_scored_on')
  const prunedAt = entry.optionalText('pruned_at')
  const fusedPath = entry.optionalText('fused_path')

  return {
    ...(version === undefined ? {} : { version }),
    runId,
    ...(weights === undefined ? {} : { weights }),
    ...(baseSha256 === undefined ? {} : { baseSha256 }),
    promotedAt,
    ...(evalScore === undefined ? {} : { evalScore }),
    ...(evalPassed === undefined ? {} : { evalPassed }),
    ...(evalScoredOn === undefined ? {} : { evalScoredOn }),
    ...(prunedAt === undefined ? {} : { prunedAt }),
    ...(fusedPath === undefined ? {} : { fusedPath }),
  }
}

/** The weights the version names, the first of its three fields it carries; none said as lacking all three. */
function weightsOf(entry: Reader): VersionEntry['weights'] {
  for (const [key, kind] of WEIGHTS) {
    const path = entry.optionalText(key)
    if (path !== undefined) return { kind, path }
  }
  entry.lack(WEIGHTS.map(([key]) => key).join('|'))

  return undefined
}
