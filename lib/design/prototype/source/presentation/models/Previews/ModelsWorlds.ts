import configGetAdaptersNone from '@recorded/business/reads/Fixtures/config-get-adapters-none.txt?raw'
import configScan from '@recorded/business/reads/Fixtures/config-scan.txt?raw'
import completeGemma12 from '@recorded/business/reads/Fixtures/drafts-complete-Gemma4-12B-4bit.txt?raw'
import completeGemmaE4B from '@recorded/business/reads/Fixtures/drafts-complete-Gemma4-E4B-4bit.txt?raw'
import completeQwen from '@recorded/business/reads/Fixtures/drafts-complete-Qwen3-8B-4bit.txt?raw'
import infoBackendGemma12 from '@recorded/business/reads/Fixtures/drafts-info-backend-Gemma4-12B-4bit.json?raw'
import infoBackendGemmaE4B from '@recorded/business/reads/Fixtures/drafts-info-backend-Gemma4-E4B-4bit.json?raw'
import infoBackendQwen from '@recorded/business/reads/Fixtures/drafts-info-backend-Qwen3-8B-4bit.json?raw'
import infoDrafter from '@recorded/business/reads/Fixtures/drafts-info-drafter.json?raw'
import infoPathGemma12 from '@recorded/business/reads/Fixtures/drafts-info-path-Gemma4-12B-4bit.json?raw'
import infoPathGemmaE4B from '@recorded/business/reads/Fixtures/drafts-info-path-Gemma4-E4B-4bit.json?raw'
import infoPathQwen from '@recorded/business/reads/Fixtures/drafts-info-path-Qwen3-8B-4bit.json?raw'
import draftsPair from '@recorded/business/reads/Fixtures/drafts-pair.txt?raw'
import draftsUnpair from '@recorded/business/reads/Fixtures/drafts-unpair.txt?raw'
import modelsInfoClaude from '@recorded/business/reads/Fixtures/models-info-switched-claude.json?raw'
import storedInfoVoice from '@recorded/business/reads/Fixtures/models-stored-info-voice.json?raw'
import quantizeAddBackendLlama from '@recorded/business/reads/Fixtures/quantize-add-backend-llama.txt?raw'
import storeInfoBackend from '@recorded/business/reads/Fixtures/store-info-backend.json?raw'
import storeInfoDiffusion from '@recorded/business/reads/Fixtures/store-info-diffusion.json?raw'
import storeInfoDraftHead from '@recorded/business/reads/Fixtures/store-info-draft-head.json?raw'
import storeInfoF16 from '@recorded/business/reads/Fixtures/store-info-f16.json?raw'
import storeInfoMlx from '@recorded/business/reads/Fixtures/store-info-mlx.json?raw'
import storeInfoQ4 from '@recorded/business/reads/Fixtures/store-info-q4.json?raw'
import storeInfoSnapshot from '@recorded/business/reads/Fixtures/store-info-snapshot.json?raw'
import storeInfoConverts from '@recorded/business/reads/Fixtures/store-info-snapshot-converts.json?raw'
import suitesInfoHelper from '@recorded/business/reads/Fixtures/suites-models-info-helper.json?raw'
import suitesInfoRoot from '@recorded/business/reads/Fixtures/suites-models-info-root.json?raw'
import suitesInfoScribe from '@recorded/business/reads/Fixtures/suites-models-info-scribe.json?raw'
import suitesInfoVast from '@recorded/business/reads/Fixtures/suites-models-info-vast.json?raw'
import suitesModelsStatus from '@recorded/business/reads/Fixtures/suites-models-status.json?raw'
import pullLlama from '../../../../fixtures/business/jobs/models-pull-llama-handmade.jsonl?raw'
import configAddBackend from '../../../../fixtures/business/reads/config-add-backend-handmade.txt?raw'
import configDeleteBackend from '../../../../fixtures/business/reads/config-delete-backend-handmade.txt?raw'
import configScanRegister from '../../../../fixtures/business/reads/config-scan-register-handmade.txt?raw'
import configSetBackendAdapters from '../../../../fixtures/business/reads/config-set-backend-adapters-handmade.txt?raw'
import configSetRole from '../../../../fixtures/business/reads/config-set-role-handmade.txt?raw'
import deleteAndBackendsModel from '../../../../fixtures/business/reads/delete-and-backends-llama-model-handmade.txt?raw'
import deleteAndBackendsQ4 from '../../../../fixtures/business/reads/delete-and-backends-llama-q4-handmade.txt?raw'
import deleteKeptModel from '../../../../fixtures/business/reads/delete-kept-llama-model-handmade.txt?raw'
import deleteKeptQ4 from '../../../../fixtures/business/reads/delete-kept-llama-q4-handmade.txt?raw'
import deletePlanModel from '../../../../fixtures/business/reads/delete-plan-llama-model-handmade.txt?raw'
import deletePlanQ4 from '../../../../fixtures/business/reads/delete-plan-llama-q4-handmade.txt?raw'
import convertLlama from '../../../../fixtures/business/reads/models-convert-llama-handmade.txt?raw'
import infoBase from '../../../../fixtures/business/reads/models-info-base-handmade.json?raw'
import infoVast from '../../../../fixtures/business/reads/models-info-vast-handmade.json?raw'
import listAllStore from '../../../../fixtures/business/reads/models-list-all-store-handmade.json?raw'
import listConversions from '../../../../fixtures/business/reads/models-list-conversions-handmade.json?raw'
import listFresh from '../../../../fixtures/business/reads/models-list-fresh-handmade.json?raw'
import listStore from '../../../../fixtures/business/reads/models-list-store-handmade.json?raw'
import quantizeLlama from '../../../../fixtures/business/reads/models-quantize-llama-handmade.txt?raw'
import type { Rule, Rules } from '../../../infrastructure/process/StandInChild'

