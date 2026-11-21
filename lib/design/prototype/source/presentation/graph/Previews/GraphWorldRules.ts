import type { Rule, Rules } from '../../../infrastructure/process/StandInChild'
import type { GraphWorld } from '../ViewModels/GraphViewModel'

/**
 * The Code graph place's stand-in answers (design 1o), every one a recording
 * of the Mac app's or a `*-handmade*` file in the CLI's shape. The code graph
 * is the recorded `demo` (`stats.json`, `query.json`: 4 Python files, 16
 * nodes, 21 edges) where the old prototype drew an invented `~/code/ingest`;
 * the prose graph is the `notes` collection's (`graph-prose-stats.json`,
 * `graph-prose-report.json`, `graph-prose-explain.json`). Its More menu's
 * commands answer as recorded, though several recordings name another size
 * or graph (`graph-export-html.txt`'s 9 entities, `graph-export-graphml.txt`'s
 * 10, `graph-export-mermaid.txt`'s 3 functions, `graph-dedupe-dry-run.txt`'s
 * 9 code entities, `graph-delete.txt`'s `spare`), listed in the record.
 *
 * Each file is named by its file name, looked up in `GraphWorlds.ts`'
 * `recordings`, where the `?raw` imports are: this module imports none, so
 * the fixture check lists what every preset names without loading a fixture.
 */

/** Where a world's recordings come from: `GraphWorlds.ts`' `recording`, or a test's lookup that lists the names. */
export type Lookup = (name: string) => string

/** The four files the demo graph's recordings name (`stats.json` counts 4): the Files view's question. */
const demoFiles = ['ingest/__init__.py', 'ingest/cli.py', 'ingest/fetch.py', 'ingest/runner.py']

/**
 * The place's world: the recorded graphs, the questions whose pictures the
 * CLI answered (`query.json`'s, and the hand-made Files picture's), the node
 * each view opens on (the old `gNode`s, `run_batch` and `runner`), where a
 * save goes, and where the graph's sources are.
 */
export const graphWorld: GraphWorld = {
  code: 'demo',
  prose: 'notes',
  functionsQuestion: 'runner fetch',
  filesQuestion: demoFiles.join(' '),
  functionsStart: 'ingest.runner.Runner.run_batch',
  filesStart: 'ingest/runner.py',
  documents: '/Users/someone/Documents',
}

/** The node graph:refused clicks in a picture an update elsewhere overtook: `Runner._fetch`, renamed `_fetch_one` there. */
export const overtakenNode = 'ingest.runner.Runner._fetch'

/** The Code graph presets, in the old `data-props` order: what the fixture check walks. Each reads the place's answers, every preset's. */
export const graphPresets = ['graph', 'graph:prose', 'graph:refused'] as const

const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })
const exact = (args: readonly string[], stdout: string): Rule => ({ arguments: args, exact: true, answer: { stdout } })
const saved = (file: string) => `${graphWorld.documents}/${file}`

/** What the Code graph place reads and runs on every preset: each read by its arguments, each change and job for its exact ones. */
export function graphRules(look: Lookup): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  const { code, prose } = graphWorld
  return {
    reads: [
      read(['graph', 'stats', code], look('stats.json')),
      read(['graph', 'query', '--graph', code, '--', graphWorld.functionsQuestion], look('query.json')),
      read(['graph', 'query', '--graph', code, '--', graphWorld.filesQuestion], look('graph-files-handmade.json')),
      // In an overtaken picture only Runner._fetch is refused (the old pickNode's one refusal): its exact rule, the longer, wins over the prefix that answers every other drawn node with the recorded explain.json, which the code views never draw -- the card is the picture's.
      { arguments: ['graph', 'explain', '--graph', code, '--', overtakenNode], exact: true, answer: { stderr: look('graph-explain-gone-fetch-handmade.err'), exit: 1 } },
      read(['graph', 'explain', '--graph', code, '--'], look('explain.json')),
      read(['graph', 'stats', prose], look('graph-prose-stats.json')),
      read(['graph', 'report', '--graph', prose], look('graph-prose-report.json')),
      exact(['graph', 'explain', '--graph', prose, '--', 'Heat pump'], look('graph-prose-explain.json')),
    ],
    commands: [
      exact(['graph', 'export', 'html', '--graph', code, '-o', saved(`${code}.html`)], look('graph-export-html.txt')),
      exact(['graph', 'export', 'graphml', '--graph', code, '-o', saved(`${code}.graphml`)], look('graph-export-graphml.txt')),
      exact(['graph', 'export', 'mermaid', '--graph', code, '-o', saved(`${code}.mmd`)], look('graph-export-mermaid.txt')),
      exact(['graph', 'report', '--graph', code, '--out', saved(`${code}-report.md`)], look('graph-report-out.txt')),
      exact(['graph', 'dedupe', code, '--dry-run'], look('graph-dedupe-dry-run.txt')),
      exact(['graph', 'dedupe', prose, '--dry-run'], look('graph-dedupe-dry-run-merges-handmade.txt')),
      exact(['graph', 'delete', code], look('graph-delete.txt')),
      exact(['graph', 'delete', prose], look('graph-delete-notes-handmade.txt')),
    ],
    jobs: [exact(['graph', 'dedupe', prose], look('graph-dedupe-merged-handmade.txt')), exact(['graph', 'update', code], look('graph-update-demo-handmade.txt'))],
  }
}

/** The file names the place's answers look up, each once: what the fixture check holds to exist on every Code graph preset. */
export function graphPlaceNames(): string[] {
  const named = new Set<string>()
  graphRules((name) => (named.add(name), ''))
  return [...named]
}
