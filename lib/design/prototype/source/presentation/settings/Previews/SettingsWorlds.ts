import checkCaptionsSkipped from '@recorded/business/reads/Fixtures/check-captions-skipped.json?raw'
import suitesModelsStatus from '@recorded/business/reads/Fixtures/suites-models-status.json?raw'
import allowedHostAdd from '@recorded/business/reads/Fixtures/allowed-host-add.txt?raw'
import allowedHostDelete from '@recorded/business/reads/Fixtures/allowed-host-delete.txt?raw'
import allowedHosts from '@recorded/business/reads/Fixtures/allowed-hosts.txt?raw'
import configGetServerTools from '@recorded/business/reads/Fixtures/config-get-server-tools.txt?raw'
import configGetServerToolsNone from '@recorded/business/reads/Fixtures/config-get-server-tools-none.txt?raw'
import mediaDefaultVideo from '@recorded/business/reads/Fixtures/media-default-video.txt?raw'
import modelsStoredInfoVoice from '@recorded/business/reads/Fixtures/models-stored-info-voice.json?raw'
import providersAddBackendOllama from '@recorded/business/reads/Fixtures/providers-add-backend-ollama.txt?raw'
import providersAuthList from '@recorded/business/reads/Fixtures/providers-auth-list.json?raw'
import providersRegisterClaude from '@recorded/business/reads/Fixtures/providers-register-claude.json?raw'
import providersRegisterCodex from '@recorded/business/reads/Fixtures/providers-register-codex.json?raw'
import providersRegisterGemini from '@recorded/business/reads/Fixtures/providers-register-gemini.json?raw'
import providersRegisterOllamaNone from '@recorded/business/reads/Fixtures/providers-register-ollama-none.json?raw'
import providersScan from '@recorded/business/reads/Fixtures/providers-scan.json?raw'
import storeInfoKindVideo from '@recorded/business/reads/Fixtures/store-info-kind-video.json?raw'
import storeInfoKindVoice from '@recorded/business/reads/Fixtures/store-info-kind-voice.json?raw'
import xcodeAcpRegistration from '@recorded/business/reads/Fixtures/xcode-acp-registration.json?raw'
import xcodeCheck from '@recorded/business/reads/Fixtures/xcode-check.json?raw'
import xcodeConfigDelete from '@recorded/business/reads/Fixtures/xcode-config-delete.txt?raw'
import xcodeConfigGetNone from '@recorded/business/reads/Fixtures/xcode-config-get-none.err?raw'
import xcodeMcpCreate from '@recorded/business/reads/Fixtures/xcode-mcp-create.txt?raw'
import xcodeMcpPresets from '@recorded/business/reads/Fixtures/xcode-mcp-presets.json?raw'
import checkFixFailed from '../../../../fixtures/business/jobs/check-fix-failed-handmade.txt?raw'
import checkFix from '../../../../fixtures/business/jobs/check-fix-handmade.txt?raw'
import allowedHostDeleteApple from '../../../../fixtures/business/reads/allowed-host-delete-handmade.txt?raw'
import chatsRecall from '../../../../fixtures/business/reads/chats-recall-handmade.json?raw'
import authAdd from '../../../../fixtures/business/reads/auth-add-handmade.txt?raw'
import authClear from '../../../../fixtures/business/reads/auth-clear-handmade.txt?raw'
import checkGithubFailing from '../../../../fixtures/business/reads/check-github-failing-handmade.json?raw'
import configGetMemoryRecall from '../../../../fixtures/business/reads/config-get-memory-recall.txt?raw'
import configGetPermissions from '../../../../fixtures/business/reads/config-get-permissions-handmade.txt?raw'
import configGetSearchUrlUnset from '../../../../fixtures/business/reads/config-get-search-url-unset.txt?raw'
import configGetSearchUrl from '../../../../fixtures/business/reads/config-get-search-url.txt?raw'
import configSetDefaultVideo from '../../../../fixtures/business/reads/config-set-default-video-handmade.txt?raw'
import configSet from '../../../../fixtures/business/reads/config-set-handmade.txt?raw'
import configSetPermission from '../../../../fixtures/business/reads/config-set-permission-handmade.txt?raw'
import modelsListSettings from '../../../../fixtures/business/reads/models-list-settings-handmade.json?raw'
import preferences from '../../../../fixtures/presentation/settings/preferences-handmade.json?raw'
import type { Rule, Rules } from '../../../infrastructure/process/StandInChild'
import type { PassedOver, SettingsPreferences } from '../ViewModels/SettingsViewModel'

