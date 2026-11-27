import type { Answer, Rule, Rules } from '../../../infrastructure/process/StandInChild'
import type { FolderFile, Folders } from '../../../infrastructure/process/StandInFolders'

/**
 * The Library place's stand-in answers (design 1n), every one a recording of
 * the Mac app's or a `*-handmade*` file in the CLI's shape: `agents list`,
 * `symphonies list`, `mcp list` and `mcp presets`, the recorded play of
 * `critique-revise`, `symphonies show notes-digest`, `config path` and the
 * removal of the server `code`; and the two files the editor opens, as the
 * folders the CLI's data directory holds -- the triage prompt the binary
 * scaffolds and `notes-digest.yaml`.
 *
 * Each file is named by its file name, looked up in `LibraryWorlds.ts`'
 * `recordings`, where the `?raw` imports are: this module imports none, so
 * the fixture check (`LibraryFixtures.test.ts`) lists what every preset
 * names without loading a fixture, and a file missing on disk fails each
 * preset that names it, naming the preset and the file.
 */

/** Where a world's recordings come from: `LibraryWorlds.ts`' `recording`, or a test's lookup that lists the names. */
export type Lookup = (name: string) => string

/** The data directory `config path` names (`config-path-handmade.txt`): the parent of its `config/` folder. */
export const dataDirectory = '/Users/someone/.apogee'

/** The folders the editor's two files are in: the agent's prompt beside its siblings, the person's own symphony spec. */
export const libraryFolders = { prompts: `${dataDirectory}/prompts`, symphonies: `${dataDirectory}/symphonies` } as const

/**
 * Each symphony's input as the old prototype's page holds it (`main.html`'s
 * `lbSyms` at `b375fac9`): the prototype's own words, since no CLI document
 * carries a sample input. `critique-revise`'s is the input the Mac app's test
 * plays (`LibraryViewModelTests.swift:86`) and the recording answers.
 */
export const sampleInputs: Readonly<Record<string, string>> = {
  'critique-revise': 'A draft README for the tool.',
  'describe-answer': 'What does the sign in the window say?',
  'notes-digest': 'Tuesday’s meeting notes.',
}

/** The image Choose an image… offers, the old prototype's (`chooseImage`, `storefront.png`): a browser picks no file for the CLI. */
export const sampleImage = '/Users/someone/Pictures/storefront.png'

/** `symphonies play critique-revise --input <its sample> --output-format json`: the play the CLI recorded (`symphony-play.json`, its stage lines `symphony-play.err`). */
export const playArguments = ['symphonies', 'play', 'critique-revise', '--input', sampleInputs['critique-revise'] ?? '', '--output-format', 'json'] as const

/** The line the `library:unsaved` preset types into the triage prompt: the old frame's and the Mac preview's (`LibraryPreviews.swift:68`). */
export const typedLine = '\nFile each issue under one area.\n'

/** The refused spec: notes-digest's with the unknown key both prototypes add (`libDigestRefused()`, `LibraryPreviews.swift:106`). */
export function refusedSpec(spec: string): string {
  return spec.replace('    role: chat\n', '    role: chat\n    temprature: 0.2\n')
}

/** The Library presets, in the old `data-props` order: what the fixture check walks. */
export const libraryPresets = ['library', 'library:symphonies', 'library:mcp', 'library:unsaved', 'library:refused', 'library:image'] as const

/** A Library preset: its route, and the world it reads. */
export type LibraryPreset = (typeof libraryPresets)[number]

const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })

/**
 * What the Library reads and runs on every preset: the lists, `config path`
 * (the agents' data directory), `symphonies show notes-digest` (the editor's
 * save asks it), the recorded play, and the removal of `code`, each for its
 * exact arguments where it is a change. `show` judges the spec file by what
 * it holds, as the old prototype's save does (`main.html:3011-3019` at
 * `b375fac9`): while the stand-in's `notes-digest.yaml` holds the unknown
 * key `temprature:` it answers the recorded refusal
 * (`symphony-show-unknown-key.err`, exit 1), on every preset and however
 * often it is saved; otherwise the document (`symphony-show.json`). `mcp
 * presets` is Settings' world's on every preset (`xcode-mcp-presets.json`,
 * 1i); the Library presets answer it with `mcp-presets.json`, as the old
 * frames list it (`libraryWorld`).
 */
export function libraryRules(look: Lookup): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  const play: Answer = { stdout: look('symphony-play.json'), stderr: look('symphony-play.err'), busy: true }
  const show: Rule = {
    arguments: ['symphonies', 'show', 'notes-digest'],
    refusedWhile: { folder: libraryFolders.symphonies, name: 'notes-digest.yaml', holds: 'temprature:', answer: { stderr: look('symphony-show-unknown-key.err'), exit: 1 } },
    answer: { stdout: look('symphony-show.json') },
  }
  return {
    reads: [read(['agents', 'list'], look('agents-list.json')), read(['symphonies', 'list'], look('symphonies-list.json')), read(['mcp', 'list'], look('mcp-list.json')), show],
    commands: [
      { arguments: ['config', 'path'], exact: true, answer: { stdout: look('config-path-handmade.txt') } },
      { arguments: ['config', 'delete-mcp-server', 'code'], exact: true, answer: { stdout: look('config-delete-mcp-server-code-handmade.txt') } },
    ],
    jobs: [{ arguments: playArguments, exact: true, answer: play }],
  }
}

/** The folders the editor reads its files from, as the CLI's data directory holds them: the triage prompt and notes-digest's spec. */
export function libraryFiles(look: Lookup): Folders {
  const prompt: FolderFile = { name: 'triage.txt', text: look('agents-create-triage-prompt-handmade.txt') }
  const spec: FolderFile = { name: 'notes-digest.yaml', text: look('symphonies-notes-digest-handmade.yaml') }
  return { [libraryFolders.prompts]: [prompt], [libraryFolders.symphonies]: [spec] }
}

/**
 * A Library preset's own answers, beside every preset's: the Library frames'
 * `mcp presets` (`mcp-presets.json`, the two presets the old lists). Every
 * Library preset answers the same; `library:refused` reaches its refusal
 * through what the saved file holds (`libraryRules`' `show`), so Put it back
 * as it was is read again and stands.
 */
export function libraryWorld(_preset: LibraryPreset, look: Lookup): Required<Pick<Rules, 'reads'>> {
  return { reads: [read(['mcp', 'presets'], look('mcp-presets.json'))] }
}

/**
 * The file names a Library preset's answers and folders look up, each once
 * -- every preset's (`libraryRules`, `libraryFiles`) and its own
 * (`libraryWorld`): what the fixture check holds to exist, so it checks what
 * the rules read and nothing kept beside them.
 */
export function libraryPresetNames(preset: LibraryPreset): string[] {
  const named = new Set<string>()
  const look: Lookup = (name) => (named.add(name), '')
  libraryRules(look)
  libraryFiles(look)
  libraryWorld(preset, look)
  return [...named]
}
