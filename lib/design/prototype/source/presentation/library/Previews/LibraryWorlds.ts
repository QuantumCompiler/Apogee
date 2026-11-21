import agentsList from '@recorded/business/reads/Fixtures/agents-list.json?raw'
import mcpList from '@recorded/business/reads/Fixtures/mcp-list.json?raw'
import mcpPresets from '@recorded/business/reads/Fixtures/mcp-presets.json?raw'
import symphoniesList from '@recorded/business/reads/Fixtures/symphonies-list.json?raw'
import symphonyPlayErr from '@recorded/business/reads/Fixtures/symphony-play.err?raw'
import symphonyPlay from '@recorded/business/reads/Fixtures/symphony-play.json?raw'
import symphonyShowUnknownKey from '@recorded/business/reads/Fixtures/symphony-show-unknown-key.err?raw'
import symphonyShow from '@recorded/business/reads/Fixtures/symphony-show.json?raw'
import triagePrompt from '../../../../fixtures/business/reads/agents-create-triage-prompt-handmade.txt?raw'
import configDeleteMcpServerCode from '../../../../fixtures/business/reads/config-delete-mcp-server-code-handmade.txt?raw'
import configPath from '../../../../fixtures/business/reads/config-path-handmade.txt?raw'
import notesDigest from '../../../../fixtures/business/reads/symphonies-notes-digest-handmade.yaml?raw'
import type { Rules } from '../../../infrastructure/process/StandInChild'
import type { Folders } from '../../../infrastructure/process/StandInFolders'
import { libraryFiles, libraryRules, libraryWorld } from './LibraryWorldRules'
import type { LibraryPreset, Lookup } from './LibraryWorldRules'

export * from './LibraryWorldRules'

/**
 * The Library place's recordings (design 1n), each a `?raw` import the build
 * inlines, named in `recordings` by its file name; the answers built from
 * them are `LibraryWorldRules.ts`, which imports none. Four are hand-made in
 * the CLI's shape under the design's `fixtures/`:
 * - `agents-create-triage-prompt-handmade.txt`: the prompt `agents create
 *   triage --description "Sort incoming issues by area"` scaffolds -- the
 *   Mac preview's literal (`LibraryPreviews.swift:34-42`), with the newline
 *   the binary ends it with (`agent.cpp:31-41`'s `starter_prompt`);
 * - `symphonies-notes-digest-handmade.yaml`: notes-digest's spec as the
 *   recording read it (`LibraryPreviews.swift:93-105`; its document is
 *   `symphony-show.json`);
 * - `config-path-handmade.txt`: `config path`'s one line
 *   (`config_cmd.cpp:931`), the Mac preview's value (`LibraryPreviews.swift:19`);
 * - `config-delete-mcp-server-code-handmade.txt`: `mcp-delete.txt`'s line for
 *   the server `mcp-list.json` lists, `code`.
 */
const recordings: Readonly<Record<string, string>> = {
  'agents-list.json': agentsList,
  'mcp-list.json': mcpList,
  'mcp-presets.json': mcpPresets,
  'symphonies-list.json': symphoniesList,
  'symphony-play.json': symphonyPlay,
  'symphony-play.err': symphonyPlayErr,
  'symphony-show.json': symphonyShow,
  'symphony-show-unknown-key.err': symphonyShowUnknownKey,
  'agents-create-triage-prompt-handmade.txt': triagePrompt,
  'config-delete-mcp-server-code-handmade.txt': configDeleteMcpServerCode,
  'config-path-handmade.txt': configPath,
  'symphonies-notes-digest-handmade.yaml': notesDigest,
}

/** A file's text, by its name; a name no import holds is refused, naming it. */
export function recording(name: string): string {
  const text = recordings[name]
  if (text === undefined) throw new Error(`no fixture named ${name}`)
  return text
}

/** The ticks the recorded play takes: its stderr's stage lines a tick each as its busy line, its stdout's lines a tick each, then its end (the stand-in's `busy`, 1j). */
export const playTicks = symphonyPlayErr.trimEnd().split('\n').length + symphonyPlay.trimEnd().split('\n').length + 1

/** The triage prompt and notes-digest's spec, as the editor opens them. */
export const editorFiles = { triage: triagePrompt, notesDigest } as const

/** What the Library reads and runs on every preset (`Presets.ts`' `modelling`). */
export function libraryPlaceRules(look: Lookup = recording): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  return libraryRules(look)
}

/** `folders` with the data directory's two editor files beside them, unless they name those folders already: every preset's. */
export function withLibraryFolders(folders: Folders = {}, look: Lookup = recording): Folders {
  return Object.entries(libraryFiles(look)).reduce<Folders>((all, [folder, files]) => (all[folder] !== undefined ? all : { ...all, [folder]: files }), folders)
}

/** `rules` with a Library preset's own answers after them, so its `mcp presets` and its refused save are the ones given. */
export function withLibraryWorld(rules: Rules, preset: LibraryPreset, look: Lookup = recording): Rules {
  const world = libraryWorld(preset, look)
  return { ...rules, reads: [...(rules.reads ?? []), ...world.reads] }
}

/** The Library's own previews' and tests' world: every preset's answers, and a Library preset's. */
export function libraryPreviewRules(preset: LibraryPreset = 'library', look: Lookup = recording): Rules {
  return withLibraryWorld(libraryRules(look), preset, look)
}
