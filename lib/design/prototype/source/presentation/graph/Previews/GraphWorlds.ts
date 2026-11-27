import buildUnregistrableErr from '@recorded/business/jobs/Fixtures/graph-build-unregistrable.err?raw'
import buildUnregistrableOut from '@recorded/business/jobs/Fixtures/graph-build-unregistrable.out?raw'
import explainMyNotes from '@recorded/business/jobs/Fixtures/graph-explain-my-notes.json?raw'
import reportMyNotes from '@recorded/business/jobs/Fixtures/graph-report-my-notes.json?raw'
import statsMyNotes from '@recorded/business/jobs/Fixtures/graph-stats-my-notes.json?raw'
import dedupeDryRun from '@recorded/business/reads/Fixtures/graph-dedupe-dry-run.txt?raw'
import dedupeMerges from '@recorded/business/reads/Fixtures/graph-dedupe-dry-run-merges-handmade.txt?raw'
import dedupeMerged from '@recorded/business/reads/Fixtures/graph-dedupe-merged-handmade.txt?raw'
import graphDelete from '@recorded/business/reads/Fixtures/graph-delete.txt?raw'
import exportGraphml from '@recorded/business/reads/Fixtures/graph-export-graphml.txt?raw'
import exportHtml from '@recorded/business/reads/Fixtures/graph-export-html.txt?raw'
import exportMermaid from '@recorded/business/reads/Fixtures/graph-export-mermaid.txt?raw'
import proseExplain from '@recorded/business/reads/Fixtures/graph-prose-explain.json?raw'
import proseReport from '@recorded/business/reads/Fixtures/graph-prose-report.json?raw'
import proseStats from '@recorded/business/reads/Fixtures/graph-prose-stats.json?raw'
import reportOut from '@recorded/business/reads/Fixtures/graph-report-out.txt?raw'
import explain from '@recorded/presentation/graph/Fixtures/explain.json?raw'
import query from '@recorded/presentation/graph/Fixtures/query.json?raw'
import stats from '@recorded/presentation/graph/Fixtures/stats.json?raw'
import updateDemo from '../../../../fixtures/business/jobs/graph-update-demo-handmade.txt?raw'
import deleteNotes from '../../../../fixtures/business/reads/graph-delete-notes-handmade.txt?raw'
import explainGoneFetch from '../../../../fixtures/business/reads/graph-explain-gone-fetch-handmade.err?raw'
import files from '../../../../fixtures/business/reads/graph-files-handmade.json?raw'
import type { Rules } from '../../../infrastructure/process/StandInChild'
import { graphNoteRules, graphRules } from './GraphWorldRules'
import type { Lookup } from './GraphWorldRules'

export * from './GraphWorldRules'

/**
 * The Code graph place's recordings (design 1o), each a `?raw` import the
 * build inlines, named in `recordings` by its file name; the answers built
 * from them are `GraphWorldRules.ts`, which imports none. Two are the Mac
 * app's own hand-made dedupe lines (`graph-dedupe-dry-run-merges-handmade.txt`,
 * `graph-dedupe-merged-handmade.txt`); four are the design's, in the CLI's
 * shape under `fixtures/`:
 * - `graph-files-handmade.json`: `graph query`'s shape (`query.json`'s keys)
 *   holding the demo graph's four Python files and their imports -- the one
 *   recorded `imports` edge (`ingest/runner.py` to `ingest/fetch.py`) and
 *   `ingest/cli.py`'s two, which `main`'s recorded calls of `HttpClient` and
 *   `Runner` across files say it makes -- the Files view's picture;
 * - `graph-explain-gone-fetch-handmade.err`: `graph-explain-gone.err`'s words
 *   on the demo graph, for `Runner._fetch` renamed `_fetch_one` by an update
 *   elsewhere, its near matches the recorded demo nodes beside it, as the old
 *   `graphRefusal()` made its own in that recording's shape;
 * - `graph-delete-notes-handmade.txt`: the CLI's line for a collection's
 *   graph cleared (`graph.cpp`'s delete), over `graph-prose-stats.json`'s
 *   counts;
 * - `fixtures/business/jobs/graph-update-demo-handmade.txt`: the rebuild's
 *   one line, `graph.cpp`'s Code graph line (`print_code_summary`) over
 *   `stats.json`'s counts, with its sync suffix for a rebuild of the deleted
 *   graph (`nodes +16 ~0 -0; edges +21 -0`); the build's earlier lines are
 *   left out, their References counts in no recording.
 */
const recordings: Readonly<Record<string, string>> = {
  'stats.json': stats,
  'query.json': query,
  'explain.json': explain,
  'graph-prose-stats.json': proseStats,
  'graph-prose-report.json': proseReport,
  'graph-prose-explain.json': proseExplain,
  'graph-export-html.txt': exportHtml,
  'graph-export-graphml.txt': exportGraphml,
  'graph-export-mermaid.txt': exportMermaid,
  'graph-report-out.txt': reportOut,
  'graph-dedupe-dry-run.txt': dedupeDryRun,
  'graph-dedupe-dry-run-merges-handmade.txt': dedupeMerges,
  'graph-dedupe-merged-handmade.txt': dedupeMerged,
  'graph-delete.txt': graphDelete,
  'graph-files-handmade.json': files,
  'graph-explain-gone-fetch-handmade.err': explainGoneFetch,
  'graph-delete-notes-handmade.txt': deleteNotes,
  'graph-update-demo-handmade.txt': updateDemo,
  // GUI M22: the built CLI's first build of my notes' graph, and what its sandbox read back after it.
  'graph-build-unregistrable.out': buildUnregistrableOut,
  'graph-build-unregistrable.err': buildUnregistrableErr,
  'graph-stats-my-notes.json': statsMyNotes,
  'graph-report-my-notes.json': reportMyNotes,
  'graph-explain-my-notes.json': explainMyNotes,
}

/** A file's text, by its name; a name no import holds is refused, naming it. */
export function recording(name: string): string {
  const text = recordings[name]
  if (text === undefined) throw new Error(`no fixture named ${name}`)
  return text
}

/** What the noted preset reads and runs beside the place's (GUI M22): `Presets.ts`' `graph:built-with-a-note`. */
export function graphNotedRules(look: Lookup = recording): Required<Pick<Rules, 'reads' | 'jobs'>> {
  return graphNoteRules(look)
}

/** What the Code graph place reads and runs on every preset (`Presets.ts`' `modelling`). */
export function graphPlaceRules(look: Lookup = recording): Required<Pick<Rules, 'reads' | 'commands' | 'jobs'>> {
  return graphRules(look)
}