/**
 * Settings' stand-in answers (design 1i), by world -- the Mac app's
 * `SettingsPreview` runners, over its recordings, and hand-made files in the
 * CLI's shape where none was recorded:
 * - `default`: `check` as `check-captions-skipped.json` recorded it (0
 *   failing, 2 to look at), the providers the scan found, no key stored
 *   (`providers-auth-list.json`), the web search address the old prototype
 *   set (`config-get-search-url.txt`, captured), the hosts and the default
 *   video recorded, the `xcode` preset unregistered;
 * - `nosearch`: the same with the address unset, as a fresh config answers it
 *   (`config-get-search-url-unset.txt`, captured);
 * - `fixfailed`: `check` with an MCP server whose command is not on PATH,
 *   which `check --fix` cannot mend, so the fix exits 1;
 * - `xcode`: `check` as GUI 5a's sandbox recorded it, with its Xcode section
 *   (`xcode-check.json`, `apogee-dev` over `/Users/someone/.apogee-dev`).
 */
export type SettingsWorld = 'default' | 'nosearch' | 'fixfailed' | 'xcode'

const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })

/** One rule a line of a hand-made file of one-line answers, each line its own call's answer; `exact` where the line echoes what was typed. */
function lineRules(text: string, args: (line: string) => readonly string[] | undefined, exact = false): Rule[] {
  return text
    .split('\n')
    .filter((line) => line !== '')
    .flatMap((line) => {
      const call = args(line)
      return call ? [{ arguments: call, answer: { stdout: `${line}\n` }, ...(exact ? { exact: true } : {}) }] : []
    })
}

/** The media sets the settings world's `models list` lists beside the sessions' backends, each its recorded `models info` (a voice model's names its voices). */
export const mediaSets = { video: 'Wan2.2-TI2V-5B/mlx/5e6f708192a3', mlxVoice: 'Kokoro-82M/mlx/6f708192a3b4', voice: 'Kokoro-82M/voice/5f3a2c91d0e4' } as const

/** What Settings reads and runs on every preset: the default world, but `models list`, which each preset's own world answers. */
export function settingsPlaceRules(): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  return {
    reads: [
      read(['auth', 'list'], providersAuthList),
      read(['providers', 'scan'], providersScan),
      read(['providers', 'register', 'claude'], providersRegisterClaude),
      read(['providers', 'register', 'codex'], providersRegisterCodex),
      read(['providers', 'register', 'gemini'], providersRegisterGemini),
      read(['providers', 'register', 'ollama'], providersRegisterOllamaNone),
      read(['check'], checkCaptionsSkipped),
      read(['mcp', 'presets'], xcodeMcpPresets),
      read(['acp', 'registration'], xcodeAcpRegistration),
      read(['models', 'info', mediaSets.video], storeInfoKindVideo),
      read(['models', 'info', mediaSets.mlxVoice], storeInfoKindVoice),
      read(['models', 'info', mediaSets.voice], modelsStoredInfoVoice),
    ],
    commands: [
      read(['config', 'get', 'permissions'], configGetPermissions),
      read(['config', 'get', 'tools.search.url'], configGetSearchUrl),
      read(['config', 'get', 'memory.recall'], configGetMemoryRecall),
      read(['config', 'get', 'tools.allowed_hosts'], allowedHosts),
      read(['config', 'get', 'models.default_video'], mediaDefaultVideo),
      // Each API backend's consent: the sessions' claude, as the recording consents; chat:pr's list's claude-api and gpt, as GUI 4k's sandbox recorded them.
      read(['config', 'get', 'backends.claude.server_tools'], configGetServerTools),
      read(['config', 'get', 'backends.claude-api.server_tools'], configGetServerTools),
      read(['config', 'get', 'backends.gpt.server_tools'], configGetServerToolsNone),
      // The xcode preset not in the config: the CLI's refusal of the key.
      { arguments: ['config', 'get', 'mcp_servers.xcode'], answer: { stderr: xcodeConfigGetNone, exit: 1 } },
      read(['mcp', 'create', 'xcode', '--from', 'xcode'], xcodeMcpCreate),
      read(['config', 'delete-mcp-server', 'xcode'], xcodeConfigDelete),
      read(['config', 'add-allowed-host', 'docs.python.org'], allowedHostAdd),
      read(['config', 'delete-allowed-host', 'docs.python.org'], allowedHostDelete),
      read(['config', 'delete-allowed-host', 'developer.apple.com'], allowedHostDeleteApple),
      // Ollama's model named: the CLI's own add-backend, which names no model in what it prints.
      read(['config', 'add-backend', 'ollama', '--type', 'ollama-cli', '--model'], providersAddBackendOllama),
      ...lineRules(authAdd, (line) => {
        const provider = /^stored a key for (\S+) in /.exec(line)?.[1]
        return provider === undefined ? undefined : ['auth', 'add', provider, '--stdin']
      }),
      ...lineRules(authClear, (line) => {
        const provider = /^cleared the stored key for (\S+)$/.exec(line)?.[1]
        return provider === undefined ? undefined : ['auth', 'clear', provider]
      }),
      ...lineRules(configSetPermission, (line) => {
        const match = /^permissions\.(\S+) = (\S+)$/.exec(line)
        return match ? ['config', 'set-permission', match[1] ?? '', match[2] ?? ''] : undefined
      }),
      ...lineRules(
        configSet,
        (line) => {
          const match = /^(\S+) = (.+)$/.exec(line)
          return match ? ['config', 'set', match[1] ?? '', match[2] ?? ''] : undefined
        },
        true,
      ),
      ...lineRules(
        configSetDefaultVideo,
        (line) => {
          const value = /^models\.default_video = (.+)$/.exec(line)?.[1]
          return value === undefined ? undefined : ['config', 'set-default-video', value === '(none)' ? 'off' : value]
        },
        true,
      ),
    ],
    jobs: [read(['check', '--fix'], checkFix)],
  }
}

