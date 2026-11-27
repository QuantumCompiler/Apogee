import modelsInfoClaude from '@recorded/business/reads/Fixtures/models-info-switched-claude.json?raw'
import suitesConfigGet from '@recorded/business/reads/Fixtures/suites-config-get.txt?raw'
import suitesDefaultSuite from '@recorded/business/reads/Fixtures/suites-default-suite.txt?raw'
import suitesModelsInfoRoot from '@recorded/business/reads/Fixtures/suites-models-info-root.json?raw'
import statusCloud from '@recorded/business/reads/Fixtures/suites-models-status-cloud.json?raw'
import statusFast from '@recorded/business/reads/Fixtures/suites-models-status-fast.json?raw'
import statusPair from '@recorded/business/reads/Fixtures/suites-models-status-pair.json?raw'
import statusShared from '@recorded/business/reads/Fixtures/suites-models-status-shared.json?raw'
import statusVast from '@recorded/business/reads/Fixtures/suites-models-status-vast.json?raw'
import symphoniesList from '@recorded/business/reads/Fixtures/symphonies-list.json?raw'
import modelsListServerTools from '@recorded/business/reads/Fixtures/models-list-server-tools.json?raw'
import suitesModelsInfoVast from '@recorded/business/reads/Fixtures/suites-models-info-vast.json?raw'
import chatsCompact from '@recorded/business/reads/Fixtures/chats-compact.txt?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import privateUnsent from '@recorded/data/protocol/Fixtures/private-unsent.jsonl?raw'
import chatsShowKv from '../../../../fixtures/business/reads/chats-show-kv-handmade.json?raw'
import modelsListKinds from '../../../../fixtures/business/reads/models-list-kinds-handmade.json?raw'
import chatErrorRetry from '../../../../fixtures/data/protocol/chat-error-retry-handmade.jsonl?raw'
import chatErrorSwitch from '../../../../fixtures/data/protocol/chat-error-switch-handmade.jsonl?raw'
import chatError from '../../../../fixtures/data/protocol/chat-error-handmade.jsonl?raw'
import permissionAlways from '../../../../fixtures/data/protocol/chat-permission-always.jsonl?raw'
import permissionCancel from '../../../../fixtures/data/protocol/chat-permission-cancel.jsonl?raw'
import permissionNo from '../../../../fixtures/data/protocol/chat-permission-no.jsonl?raw'
import permissionSession from '../../../../fixtures/data/protocol/chat-permission-session.jsonl?raw'
import permissionYes from '../../../../fixtures/data/protocol/chat-permission-yes.jsonl?raw'
import questionBatch from '../../../../fixtures/data/protocol/chat-question-batch-handmade.jsonl?raw'
import questionCancel from '../../../../fixtures/data/protocol/chat-question-cancel.jsonl?raw'
import questionHost from '../../../../fixtures/data/protocol/chat-question-host-handmade.jsonl?raw'
import questionOwn from '../../../../fixtures/data/protocol/chat-question-own-handmade.jsonl?raw'
import questionSkip from '../../../../fixtures/data/protocol/chat-question-skip-handmade.jsonl?raw'
import questionUpload from '../../../../fixtures/data/protocol/chat-question-upload-handmade.jsonl?raw'
import sourcesStream from '../../../../fixtures/data/protocol/chat-sources.jsonl?raw'
import streaming from '../../../../fixtures/data/protocol/chat-streaming-handmade.jsonl?raw'
import streamingCancel from '../../../../fixtures/data/protocol/chat-streaming-cancel.jsonl?raw'
import executePlay from '../../../../fixtures/data/protocol/execute-play-summarize-verify.jsonl?raw'
import resumedKvEnded from '../../../../fixtures/data/protocol/resumed-kv-ended-handmade.jsonl?raw'
import resumedKv from '../../../../fixtures/data/protocol/resumed-kv-handmade.jsonl?raw'
import resumedKvHeld from '../../../../fixtures/data/protocol/resumed-kv-held-handmade.jsonl?raw'
import resumedKvTurn from '../../../../fixtures/data/protocol/resumed-kv-turn.jsonl?raw'
import resumedKvPair from '../../../../fixtures/data/protocol/resumed-kv-pair-handmade.jsonl?raw'
import { ChatLaunch } from '../../../business/session/ChatLaunch'
import { EventDecoder } from '../../../data/protocol/EventDecoder'
import { LineFramer } from '../../../infrastructure/process/LineFramer'
import type { Preset } from '../../../infrastructure/process/Preset'
import type { Answer, ApogeeMode, Freeze, Rule } from '../../../infrastructure/process/StandInChild'
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
 * The model facts a session's header reads, as 1g's library reads them:
 * `models list` -- each backend's kind, the menu's groups, the list's glyphs
 * -- and `models info` for the model the `session` event names, its window.
 * The list is `suites-models-list.json` with one subscription backend beside
 * it (`models-list-kinds-handmade.json`, design 1g), so the session list's
 * three glyphs each have a backend of their kind.
 */