/**
 * The Models place's stand-in answers (design 1h), one store a world, every
 * answer a recording or a `*-handmade*` file in the CLI's shape:
 *
 * - `store`: the Mac app's store recordings (`store-list.json`: Llama 3.2 1B
 *   converted, its Q4 registered as `llama3.2-1b-Q4KM`, Gemma 4 26B as MLX,
 *   Kokoro) -- `models`, `models:roles`, `models:register`, `models:delete`
 *   and `models:notran`;
 * - `conversions`: the snapshots before any conversion
 *   (`store-list-conversions.json`) -- `models:store`, `models:convert` and
 *   `models:downloads`, whose conversion lands the store world's F16 and Q4;
 * - `fresh`: the same without the Llama snapshot -- `download`, whose pull
 *   lands its three sets, and whose Resume from … after its snapshot runs
 *   the conversion its chain names.
 *
 * Each is joined with the sessions' configured backends
 * (`models-list-kinds-handmade.json`, so the sidebar's glyphs stay) and the
 * drafters' recordings (`drafts-list.json`): the hand-made lists are those
 * recorded rows, merged in the CLI's order.
 */
export type ModelsWorld = 'store' | 'conversions' | 'fresh'

export const handles = {
  llamaSnapshot: 'Llama3.2-1B-Instruct/safetensors/edaba290ce7f',
  llamaF16: 'Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41',
  llamaQ4: 'Llama3.2-1B-Instruct/gguf/8d51e6c04a27',
  gemmaMlx: 'Gemma4-26B-Instruct/mlx/a88b9f386aff',
} as const

const models = '/Users/someone/.apogee/models'
const paths = {
  base: `${models}/base/gguf/dddddddddddd/base-Q4_K_M.gguf`,
  vast: `${models}/vast/gguf/eeeeeeeeeeee/vast-F16.gguf`,
  llamaF16: `${models}/Llama3.2-1B-Instruct/gguf/3f0c2a9b7e41/Llama3.2-1B-Instruct-F16.gguf`,
  llamaQ4: `${models}/Llama3.2-1B-Instruct/gguf/8d51e6c04a27/Llama3.2-1B-Instruct-Q4KM.gguf`,
  gemma12: `${models}/Gemma4-12B/mlx/7a8b9c0d1e2f`,
  gemmaE4B: `${models}/Gemma4-E4B/mlx/2b3c4d5e6f70`,
  qwen: `${models}/Qwen3-8B/mlx/1a2b3c4d5e6f`,
}

/** The pull the `download` preset's sheet starts, as the Downloads view-model runs it. */
export const llamaPull = ['models', 'pull', 'meta-llama/Llama-3.2-1B-Instruct', '--safetensors', '--register-with', 'Q4_K_M', '--output-format', 'stream-json']

/** The conversion `models:downloads` runs and `models:convert`'s sheet starts. */
export const llamaConversion = ['models', 'convert', handles.llamaSnapshot, '--register-with', 'Q4_K_M']

/** The quantization `models:notran` stopped before it ran, and its Run again. */
export const llamaQuantization = ['models', 'quantize', handles.llamaF16]

const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })
const info = (name: string, stdout: string): Rule => read(['models', 'info', name], stdout)

