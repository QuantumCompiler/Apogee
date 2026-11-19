import modelsInfoClaude from '@recorded/business/reads/Fixtures/models-info-switched-claude.json?raw'
import suitesConfigGet from '@recorded/business/reads/Fixtures/suites-config-get.txt?raw'
import suitesDefaultSuite from '@recorded/business/reads/Fixtures/suites-default-suite.txt?raw'
import suitesModelsInfoRoot from '@recorded/business/reads/Fixtures/suites-models-info-root.json?raw'
import suitesModelsList from '@recorded/business/reads/Fixtures/suites-models-list.json?raw'
import statusCloud from '@recorded/business/reads/Fixtures/suites-models-status-cloud.json?raw'
import statusFast from '@recorded/business/reads/Fixtures/suites-models-status-fast.json?raw'
import statusPair from '@recorded/business/reads/Fixtures/suites-models-status-pair.json?raw'
import statusShared from '@recorded/business/reads/Fixtures/suites-models-status-shared.json?raw'
import statusVast from '@recorded/business/reads/Fixtures/suites-models-status-vast.json?raw'
import symphoniesList from '@recorded/business/reads/Fixtures/symphonies-list.json?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import privateUnsent from '@recorded/data/protocol/Fixtures/private-unsent.jsonl?raw'
import executePlay from '../../../../fixtures/data/protocol/execute-play-summarize-verify.jsonl?raw'
import { ChatLaunch } from '../../../business/session/ChatLaunch'
import type { Preset } from '../../../infrastructure/process/Preset'
import type { ApogeeMode, Rule } from '../../../infrastructure/process/StandInChild'
import type { AttachChoice, FolderListing } from '../ViewModels/SessionViewModel'

/**
 * The session's four plain start presets (design 1u), on the CLI's own
 * recordings: the old prototype's `chat:new`, `chat:private`, `chat:play`
 * and `chat:suite` (`applyStart`, `main.html:2722-2733` at `b375fac9`). What
 * each opens -- the launch, the play's line, the chooser -- is the
 * composition's (`AppViewModel.applyStart`); these are the stand-in's rules
 * that answer it.
 */

/** How a start preset is asked for: whether it stands still, and the `apogee` mode. */
export interface PresetOptions {
  readonly still: boolean
  readonly apogee: ApogeeMode
}

/** A private chat's launch, as the app builds it: what "Private session instead" opens. */
const privateLaunch = ChatLaunch.arguments({ privately: true })

/**
 * The model facts a session's header reads (until 1g's library reads them):
 * `models list` -- each backend's kind, the menu's groups -- and `models
 * info` for the model the `session` event names, its window.
 */
function modelReads(model: 'claude' | 'root'): Rule[] {
  return [
    { arguments: ['models', 'list'], answer: { stdout: suitesModelsList } },
    { arguments: ['models', 'info', model], answer: { stdout: model === 'claude' ? modelsInfoClaude : suitesModelsInfoRoot } },
  ]
}

/**
 * What the suite chooser reads, wherever a chat preset offers it (Choose a
 * suite, the header's suite, `/suite`): `config get suites` and
 * `models.default_suite`, and each recorded suite's footprint (`models status
 * --suite <name>`). The default is also read when a new or private chat
 * opens, so the header names the suite it runs under.
 */
function suiteRules(): { readonly reads: Rule[]; readonly commands: Rule[] } {
  const footprints: Record<string, string> = { cloud: statusCloud, fast: statusFast, pair: statusPair, shared: statusShared, vast: statusVast }
  return {
    reads: Object.entries(footprints).map(([name, stdout]): Rule => ({ arguments: ['models', 'status', '--suite', name], answer: { stdout } })),
    commands: [
      { arguments: ['config', 'get', 'suites'], answer: { stdout: suitesConfigGet } },
      { arguments: ['config', 'get', 'models.default_suite'], answer: { stdout: suitesDefaultSuite } },
    ],
  }
}