/** A world's answers: every preset's, with the world's own after them (the later of two as long wins). */
export function settingsWorld(world: SettingsWorld = 'default'): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  const place = settingsPlaceRules()
  const reads: Rule[] = [...place.reads, read(['models', 'list'], modelsListSettings)]
  const commands: Rule[] = [...place.commands]
  const jobs: Rule[] = [...place.jobs]
  if (world === 'nosearch') commands.push(read(['config', 'get', 'tools.search.url'], configGetSearchUrlUnset))
  if (world === 'xcode') reads.push(read(['check'], xcodeCheck))
  if (world === 'fixfailed') {
    reads.push(read(['check'], checkGithubFailing))
    jobs.push({ arguments: ['check', '--fix'], answer: { stdout: checkFixFailed, exit: 1 } })
  }
  return { reads, commands, jobs }
}

/**
 * A preview's answers: a world's, with the two reads every preset's other
 * places answer for Settings (the roles' `models status`, the recorded one
 * the Sessions and Models places read, and `chats recall`, the library's
 * hand-made listing), named here since a preview of `settings` may import
 * neither place.
 */
export function settingsPreviewRules(world: SettingsWorld = 'default'): Rules {
  const own = settingsWorld(world)
  return { ...own, reads: [read(['models', 'status'], suitesModelsStatus), read(['chats', 'recall'], chatsRecall), ...own.reads] }
}

/** `rules` with a world's beside them, the world's after, so its `models list` answers where both have one. */
export function withSettingsWorld(rules: Rules, world: SettingsWorld = 'default'): Rules {
  const settings = settingsWorld(world)
  return { ...rules, reads: [...(rules.reads ?? []), ...settings.reads], commands: [...(rules.commands ?? []), ...settings.commands], jobs: [...(rules.jobs ?? []), ...settings.jobs] }
}

/** The lines `check --fix` prints in a world: how many ticks its run plays, one a tick. */
export function fixLines(world: SettingsWorld = 'default'): number {
  return (world === 'fixfailed' ? checkFixFailed : checkFix).split('\n').filter((line, index, all) => index < all.length - 1 || line !== '').length
}

/**
 * The app's own state the presets start from, as the old prototype's
 * constructor holds it (`main.html:2515-2665` at `b375fac9`) -- what the Mac
 * keeps in its `Preferences`, never the CLI's: the session defaults, the
 * media defaults, the binary the person pointed the app at and the reason
 * discovery passed it over, in the words `BinaryDiscoveryTests.swift` gives
 * it. Hand-made, `fixtures/presentation/settings/preferences-handmade.json`.
 */
export const settingsPreferences: SettingsPreferences & { readonly passedOver: PassedOver } = JSON.parse(preferences) as SettingsPreferences & { readonly passedOver: PassedOver }