/** One rule a line of a hand-made file of one-line answers: each line its own call's answer. */
function lineRules(text: string, args: (line: string) => readonly string[] | undefined): Rule[] {
  return text
    .split('\n')
    .filter((line) => line !== '')
    .flatMap((line) => {
      const call = args(line)
      return call ? [{ arguments: call, answer: { stdout: `${line}\n` } }] : []
    })
}

/** The roles' verbs, each `models.<field> = <name>` line answering `config set-default…`. */
function roleRules(): Rule[] {
  return lineRules(configSetRole, (line) => {
    const match = /^models\.(\S+) = (\S+)$/.exec(line)
    if (!match) return undefined
    const field = match[1] ?? ''
    return ['config', field === 'default' ? 'set-default' : `set-${field.replaceAll('_', '-')}`, match[2] ?? '']
  })
}

/**
 * `config delete-backend <b>`: each `removed backend` line and the
 * `models.<field> cleared: it named '<b>'` lines after it, one answer -- the
 * CLI's `remove_backend` clears every role pointer that names the entry, each
 * said (`config_cmd.cpp`, M33; the shape `delete-backend-clears.txt` records).
 */
function deleteBackendRules(): Rule[] {
  const rules: Rule[] = []
  for (const line of configDeleteBackend.split('\n').filter((text) => text !== '')) {
    const name = /^removed backend '([^']+)'/.exec(line)?.[1]
    const last = rules.at(-1)
    if (name !== undefined) rules.push(read(['config', 'delete-backend', name], `${line}\n`))
    else if (last && 'stdout' in last.answer) rules[rules.length - 1] = { ...last, answer: { stdout: `${last.answer.stdout}${line}\n` } }
  }
  return rules
}

/** `config set-backend <b> --adapters …`: set for any list, cleared for `""`. */
function adapterRules(): Rule[] {
  return lineRules(configSetBackendAdapters, (line) => {
    const match = /^edited backend '([^']+)' in \S+: (set|cleared) adapters/.exec(line)
    if (!match) return undefined
    const backend = match[1] ?? ''
    return match[2] === 'cleared' ? ['config', 'set-backend', backend, '--adapters', ''] : ['config', 'set-backend', backend, '--adapters']
  })
}

/** Every configured llama.cpp or mlx backend of the worlds: each's adapters read none (`config-get-adapters-none.txt`). */
const adapterBackends = ['Gemma4-12B-4bit', 'Gemma4-E4B-4bit', 'Qwen3-8B-4bit', 'helper', 'llama3.2-1b-Q4KM', 'root', 'scribe', 'vast']

/** What every world answers: the sessions' backends, the drafters, their sets, and the place's commands. */
function shared(): Required<Rules> {
  return {
    reads: [
      // The roles, as every preset's Sessions place reads them too (design 1g's library rules).
      read(['models', 'status'], suitesModelsStatus),
      info('root', suitesInfoRoot),
      info('helper', suitesInfoHelper),
      info('scribe', suitesInfoScribe),
      info('vast', suitesInfoVast),
      info('claude', modelsInfoClaude),
      info(paths.base, infoBase),
      info('base/gguf/dddddddddddd', infoBase),
      info(paths.vast, infoVast),
      info('vast/gguf/eeeeeeeeeeee', infoVast),
      info('Gemma4-12B-4bit', infoBackendGemma12),
      info('Gemma4-E4B-4bit', infoBackendGemmaE4B),
      info('Qwen3-8B-4bit', infoBackendQwen),
      info(paths.gemma12, infoPathGemma12),
      info(paths.gemmaE4B, infoPathGemmaE4B),
      info(paths.qwen, infoPathQwen),
      info('Gemma4-12B/mlx/7a8b9c0d1e2f', infoPathGemma12),
      info('Gemma4-E4B/mlx/2b3c4d5e6f70', infoPathGemmaE4B),
      info('Qwen3-8B/mlx/1a2b3c4d5e6f', infoPathQwen),
      info('Gemma4-E4B-Assistant/mlx/4d5e6f708192', infoDrafter),
      // What a conversion of the Llama snapshot names, by its files and by its handles.
      info(paths.llamaF16, storeInfoF16),
      info(paths.llamaQ4, storeInfoQ4),
      info(handles.llamaF16, storeInfoF16),
      info(handles.llamaQ4, storeInfoQ4),
    ],
    commands: [
      ...adapterBackends.map((backend) => read(['config', 'get', `backends.${backend}.adapters`], configGetAdaptersNone)),
      read(['__complete', 'config', 'set-backend', 'Gemma4-12B-4bit', '--draft', ''], completeGemma12),
      read(['__complete', 'config', 'set-backend', 'Gemma4-E4B-4bit', '--draft', ''], completeGemmaE4B),
      read(['__complete', 'config', 'set-backend', 'Qwen3-8B-4bit', '--draft', ''], completeQwen),
      read(['config', 'set-backend', 'Gemma4-E4B-4bit', '--draft'], draftsPair),
      read(['config', 'set-backend', 'Gemma4-E4B-4bit', '--draft', ''], draftsUnpair),
      ...adapterRules(),
      ...roleRules(),
      ...deleteBackendRules(),
      ...lineRules(configAddBackend, (line) => {
        const name = /^added backend '([^']+)'/.exec(line)?.[1]
        return name === undefined ? undefined : ['config', 'add-backend', name]
      }),
      read(['config', 'add-backend', 'Llama3.2-1B-Instruct-Q4KM'], quantizeAddBackendLlama),
    ],
    jobs: [],
    chats: [],
  }
}