/** `chat:new`: a new chat, unsent -- the empty state. "Private session instead" opens a private one. */
export function chatNewPreset({ still, apogee }: PresetOptions): Preset {
  const suites = suiteRules()
  return {
    rules: {
      chats: [
        { arguments: ['chat'], answer: { stdout: newUnsent } },
        { arguments: privateLaunch, answer: { stdout: privateUnsent } },
      ],
      reads: [...modelReads('claude'), ...suites.reads],
      commands: suites.commands,
    },
    route: 'chat:new',
    clock: { start: 2, still },
    apogee,
  }
}

/** `chat:private`: a private chat, unsent -- the private empty state, the CLI's notice under it. */
export function chatPrivatePreset({ still, apogee }: PresetOptions): Preset {
  const suites = suiteRules()
  return {
    rules: { chats: [{ arguments: privateLaunch, answer: { stdout: privateUnsent } }], reads: [...modelReads('claude'), ...suites.reads], commands: suites.commands },
    route: 'chat:private',
    clock: { start: 3, still },
    apogee,
  }
}

/**
 * `chat:play`: an `apogee execute` session under the suite `coding`, its one
 * turn a `/play summarize-verify`, captured from the branch build on two
 * mock members (`npm run capture -- execute-play-summarize-verify`). Its 13
 * lines play within the start ticks, so the frame is its finished turn.
 * `symphonies list` names what `/play ` offers.
 */
export function chatPlayPreset({ still, apogee }: PresetOptions): Preset {
  const suites = suiteRules()
  return {
    rules: {
      chats: [{ arguments: ['execute'], answer: { stdout: executePlay } }],
      reads: [...modelReads('root'), { arguments: ['symphonies', 'list'], answer: { stdout: symphoniesList } }, ...suites.reads],
      commands: suites.commands,
    },
    route: 'chat:play',
    clock: { start: 15, still },
    apogee,
  }
}

/**
 * `chat:suite`: a new chat with the suite chooser open on `vast`, the
 * recorded suite the CLI prices over this machine's memory (the old frame's
 * `big`): `config get suites`, the default, and each suite's footprint.
 * "Private session instead", the chooser cancelled, opens a private one.
 */
export function chatSuitePreset({ still, apogee }: PresetOptions): Preset {
  const suites = suiteRules()
  return {
    rules: {
      chats: [
        { arguments: ['chat'], answer: { stdout: newUnsent } },
        { arguments: privateLaunch, answer: { stdout: privateUnsent } },
      ],
      reads: [...modelReads('claude'), ...suites.reads],
      commands: suites.commands,
    },
    route: 'chat:suite',
    clock: { start: 2, still },
    apogee,
  }
}

/**
 * What `@` and the attach menu offer: the old prototype's `~/code/ingest`
 * listing (`repoFiles()`, `main.html:3846` at `b375fac9`) and its attach
 * menu's samples (`main.html:5234`). A browser reads no folder, so these are
 * handed in where the Mac app's `Mentions.candidates` reads one: the one
 * listing in the session that is not the CLI's.
 */
export const sessionSamples: { readonly listing: FolderListing; readonly attachChoices: readonly AttachChoice[] } = {
  listing: {
    folder: '~/code/ingest',
    files: [
      { path: 'src/ingest/runner.py', size: '4.2 KB' },
      { path: 'src/ingest/fetch.py', size: '2.1 KB' },
      { path: 'src/ingest/config.py', size: '1.6 KB' },
      { path: 'src/ingest/retry.py', size: '0.9 KB' },
      { path: 'src/ingest/parsers/feed.py', size: '3.3 KB' },
      { path: 'src/upload/uploader.py', size: '5.8 KB' },
      { path: 'tests/test_runner.py', size: '2.8 KB' },
      { path: 'scripts/run_local.sh', size: '0.6 KB' },
      { path: 'config.toml', size: '0.4 KB' },
      { path: 'README.md', size: '5.1 KB' },
    ],
  },
  attachChoices: [
    { label: 'File…', hint: '⌘O', path: 'notes/wal-design.md' },
    { label: 'Folder…', hint: '⇧⌘O', path: '~/code/ingest/' },
    { label: 'PDF…', hint: '', path: 'storage-engine-design.pdf' },
    { label: 'Image…', hint: '', path: 'crash-0912.png' },
    { label: 'Audio…', hint: '', path: 'standup-2026-10-09.m4a' },
    { label: 'Video…', hint: '', path: 'repro-crash.mov' },
  ],
}