function modelReads(...models: ReadonlyArray<'claude' | 'root' | 'vast'>): Rule[] {
  const info = { claude: modelsInfoClaude, root: suitesModelsInfoRoot, vast: suitesModelsInfoVast }
  return [{ arguments: ['models', 'list'], answer: { stdout: modelsListKinds } }, ...models.map((model): Rule => ({ arguments: ['models', 'info', model], answer: { stdout: info[model] } }))]
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

// MARK: - The scenarios and states (design 1v)

/** The chat a recording's `session` line names. */
function chatIdOf(recording: string): string {
  const first = EventDecoder.decode(LineFramer.lines(recording)[0] ?? '')
  return first.kind.case === 'session' ? (first.kind.session.chatID ?? '') : ''
}

/** The kv chat (`chats-show-kv-handmade.json`): what `chat:restarting`, `chat:notsent` and `chat:ended` resume. */
export const kvChat = (JSON.parse(chatsShowKv) as { id: string }).id
/** The retry chat (`chat-permission-yes.jsonl`'s): one of `chat:compacting`'s two. */
export const retryChat = chatIdOf(permissionYes)

/**
 * The Streaming session's two frames, each a named event of its recording:
 * the old `progress` of 40 reasoning ticks, as the share of turn 2's
 * reasoning deltas shown (`REASON.slice(0, round(len * p / 40))`,
 * `main.html:4964` at `b375fac9`). `chat` and `monitor` stand at 30;
 * `chat:compacting`, `console` (1g) and the `settings:*` presets (1i) at 15,
 * the old state's own.
 */
export const streamingFrames: Readonly<Record<'chat' | 'start', Freeze>> = (() => {
  const deltas = LineFramer.lines(streaming).filter((line) => EventDecoder.decode(line).type === 'thinking_delta').length
  return {
    chat: { name: 'progress 30: three-quarters through the reasoning', event: 'thinking_delta', nth: Math.round((deltas * 30) / 40) },
    start: { name: 'progress 15: the old starting frame', event: 'thinking_delta', nth: Math.round((deltas * 15) / 40) },
  }
})()

/** The Streaming session's rules at `frame`: `chat-streaming-handmade.jsonl`, frozen there, Stop ending its turn as `chat-streaming-cancel.jsonl` recorded it, and the model facts and chooser's reads. */
export function streamingRules(frame: keyof typeof streamingFrames): { readonly chats: Rule[]; readonly reads: Rule[]; readonly commands: Rule[] } {
  const suites = suiteRules()
  return {
    chats: [{ arguments: ['chat'], answer: { stdout: streaming, freeze: streamingFrames[frame], cancelled: streamingCancel } }],
    reads: [...modelReads('root'), ...suites.reads],
    commands: suites.commands,
  }
}

/** `chat`: Streaming, mid-reasoning (the old default start). */
export function chatPreset({ still, apogee }: PresetOptions): Preset {
  return { rules: streamingRules('chat'), route: 'chat', clock: { start: 1, still }, apogee }
}

/** `chat:retry`: Permission -- two reads, then `write_file` behind the gate, its card open; each answer its own capture, and Stop the capture of a cancel there. */
export function chatRetryPreset({ still, apogee }: PresetOptions): Preset {
  const suites = suiteRules()
  const answer: Answer = { stdout: permissionYes, continuations: { after: 'question', by: { yes: permissionYes, session: permissionSession, always: permissionAlways, no: permissionNo } }, cancelled: permissionCancel }
  return { rules: { chats: [{ arguments: ['chat'], answer }], reads: [...modelReads('root'), ...suites.reads], commands: suites.commands }, route: 'chat:retry', clock: { start: 60, still }, apogee }
}

/** `chat:wal`: Sources -- moved to the billed `claude`, the storage notes and a postgresql.conf attached, the cited answer, citation 1 open. */
export function chatWalPreset({ still, apogee }: PresetOptions): Preset {
  const suites = suiteRules()
  return { rules: { chats: [{ arguments: ['chat'], answer: { stdout: sourcesStream } }], reads: [...modelReads('root', 'claude'), ...suites.reads], commands: suites.commands }, route: 'chat:wal', clock: { start: 120, still }, apogee }
}

/** `chat:crash`: Error -- `vast` failed to load; Unload and retry, or Ask claude instead · billed, each its own continuation. */
export function chatCrashPreset({ still, apogee }: PresetOptions): Preset {
  const suites = suiteRules()
  const answer: Answer = { stdout: chatError, continuations: { after: 'error', by: { 'set model=claude': chatErrorSwitch, user: chatErrorRetry } } }
  return { rules: { chats: [{ arguments: ['chat'], answer }], reads: [...modelReads('vast', 'claude'), ...suites.reads], commands: suites.commands }, route: 'chat:crash', clock: { start: 10, still }, apogee }
}

/** `chat:pr`: Question -- Claude, a subscription (`models-list-server-tools.json`), asking the retry budget's scope; each answer and Skip its own continuation, and Stop the capture of a cancel there. */
export function chatPrPreset({ still, apogee }: PresetOptions): Preset {
  const suites = suiteRules()
  const answer: Answer = {
    stdout: questionBatch,
    continuations: { after: 'question', by: { 'Per upload': questionUpload, 'Per batch': questionBatch, 'Per host': questionHost, 'Per endpoint, shared by every upload to it': questionOwn, '': questionSkip } },
    cancelled: questionCancel,
  }
  return {
    rules: {
      chats: [{ arguments: ['chat'], answer }],
      reads: [{ arguments: ['models', 'list'], answer: { stdout: modelsListServerTools } }, { arguments: ['models', 'info', 'claude'], answer: { stdout: modelsInfoClaude } }, ...suites.reads],
      commands: suites.commands,
    },
    route: 'chat:pr',
    clock: { start: 60, still },
    apogee,
  }
}

/** The kv chat's rules: resumed (`--resume`), its children's recordings in turn, its earlier turns as `chats show` reads them back. */
function kvRules(children: readonly [Answer, ...Answer[]]): Preset['rules'] {
  const suites = suiteRules()
  return {
    chats: [{ arguments: [...ChatLaunch.arguments(), '--resume', kvChat], answer: children }],
    reads: [{ arguments: ['chats', 'show', kvChat], answer: { stdout: chatsShowKv } }, ...modelReads('root'), ...suites.reads],
    commands: suites.commands,
  }
}

/** `chat:restarting`: the kv chat moved to the suite `pair`, its child being stopped (it exits 5 ticks after its input closes), an attach held for the next. */
export function chatRestartingPreset({ still, apogee }: PresetOptions): Preset {
  return { rules: kvRules([{ stdout: resumedKv, exitAfter: 5 }, { stdout: resumedKvPair }]), route: 'chat:restarting', clock: { start: 3, still }, apogee }
}

/**
 * `chat:notsent`: the kv chat's turn written while a stop had closed its
 * child's input -- not sent -- then the session back on its next child,
 * which answers Send again as the chat-kv recipe's resume recorded that turn
 * (`resumed-kv-turn.jsonl`).
 */
export function chatNotSentPreset({ still, apogee }: PresetOptions): Preset {
  return { rules: kvRules([{ stdout: resumedKv, exitAfter: 2 }, { stdout: resumedKvTurn }]), route: 'chat:notsent', clock: { start: 6, still }, apogee }
}

/**
 * `chat:ended`: the kv chat's child dead mid-answer (exit 2, its stderr), a
 * setting and a compact held for Start again; the next child answers them,
 * the setting first (`resumed-kv-held-handmade.jsonl`).
 */
export function chatEndedPreset({ still, apogee }: PresetOptions): Preset {
  return {
    rules: kvRules([{ stdout: resumedKvEnded, exit: 2, stderr: 'loading\nfatal: the model went away\n' }, { stdout: resumedKv, continuations: { after: 'session', by: { 'set temperature=0.4': resumedKvHeld } } }]),
    route: 'chat:ended',
    clock: { start: 24, still },
    apogee,
  }
}

/**
 * `chat:compacting`: two saved chats compacting as jobs (`chats-compact.txt`,
 * each held back 30 ticks past its line, so a still frame has both running),
 * its open session Streaming at its starting frame. Its busy rows and open
 * row menu are 1g's list.
 */
export function chatCompactingPreset({ still, apogee }: PresetOptions): Preset {
  const streamingSetup = streamingRules('start')
  return {
    rules: { ...streamingSetup, jobs: [retryChat, kvChat].map((id): Rule => ({ arguments: ['chats', 'compact', id, '--yes'], answer: { stdout: chatsCompact, exitAfter: 30 } })) },
    route: 'chat:compacting',
    clock: { start: 1, still },
    apogee,
  }
}