/** The store world's own: its lists, its sets, the deletes of the Llama Q4 and of the whole model, the scan. */
function store(): Rules {
  const base = shared()
  return {
    reads: [
      ...base.reads,
      read(['models', 'list'], listStore),
      read(['models', 'list', '--all'], listAllStore),
      info('llama3.2-1b-Q4KM', storeInfoBackend),
      info(handles.gemmaMlx, storeInfoMlx),
      info('Kokoro-82M/voice/5f3a2c91d0e4', storedInfoVoice),
      info(handles.llamaSnapshot, storeInfoSnapshot),
    ],
    commands: [
      ...base.commands,
      read(['config', 'scan'], configScan),
      read(['config', 'scan', '--register'], configScanRegister),
      ...[handles.llamaQ4, 'llama3.2-1b-Q4KM'].flatMap((name) => [
        read(['models', 'delete', name], deletePlanQ4),
        read(['models', 'delete', name, '--yes'], deleteKeptQ4),
        read(['models', 'delete', name, '--yes', '--and-backends'], deleteAndBackendsQ4),
      ]),
      read(['models', 'delete', 'Llama3.2-1B-Instruct'], deletePlanModel),
      read(['models', 'delete', 'Llama3.2-1B-Instruct', '--yes'], deleteKeptModel),
      read(['models', 'delete', 'Llama3.2-1B-Instruct', '--yes', '--and-backends'], deleteAndBackendsModel),
    ],
    jobs: [{ arguments: llamaQuantization, exact: true, answer: { stdout: quantizeLlama } }],
  }
}

/** The snapshots before any conversion: the Llama conversion, recorded as the CLI words it. */
function conversions(list = listConversions): Rules {
  const base = shared()
  return {
    reads: [
      ...base.reads,
      read(['models', 'list'], list),
      info(handles.llamaSnapshot, storeInfoConverts),
      info('Gemma4-12B-Assistant/safetensors/43b42c6289a0', storeInfoDraftHead),
      info('Wan-AI--Wan2.2-TI2V-5B/safetensors/6ef9b00b8b83', storeInfoDiffusion),
    ],
    commands: base.commands,
    jobs: [{ arguments: llamaConversion, exact: true, answer: { stdout: convertLlama } }],
  }
}

/**
 * The place's reads and commands on every other preset (design 1h): the
 * sessions' backends' sets, their adapters, the roles and the role verbs,
 * so Models opens whole from any screen over the preset's own `models list`.
 */
export function modelsPlaceRules(): Rules {
  const { reads, commands } = shared()
  return { reads, commands }
}

/** A world's rules. */
export function modelsWorld(world: ModelsWorld): Rules {
  switch (world) {
    case 'store':
      return store()
    case 'conversions':
      return conversions()
    case 'fresh': {
      // The pull, and the conversion its chain names for resuming it once its snapshot is stored (`models-convert-llama-handmade.txt`, that command's own output).
      const rules = conversions(listFresh)
      return { ...rules, jobs: [...(rules.jobs ?? []), { arguments: llamaPull, exact: true, answer: { stdout: pullLlama } }] }
    }
  }
}

/** `rules` with a world's beside them, the world's later, so its `models list` answers the session's read too. */
export function withModelsWorld(rules: Rules, world: ModelsWorld): Rules {
  const models = modelsWorld(world)
  return {
    ...rules,
    reads: [...(rules.reads ?? []), ...(models.reads ?? [])],
    commands: [...(rules.commands ?? []), ...(models.commands ?? [])],
    jobs: [...(rules.jobs ?? []), ...(models.jobs ?? [])],
  }
}
