// The reads of the `/apogee` set, each its command's own document, decoded.
import type { EngineInterface } from 'claude-code'

import { decodeCheck } from '../../data/documents/check'
import type { CheckReport } from '../../data/documents/check'
import { decodeModelsList } from '../../data/documents/models'
import type { ModelsList } from '../../data/documents/models'
import { decodeTrainStatus } from '../../data/documents/training'
import type { TrainStatus } from '../../data/documents/training'
import { readDocument } from '../client/client'
import type { FoundBinary, Read } from '../client/client'
import { READ_ARGS } from './plan'
import type { ReadName } from './plan'

/** A read of the set, done: which, and its document or why there is none. */
export type ReadDone =
  | { read: 'check'; document: Read<CheckReport> }
  | { read: 'models'; document: Read<ModelsList> }
  | { read: 'train status'; document: Read<TrainStatus> }

/** Runs one read of the set on the binary. */
export async function runRead($: EngineInterface, binary: FoundBinary, read: ReadName): Promise<ReadDone> {
  const args = READ_ARGS[read]
  if (read === 'check') return { read, document: await readDocument($, binary, args, decodeCheck) }
  if (read === 'models') return { read, document: await readDocument($, binary, args, decodeModelsList) }

  return { read, document: await readDocument($, binary, args, decodeTrainStatus) }
}
