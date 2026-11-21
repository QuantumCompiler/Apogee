/**
 * The contract page's data (design 1r): the old BuildContract board, "how the
 * app talks to Apogee", at 1c's frozen revision `b375fac9`, converted once by
 * a throwaway script and kept here as data, edited by hand. A part the design
 * needs from the CLI is one row; a gap the CLI has is one gap; the stat cards
 * and the chips' links are counted and found from them, never typed.
 *
 * Inline code is written in backticks, as the page's one code span draws it.
 * Every row, gap, decision and call is the frozen board's text, word for word;
 * after its 108 rows and 24 gaps come the eight the shipped items handed this
 * page (1e's six from the monitor, 1h's two from the Models place).
 */

/** The map's parts, its `SESSIONS` … `SETTINGS AND SYSTEM` labels, in the board's order. */
export const contractParts = ['SESSIONS', 'MODELS', 'SUITES', 'TASKS', 'CODE GRAPH', 'KNOWLEDGE', 'TRAINING', 'LIBRARY', 'REVIEW', 'VIDEO AND VOICE', 'SETTINGS AND SYSTEM'] as const

export type ContractPart = (typeof contractParts)[number]

/**
 * A part's status: `ready` exists today; `app` the app does itself from what
 * exists; `gap` needs a CLI change, a gap below; `verify` is assumed, to be
 * checked before building on it.
 */
export type ContractStatus = 'ready' | 'app' | 'gap' | 'verify'

/** A gap's id: `G1` … */
export type GapId = `G${number}`

/** When a gap closes: it has (`Closed`), before the app's first release (`Before v1`), or after it (`Later`). */
export type GapWhen = 'Closed' | 'Later' | 'Before v1'

/** One part of the design: what it shows, how the app gets it, and its status; a `gap` names its gap. */
export type ContractRow =
  | { readonly part: ContractPart; readonly shows: string; readonly how: string; readonly status: 'ready' | 'app' | 'verify' }
  | { readonly part: ContractPart; readonly shows: string; readonly how: string; readonly status: 'gap'; readonly gap: GapId }

/**
 * A gap to close in the CLI: what is missing (`title`), the additive change
 * that closes it (`change`), and when. A closed gap says how it closed
 * (`note`, after "Closed:"); an open one what the app does until then
 * (`fallback`, after "Until then:").
 */
export type Gap =
  | { readonly id: GapId; readonly title: string; readonly change: string; readonly when: 'Closed'; readonly note: string }
  | { readonly id: GapId; readonly title: string; readonly change: string; readonly when: 'Later' | 'Before v1'; readonly fallback: string }

/** A step, a decision or a call: its lead in bold, then the rest. */
export interface Lead {
  readonly lead: string
  readonly text: string
}

/** The page's own words, as the frozen board writes them. */
export const contractWords = {
  eyebrow: 'BUILD · 1 OF 2',
  title: 'How the app talks to Apogee',
  intro:
    'The desktop app is the sibling application the spec commits to. It runs the `apogee` binary as child processes and speaks to them over stdin and stdout in machine mode, never over a port. Every change is the command a person would type, and every screen is drawn from a command’s JSON. This board maps each part of the design to that contract, names what the CLI doesn’t offer yet, and records the decisions the design makes. Checked against apogee 0.2.0 (f72f05f6); the named pipelines’ reads are the CLI’s M30 (32e1424c), as its reference documents them.',
  /** Each stat card's words, by what it counts: the rows by their status, and the gaps still open. */
  cards: {
    ready: 'parts drawn from what the CLI does today',
    app: 'the app does itself, from what exists',
    gap: 'gaps open, each an additive CLI change',
    verify: 'assumed; each checked by probe',
  } satisfies Readonly<Record<ContractStatus, string>>,
  startingUp: 'Starting up',
  map: 'The contract, part by part',
  mapIntro: 'What each part of the design needs, and where it comes from.',
  /** What each chip means, after the chip in the map's introduction. */
  legend: {
    ready: 'exists today.',
    app: 'the app does itself from what exists.',
    gap: 'needs a CLI change, below.',
    verify: 'assumed; check before building on it.',
  } satisfies Readonly<Record<ContractStatus, string>>,
  head: ['THE DESIGN SHOWS', 'HOW THE APP GETS IT', 'STATUS'],
  gaps: 'Gaps to close in the CLI',
  gapsIntro: 'Each is a growth the stability promise allows: a new field, line or flag that older drivers ignore. None blocks building; each has a fallback, and the fallback is what the app does until the change lands.',
  decisions: 'Decisions the design makes',
  decisionsIntro: 'Taken while designing; each can be vetoed.',
  calls: 'Calls for you',
  callsIntro: 'Yours to make before the first build.',
} as const


/** The steps the app takes when it starts, in order. */
export const startingUp: readonly Lead[] = [
  { lead: 'Find the binary.', text: '`~/.local/bin/apogee`, or the path set in Settings › General. Run `apogee version`.' },
  { lead: 'Check the vocabulary.', text: '`apogee __machine-schema` carries `x-apogee.schema`, and every child’s `session` event repeats it as `capabilities.schema`. A newer schema still works, since growth is additive; a different `protocol_version` is refused, naming the build the app needs.' },
  { lead: 'Check health.', text: '`apogee check --output-format json`. Failures show in Settings › Backends & keys with each row’s remedy, and `check --fix` and `bootstrap` are buttons.' },
  { lead: 'First reads.', text: '`models list`, `models status`, `chats list` and `task list`, each with `--output-format json`.' },
  { lead: 'Open the last session.', text: '`apogee chat --resume <id> --tools --output-format stream-json --input-format stream-json`, started when the session comes into view.' },
]

/**
 * The contract map: every part of the design and how the app gets it, by part, in
 * table order -- the frozen board's 108 rows, then at the end of their part the
 * rows for the gaps the shipped items handed this page (1e, 1h). A row is one
 * entry; the stat cards and the gap links are computed from them.
 */
export const contractRows: readonly ContractRow[] = [
  { part: 'SESSIONS', shows: 'The answer, thinking, helper and tool lines, notices, the todo', how: '`chat` child events: `answer_*`, `thinking*`, `tool_status`, `notice`, `todo`, `result`', status: 'ready' },
  { part: 'SESSIONS', shows: 'Stop', how: 'send `{"type":"cancel"}`; the turn still ends in one `result`, `finish_reason: "cancelled"`', status: 'ready' },
  { part: 'SESSIONS', shows: 'Restarting, Not sent, Session ended, and when held lines go', how: 'a restart closes the child’s input and stops it; Restarting while it stops, Starting apogee until the next child’s `session` event, an `attach`, `detach`, `set` or `compact` made meanwhile held for it; a `user` line the closed input refused drawn Not sent, with Send again; a child that dies said from its exit status and its stderr’s last line, Start again resuming with `--resume <id>`, the lines it never took still held', status: 'app' },
  { part: 'SESSIONS', shows: 'Thinking off, on or auto, and the effort', how: '`--think`, `--effort` when the child starts; `thinking` says `budget_reached`', status: 'ready' },
  { part: 'SESSIONS', shows: 'Changing model, thinking, effort, temperature or system prompt mid-session', how: 'send `{"type":"set", …}` between turns; each setting said in a `notice` (`temperature: 0.3 -- from the next turn`) or refused by name', status: 'ready' },
  { part: 'SESSIONS', shows: 'Permission card: allow once, for the session, always, deny', how: '`question` with `kind: "permission"`, `tool`, `target`; answer `yes`, `session`, `always`, `no`', status: 'ready' },
  { part: 'SESSIONS', shows: 'The card’s +12 −3 and Show changes', how: 'the question’s `detail` on `write_file` and `edit_file`: a unified diff of the change, its counts read off it', status: 'ready' },
  { part: 'SESSIONS', shows: 'A new website’s whole URL on the card', how: '`outbound: true` and `detail` on the question', status: 'ready' },
  { part: 'SESSIONS', shows: 'Question card', how: '`question`; one `answer` line per question, free text always accepted', status: 'ready' },
  { part: 'SESSIONS', shows: 'Attach a file, folder, image, recording or video; @ mentions', how: '`{"type":"attach"}` lines; `@path` in the text; each outcome a `notice`', status: 'ready' },
  { part: 'SESSIONS', shows: 'Detach', how: 'send `{"type":"detach","path":…}`; a `notice` says it', status: 'ready' },
  { part: 'SESSIONS', shows: '“Recalled 1 past session: asyncio timeouts in the scraper”', how: 'the `memory` event’s `sessions`: each `id`, `name` and `updated`, the name a link to it', status: 'ready' },
  { part: 'SESSIONS', shows: 'Sources [1] [2] [3] with their page', how: 'the `sources` event before the answer: each excerpt’s `file`, `pages`, `lines` or `times`, and its `score`', status: 'ready' },
  { part: 'SESSIONS', shows: 'Context meter and “not sent” lines', how: '`result.usage.input_tokens` against the window from `models info`; the budget `notice`', status: 'app' },
  { part: 'SESSIONS', shows: 'Compact', how: 'send `{"type":"compact"}` to the open session; `chats compact <id> --yes` for a saved one, after the person’s word', status: 'ready' },
  { part: 'SESSIONS', shows: 'Private session', how: '`chat --private`: nothing written; the `session` event says `private: true` and names no `chat_id`', status: 'ready' },
  { part: 'SESSIONS', shows: 'Session list: titles, dates, turns; rename, delete, details', how: '`chats list --output-format json`; `chats title`, `chats delete`; `chats info` as printed, for Details…', status: 'ready' },
  { part: 'SESSIONS', shows: 'The model glyph on each session row', how: 'each `chats list` row’s `backend` and `model`; the backend’s kind from `models list`', status: 'ready' },
  { part: 'SESSIONS', shows: 'Inspector: helpers and roles', how: '`models status --output-format json`', status: 'ready' },
  { part: 'SESSIONS', shows: 'Slash commands', how: 'machine mode doesn’t carry them: each is the app’s own action (a `set`, `detach` or `compact` line, a restart with flags, a command, a sheet)', status: 'app' },
  { part: 'SESSIONS', shows: 'The execute door: a suite or bare on one model, a project folder, orchestrate', how: '`execute --suite <name> | -m <backend> [--orchestrate]`, the same `stream-json` both ways, started in the folder; a project says its context file and the coder persona as `notice`s', status: 'ready' },
  { part: 'SESSIONS', shows: '/play in an execute session, its stages narrated', how: 'the `user` line `/play <symphony> <input>`; a `tool_status` per stage (`stage 1/2 summarize — asking utility (…)`), the output as `answer_*`; the catalog from `symphonies list`', status: 'ready' },
  { part: 'SESSIONS', shows: 'A session’s suite: the header’s name, the chooser, a switch', how: '`chat --suite <name|off> [--force]` when the child starts; machine mode carries no suite, so a switch stops the child and resumes it with `--resume <id> --suite …`, said in the transcript; each suite’s verdict from `models status --suite <name> --output-format json`; a resumed chat’s suite isn’t said (G23)', status: 'app' },
  { part: 'SESSIONS', shows: 'The banner’s suite and how many symphonies orchestrate offers', how: 'the `session` event names the model only; the app says the suite from its own launch and `config get models.default_suite`', status: 'gap', gap: 'G23' },
  { part: 'MODELS', shows: 'Backends: rows and details', how: '`models list --output-format json`, `models info --output-format json`; local, cloud and subscription rows', status: 'ready' },
  { part: 'MODELS', shows: 'Memory a backend needs at its window', how: '`models info`’s typed `data`: `bytes`, `window` and `cache`, null where the files state nothing (checked by probe)', status: 'ready' },
  { part: 'MODELS', shows: 'Stored weights: every set, pipelines and voice models included', how: '`models list --all --output-format json`: each set’s handle, format, architecture, record, and its backend or `(not configured)`; `--all` shows the SafeTensors sets a conversion used', status: 'ready' },
  { part: 'MODELS', shows: 'A set’s details and what it was made from', how: '`models info <set> --output-format json`: `data.lineage` as structured steps, and the `made from it` field (checked by probe)', status: 'ready' },
  { part: 'MODELS', shows: 'Convert SafeTensors to GGUF or MLX, registering what’s made', how: '`models convert <set> [--mlx] [-t …] [--register | --register-with …] [--base-name] [--model-type]`', status: 'ready' },
  { part: 'MODELS', shows: 'Quantize a GGUF', how: '`models quantize <set> -t <level>`, then `config add-backend` when it’s to be registered', status: 'ready' },
  { part: 'MODELS', shows: 'A conversion’s or a quantize’s progress', how: 'stderr text only; each step ticks on the set its own run names, once `models info <path>` places it in the job’s own model folder: a quantize’s one GGUF, or each path a conversion prints. `models list` decides nothing', status: 'gap', gap: 'G18' },
  { part: 'MODELS', shows: 'Register one set as a backend', how: '`config add-backend <name> --type llamacpp|mlx --model-path … [--mmproj-path] [--context-size] [--cache-type]`', status: 'ready' },
  { part: 'MODELS', shows: 'Register every set that isn’t yet', how: '`config scan --register` runs it; the names it gives are text', status: 'gap', gap: 'G19' },
  { part: 'MODELS', shows: 'Remove a backend, keeping its weights', how: '`config delete-backend <name>`', status: 'ready' },
  { part: 'MODELS', shows: 'Delete a set or a whole model, its backends with it or not', how: '`models delete <handle|model> -y [--and-backends]`; without the flag the entries stay, and the backend row says its weights are gone', status: 'ready' },
  { part: 'MODELS', shows: 'Rename a model, its backends with it', how: '`models rename <model> <new-name>`', status: 'ready' },
  { part: 'MODELS', shows: 'Check a set’s files', how: '`models repair <set>` runs it; the result is text and an exit status', status: 'gap', gap: 'G20' },
  { part: 'MODELS', shows: 'A download with its steps', how: '`models pull … --output-format stream-json`: each step, bytes done and total, then each stored set’s document (checked by probe)', status: 'ready' },
  { part: 'MODELS', shows: 'Stop a job, run the stopped step again', how: 'interrupt the child, which removes its staging; the sets already made stay, and the app runs what is left (`models convert`, `models quantize`)', status: 'app' },
  { part: 'MODELS', shows: 'Roles', how: '`config set-default-*`; `models status`: its six role fields, and the suite they resolve under (`footprint.suite`, else the `suite` field’s name) as a line above them, never a row or a picker', status: 'ready' },
  { part: 'MODELS', shows: 'Unload', how: 'end the session child holding it; the app knows which child holds what', status: 'app' },
  { part: 'MODELS', shows: 'What a repository holds before it is pulled: a model, a pipeline or a voice', how: 'nothing states it before the pull: each stored set’s document says what it is once `models pull` has stored it', status: 'gap', gap: 'G31' },
  { part: 'MODELS', shows: 'The free space on the store’s volume, beside the store', how: '`models info` says each set’s size, never the volume’s; only `system`’s `disk.volume_available_bytes` says it', status: 'gap', gap: 'G32' },
  { part: 'SUITES', shows: 'Suites and their members', how: '`models status` names the default suite; `config get suites` and `suites.<name>` its members (checked by probe)', status: 'ready' },
  { part: 'SUITES', shows: 'Will it fit?', how: '`models status --suite <name> --output-format json`: its `footprint` field says each member’s weights and cache and the verdict `chat --suite` reaches, as text', status: 'gap', gap: 'G10' },
  { part: 'TASKS', shows: 'A running task: plan, rounds, checks, activity', how: '`task run … --output-format stream-json`: `task_started`, `task_plan`, `task_round`, `task_grant`, `task_finished`', status: 'ready' },
  { part: 'TASKS', shows: 'Tasks started elsewhere', how: '`task list`, `task status --output-format json`, read again on focus', status: 'ready' },
  { part: 'TASKS', shows: 'Stop after this round, stop now, resume', how: '`task halt`, `task cancel`, `task resume`', status: 'ready' },
  { part: 'TASKS', shows: 'A task’s grants, and each call allowed or denied', how: '`task status --output-format json`: `policy.grants`, each turn’s `allowed` and `denied` (`tool`, `target`, `by`), worded as `task status` prints them; a live round’s `task_grant` lines until its `task_round` lands', status: 'ready' },
  { part: 'TASKS', shows: 'The list past the newest 50, and Show all', how: '`task list`’s `total`; Show all reads `task list --all`', status: 'ready' },
  { part: 'TASKS', shows: 'A start or resume the CLI refuses', how: 'exit 1, nothing on stdout, the refusal on stderr alone: its last line, `apogee task:` kept, in the alert; a refused resume’s line stays in its task’s activity', status: 'ready' },
  { part: 'TASKS', shows: 'Grants, checks and the round budget', how: '`--allow`, `--require`, `--require-file`, `--rounds` (1 to 32)', status: 'ready' },
  { part: 'TASKS', shows: 'Time so far', how: 'the app’s own clock', status: 'app' },
  { part: 'TASKS', shows: 'New task from a recipe, its expansion shown before it runs', how: '`task recipes [show <name>] --output-format json`; launched as `task run --recipe <name> --input …`, a suggested grant given only as a ticked `--allow`; the hand-typed twin drawn from the recipe’s document', status: 'ready' },
  { part: 'CODE GRAPH', shows: 'Symbols, callers and callees, files', how: '`graph stats | show | neighbors | explain --output-format json`', status: 'ready' },
  { part: 'CODE GRAPH', shows: 'The layout of the graph', how: 'the app lays it out', status: 'app' },
  { part: 'CODE GRAPH', shows: 'Rebuild', how: '`graph build`, `graph update`', status: 'ready' },
  { part: 'CODE GRAPH', shows: 'A collection’s prose graph: its report, communities and decisions', how: '`graph report --output-format json`; `graph communities` run supervised; `graph explain`’s `communities` and `decisions`', status: 'ready' },
  { part: 'CODE GRAPH', shows: 'Export, the report as a file, dedupe, delete', how: '`graph export html | graphml | mermaid --graph <g> -o <path>` and `graph report --out <path>`, to a path the person picks; `graph dedupe <g> --dry-run` first, then `graph dedupe <g>` supervised; `graph delete <g>` after the person’s word; each one’s first line said', status: 'ready' },
  { part: 'CODE GRAPH', shows: 'A node the graph no longer explains', how: '`graph explain` exits 1, the refusal on stderr alone: its last line, `apogee graph:` and its near matches kept, in the alert over the kept canvas, the node highlighted under it with no card; OK drops that selection, the picture kept. A node a Rebuild removed is dropped the same way, its line said once (GUI 6h)', status: 'ready' },
  { part: 'KNOWLEDGE', shows: 'Decision records: list, ask, capture, link, move, delete, export, a record’s raw conversation', how: '`knowledge list --output-format json`; `knowledge query --json`; `capture` supervised; `link`, `status`, `delete`, `export`, each read back; `knowledge info <id> --raw` as printed, cut at its marker', status: 'ready' },
  { part: 'KNOWLEDGE', shows: 'Collections: health, sources, search, ingest, delete', how: '`embed list | info --output-format json`; `embed query`’s lines as printed (no document form); `ingest` supervised; `delete --yes` only after the person’s word', status: 'ready' },
  { part: 'KNOWLEDGE', shows: 'A collection’s vectors: how many, their width, and the model they came from, or the models they are mixed from', how: '`embed info --output-format json`’s `vectors` object (`chunks`, `dimension`, `widths`, `model`, `mixed`), said in the text form’s own words, `[mixed: \'a\' and \'b\' -- re-ingest every source with one model]` among them (the CLI’s 57s); `null` is a text index only (GUI 6h)', status: 'ready' },
  { part: 'TRAINING', shows: 'Runs: launched, gated, promoted, rolled back', how: '`train status | versions --output-format json`; `train run`, `eval` and `promote` supervised; `rollback` confirmed', status: 'ready' },
  { part: 'TRAINING', shows: 'A run’s progress', how: '`train run` prints its iterations to stderr as text and takes no `--output-format`', status: 'gap', gap: 'G21' },
  { part: 'TRAINING', shows: 'Pipelines: the named specs, each with its stages and student', how: '`train pipeline list --output-format json`: the config’s `training.pipelines` entries by name, each its `student` and `stages`; `train pipeline show <name>` for one whole (the CLI’s M30, pinned by its own end-to-end test). The app names them from `__complete train pipeline run --pipeline` until it reads `list`', status: 'ready' },
  { part: 'TRAINING', shows: 'Pipeline runs: newest first, a run’s stages, Resume where it stopped short', how: '`train pipeline status --output-format json`: every run, a cycle pass’s among them; `train pipeline status <id>`: its manifest, each stage’s word, run and cumulative score; `train pipeline resume <id>` supervised after the person’s word (checked by probe)', status: 'ready' },
  { part: 'TRAINING', shows: 'A run’s Resume: where it starts and why it is held; a cycle pass never resumed', how: '`train pipeline status <id> --output-format json`’s `resume` (`stage`, `name`, `pipeline`, `note`) and `holder`: Resume from the stage it names, a stage kept failed said in its `note`, run as `train pipeline resume <id> --pipeline <resume.pipeline>`; held in the CLI’s words while `holder` is set, or with the console line where `resume.pipeline` is null; a `cycle-` run never offered it, its page line the CLI’s 57h words, Show the cycle its door (GUI 6i)', status: 'ready' },
  { part: 'TRAINING', shows: 'A pipeline run, stage by stage, live', how: '`train pipeline run --pipeline <name> --output-format stream-json` supervised after the person’s word: its `stage`, `iteration` and `notice` events, the manifest’s document last; Stop leaves it `aborted` and resumable (checked by probe)', status: 'ready' },
  { part: 'TRAINING', shows: 'The cycle: its state and passes, one pass live, halt and resume; its schedule the person’s own', how: '`train status --output-format json`’s `cycle`, null until the first pass, then `train cycle status --output-format json`; `train cycle run --output-format stream-json` supervised after the person’s word, its `pass` events and its pipeline’s; `halt` and `resume` asked first and read back, a halt taken during a pass kept since the CLI’s M30. launchd or cron runs it, never the app (checked by probe)', status: 'ready' },
  { part: 'TRAINING', shows: 'The cycle’s lock a dead pass left, and a history that cannot be read', how: '`train status` and `train cycle status --output-format json`’s `stale_lock`: its path and remedy, the State `stopped, its lock left behind`, Run one pass held, the file shown in the Finder and never removed; `train status`’s `cycle_error`: said where the history goes, Run one pass, Halt and Resume held, `train cycle status` not asked (GUI 6i)', status: 'ready' },
  { part: 'TRAINING', shows: 'Regimes and kits', how: '`train regime list | show`, `datasets kits --output-format json`; `train regime run` supervised', status: 'ready' },
  { part: 'TRAINING', shows: 'Datasets: create, pull, synthesize, prepare, delete', how: '`datasets list | info --output-format json`; `create`; `datasets pull <ref>` supervised, its lines as printed, Prepare it on the folder its last line names; `synth` and `prepare` supervised; `delete --yes` after the person’s word', status: 'ready' },
  { part: 'LIBRARY', shows: 'Agents: list, create, delete, own files edited in place', how: '`agents list --output-format json`; `agents create`; `delete --purge | --keep-files` as chosen; the prompt and schema files written where the CLI reads them, put back as they were after any save; Close asks before it loses typing', status: 'ready' },
  { part: 'LIBRARY', shows: 'An agent’s files checked on save', how: 'nothing checks an agent’s prompt or schema short of running it', status: 'gap', gap: 'G22' },
  { part: 'LIBRARY', shows: 'Symphonies: the catalog, a play, own spec files edited and checked', how: '`symphonies list | show --output-format json` (its refusal verbatim after a save; Close then offers to put the file back, read again by `show`); `play --output-format json` supervised, with `--image <path>` only where the list says `input.image`; `create --from`; `delete`', status: 'ready' },
  { part: 'LIBRARY', shows: 'MCP servers: connect state, presets, enable, a tool tried, removed', how: '`mcp list | presets --output-format json`; `mcp create --from` byte for byte; `enable`, `disable`; `mcp test`; `config delete-mcp-server <name>` after the person’s word, read back', status: 'ready' },
  { part: 'REVIEW', shows: 'A merge request reviewed, dry run first; its findings drawn', how: '`review mr <ref> --dry-run --output-format json` supervised, its narration (`[review] hunting with security`) the busy line; the `review.run` document: each finding’s file, line, claim and suggested fix, the counts, the batch’s fate', status: 'ready' },
  { part: 'REVIEW', shows: 'Posting, on the person’s click on the shown batch', how: '`review mr <ref> --yes`, passed only from that consent; the post is a run of its own, and its document is drawn after', status: 'ready' },
  { part: 'REVIEW', shows: 'Posting exactly the batch a dry run found', how: 'no verb posts a kept document: the post reviews again', status: 'gap', gap: 'G24' },
  { part: 'REVIEW', shows: 'Silence: nothing to say', how: '`posted.reason` `nothing to say: silence is a complete review`, drawn as a complete review', status: 'ready' },
  { part: 'REVIEW', shows: 'A forge’s refusal, with its fix', how: 'the CLI’s last words verbatim: `gh is not installed, or not on PATH … then: gh auth login`', status: 'ready' },
  { part: 'REVIEW', shows: 'Targets: list, add, remove; a sweep, dry or consented, its watermarks', how: '`review targets [add | delete] --output-format json`; `review sweep [--dry-run | --allow-post] --output-format json`, the `review.sweep` document: each target’s grant, watermarks before and after, each review, where it was kept', status: 'ready' },
  { part: 'REVIEW', shows: 'The review skills, listed and shown', how: '`review skills list | show --output-format json`: source, what each applies to, the prompt read only', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'Generate; run again, extend, draft and final; LoRAs', how: '`video generate` with `--from`, `--extend`, `--draft`, `--final`, `--lora`, `--no-lora`', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'A clip’s record and its history', how: 'the `video.generation` document, kept beside the clip as `<clip>.json`', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'The clips, compositions and speeches lists', how: 'the records found in `~/Movies/Apogee`, `~/Music/Apogee` and any folder the person adds', status: 'app' },
  { part: 'VIDEO AND VOICE', shows: 'Live steps while generating or speaking', how: '`--output-format stream-json`: `job`, `ready`, `step` n of N, `frames`, `assembling`, `segment`, `scene` and `notice`s, then the document', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'Stop a run', how: 'interrupt the child: exit 130, the output untouched, no record', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'A new composition planned before anything renders', how: '`video compose "<brief>" --dry-run`: one planning call, the project kept beside the video as `planned`; the same command without it renders that plan', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'Rendering progress, scene by scene', how: 'the `video.project` document beside the video, saved after every step', status: 'app' },
  { part: 'VIDEO AND VOICE', shows: 'Resume a stopped composition', how: 'the same command again; it renders only what the project lacks', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'Edit by instruction, re-roll, new voice, pipeline, music, captions', how: '`--from`, `--edit`, `--redo-scene`, `--voice`, `-m`, `--music`, `--no-captions`', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'The edit preview’s scene-by-scene diff', how: '`--dry-run` with `--from` prints the edited project: each scene’s `from`, `changes` and `moved`, and the parent’s scenes `removed`', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'Render after an edit’s preview: said to plan again, and the render compared with the preview', how: 'the edit’s `--dry-run` writes nothing (52c), so Render runs the same command and plans again, with the planner the preview’s `plan.backend` names; the rendered project’s own `from`, `changes` and narration, scene by scene, and `removed`, compared with the preview’s, never the seeds (GUI 6j)', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'Voices for the picker', how: '`models info kokoro-82m --output-format json`', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'A speech and its sentence timings', how: '`voice say … --output-format json`: the `voice.speech` document', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'The install prompt', how: 'a child on a pipe never asks: the app asks, then runs `train setup --with video|voice`', status: 'app' },
  { part: 'VIDEO AND VOICE', shows: 'Whether captions can be burned in', how: '`check --output-format json`’s `ffmpeg captions` row, `ok` or `skipped` with its remedy, read when the compose sheet opens', status: 'ready' },
  { part: 'VIDEO AND VOICE', shows: 'Default pipeline', how: '`config set-default-video`', status: 'ready' },
  { part: 'SETTINGS AND SYSTEM', shows: 'The console: any command, as typed', how: '`apogee <words…>` run as typed, its output streamed and stoppable; Tab asks `__complete`; `serve`, `acp`, `reset` and `uninstall` refused with the parity table’s reasons', status: 'ready' },
  { part: 'SETTINGS AND SYSTEM', shows: 'Backends, keys and providers', how: '`config add-backend | set-backend`; `auth add` with the key on stdin; `auth list`, `providers scan --output-format json`', status: 'ready' },
  { part: 'SETTINGS AND SYSTEM', shows: 'Health: what check finds, and fixing it, with Stop', how: '`check --output-format json`: what fails and warns, then each skipped row with its remedy; `check --fix` and `bootstrap` supervised, one at a time, Stop interrupting either; each end reads check again and says how it ended, a failure in its exit status or stderr’s last line, kept red until the next read; `bootstrap` starts nothing while another install runs', status: 'ready' },
  { part: 'SETTINGS AND SYSTEM', shows: 'Permissions and allowed websites', how: '`config set-permission`, `add-allowed-host`, `delete-allowed-host`', status: 'ready' },
  { part: 'SETTINGS AND SYSTEM', shows: 'Web search address; recall on or off', how: '`config get` reads them; `config set tools.search.url | memory.recall <value>` sets them', status: 'ready' },
  { part: 'SETTINGS AND SYSTEM', shows: 'Forget all session summaries', how: '`chats recall` says what it holds; `chats recall --forget --yes`, after the person’s word, removes every summary and keeps every transcript', status: 'ready' },
  { part: 'SETTINGS AND SYSTEM', shows: 'Xcode: what check says of it, its tools for Apogee’s agents, and the fields Xcode’s Add an Agent takes', how: '`check --output-format json`’s `Xcode` section, every row; `mcp presets --output-format json`’s `xcode` row, its `note`; `config get mcp_servers.xcode` for whether it is registered, never `mcp list`, which connects; `mcp create xcode --from xcode` and `config delete-mcp-server xcode`, each read back; `acp registration --output-format json`’s fields, each with Copy. Nothing written into Xcode', status: 'ready' },
  { part: 'SETTINGS AND SYSTEM', shows: 'Appearance', how: 'the app’s own theme files', status: 'app' },
  { part: 'SETTINGS AND SYSTEM', shows: 'CPU, memory, GPU, disk', how: '`system --output-format json`, every 2 s while the monitor is open', status: 'ready' },
  { part: 'SETTINGS AND SYSTEM', shows: 'Memory held by each child', how: 'the app reads its own children’s memory', status: 'app' },
  { part: 'SETTINGS AND SYSTEM', shows: 'How busy the GPU is, and its history', how: '`system`’s `gpu` says its `name` and `model_budget_bytes`, never how busy it is', status: 'gap', gap: 'G25' },
  { part: 'SETTINGS AND SYSTEM', shows: 'What each loaded model takes: its size, its weights and cache, its window', how: '`apogee.models_held` names the backends, with no size', status: 'gap', gap: 'G26' },
  { part: 'SETTINGS AND SYSTEM', shows: 'What Apogee’s other processes hold', how: '`apogee.process_bytes` is the reading process’s own; a session’s child and the media runs are counted nowhere', status: 'gap', gap: 'G27' },
  { part: 'SETTINGS AND SYSTEM', shows: 'The store, format by format', how: '`disk.store_bytes` is one total; the store listing carries `bytes` on some rows only', status: 'gap', gap: 'G28' },
  { part: 'SETTINGS AND SYSTEM', shows: 'The name of the store’s volume', how: '`disk` gives the volume’s free and total bytes, not its name', status: 'gap', gap: 'G29' },
  { part: 'SETTINGS AND SYSTEM', shows: 'Which models the sessions hold', how: 'a one-shot `system` reports only its own `models_held`, always empty: `none by this process -- a session holds its own`', status: 'gap', gap: 'G30' },
]

/**
 * The gaps to close in the CLI: the frozen board's G1 to G24, then G25 to G32,
 * the gaps the shipped items handed this page -- 1e's six from the monitor
 * (G25 to G30) and 1h's two from the Models place (G31, G32) -- each in the
 * CLI's words with its fallback, Later, as no call for you marks it before v1.
 */
export const contractGaps: readonly Gap[] = [
  { id: 'G1', title: 'Permission questions don’t carry the change', change: 'Add `detail` to the `write_file` and `edit_file` permission questions: a unified diff, as `fetch_url` already carries its URL.', when: 'Closed', note: 'it shipped with the CLI’s 52a, and the app draws it: the card’s line counts and Show changes, from the diff; checked by probe.' },
  { id: 'G2', title: 'No way to detach', change: 'An inbound `{"type":"detach","path":…}`, answered by a `notice`.', when: 'Closed', note: 'it shipped with the CLI’s 52d, and the app draws it: Detach on each attachment’s chip and /detach, said in the CLI’s notice; checked by probe.' },
  { id: 'G3', title: 'Recall doesn’t say which sessions', change: 'Add `sessions` to the `memory` event: each `id`, `name`, `updated`.', when: 'Closed', note: 'it shipped with the CLI’s 52a, and the app draws it: each recalled session named, a link that opens it; checked by probe.' },
  { id: 'G4', title: 'Answers don’t say which excerpts they drew on', change: 'A `sources` event before the answer: each excerpt’s file, page or lines, and score.', when: 'Closed', note: 'it shipped with the CLI’s 52a, and the app draws it: numbered source chips, each with its file and place; checked by probe.' },
  { id: 'G5', title: 'Settings need a restart', change: 'An inbound `{"type":"set", …}` for model, think, effort, temperature and system prompt, applied from the next turn and said in a `notice`.', when: 'Closed', note: 'it shipped with the CLI’s 52d, and the app uses it: a change waits for the turn to end, then goes as the line, and nothing loads again; checked by probe.' },
  { id: 'G6', title: 'No compact', change: 'A `{"type":"compact"}` line, and `chats compact <id>`.', when: 'Closed', note: 'it shipped with the CLI’s 52d, line and verb, and the app uses both: /compact sends the line, and a saved session’s Compact… runs the verb; checked by probe.' },
  { id: 'G7', title: 'No session that is never written', change: '`chat --private`: no transcript, no summary.', when: 'Closed', note: 'it shipped with the CLI’s 52d, and the app uses it: a private session runs with `--private`, so nothing is written and nothing is left to delete; checked by probe.' },
  { id: 'G8', title: 'The session list has no model', change: 'Add `backend` and `model` to each `chats list` row.', when: 'Closed', note: 'it shipped with the CLI’s 52a, and the app draws it: each row’s glyph is its backend’s kind; checked by probe.' },
  { id: 'G9', title: 'Download progress is text', change: '`models pull --output-format stream-json`: step, bytes done and total, then the stored entry.', when: 'Closed', note: 'it shipped with the CLI’s 52b, and the design draws it; checked by probe.' },
  { id: 'G10', title: 'A suite’s fit is text', change: '`models status --suite <name> --output-format json` says it as text in its `footprint` field, as it has since the CLI’s 27e. A typed form: each member’s weights, cache at its window, and the verdict `chat --suite` reaches, as fields.', when: 'Later', fallback: 'Show the CLI’s verdict and member lines as it writes them; the app’s own estimate, said as the app’s, only where the CLI states none.' },
  { id: 'G11', title: 'Media progress is text', change: '`--output-format stream-json` on `video generate`, `video compose` and `voice say`: `ready` (device, dtype), `step` n of N, `frames`, `assembling`, scene events, then the document.', when: 'Closed', note: 'it shipped with the CLI’s 52b, and the app draws it: a clip’s, a composition’s and a speech’s steps as they happen; checked by probe.' },
  { id: 'G12', title: 'A new composition can’t be planned alone', change: '`--dry-run` on a new brief prints the planned project; the same command without it renders that plan.', when: 'Closed', note: 'it shipped with the CLI’s 52c, and the app uses it: Plan first keeps the plan, and Render it renders that plan; checked by probe.' },
  { id: 'G13', title: 'The edit diff is text', change: 'In the dry-run document, each scene’s `from` and `changes`.', when: 'Closed', note: 'it shipped with the CLI’s 52c, and the app draws it: each scene kept, changed and with what, new, moved or removed, from the document; checked by probe.' },
  { id: 'G14', title: 'No check of ffmpeg’s filters', change: '`check` rows for ffmpeg, x264 and libass.', when: 'Closed', note: 'it shipped with the CLI’s 52e, and the app uses it: the compose sheet says up front whether captions burn in, and Health shows a skipped row with its remedy; checked by probe.' },
  { id: 'G15', title: 'No setter for plain config keys', change: '`config set <key> <value>` for scalar keys such as the web search address and `memory.recall`.', when: 'Closed', note: 'it shipped with the CLI’s 52e, and the app uses it: Permissions sets the web search address and recall; checked by probe.' },
  { id: 'G16', title: 'Recall can’t be forgotten', change: '`chats recall --forget`: every summary the recall index keeps, removed. Owned by the CLI’s 52e.', when: 'Closed', note: 'it shipped with the CLI’s 52e, and the app uses it: Forget every summary, asked first; checked by probe.' },
  { id: 'G17', title: 'A model’s details are display strings', change: '`models info <set>` adds a typed `data`: format, bytes, the window, the cache at that window and the lineage, null where the files state nothing. Owned by the CLI’s 52e.', when: 'Closed', note: 'it shipped with the CLI’s 52e, and the design draws it; checked by probe.' },
  { id: 'G18', title: 'A conversion’s progress is text', change: '`models convert` and `models quantize` gain `--output-format stream-json`: each step as it starts and ends, bytes written against the estimate, each set’s document as it lands, and the backends registered.', when: 'Before v1', fallback: 'Each step ticks on the set its own run names, once `models info` places it in the job’s own model folder (a quantize’s one GGUF, or each path a conversion prints), never on what appears in `models list`, where a sibling job’s set lands too; the running step shows how long it has run, with no percent, and the CLI’s text goes to the job’s log.' },
  { id: 'G19', title: 'Registering everything says its names as text', change: '`config scan --output-format json`, with or without `--register`: each unregistered set with the name it gets, then what was registered.', when: 'Before v1', fallback: 'Show `config scan`’s own report before, each stored model with the name it registers under in its words, and read the new names back from `models list` after.' },
  { id: 'G20', title: 'A check’s result is text', change: '`models repair <set> --output-format json`: each file with its expected and found size and digest, and what was fixed.', when: 'Before v1', fallback: 'Report pass or fail from the exit status, with the CLI’s text in the job’s log.' },
  { id: 'G21', title: 'A run’s progress is text', change: '`train run --output-format stream-json`: the trainer’s own steps (iteration, loss, learning rate) as the `iteration` events the CLI’s 55h gave `train pipeline` and `train cycle`.', when: 'Before v1', fallback: 'Show the run’s busy line in the CLI’s words, with Stop; decide nothing from it.' },
  { id: 'G22', title: 'An agent’s files are checked only when it runs', change: '`agents show <name> --output-format json`: the agent loaded as a run would load it, its prompt and schema read, each problem named -- no model call.', when: 'Later', fallback: 'Save the file and say that its first run checks it, in the CLI’s words.' },
  { id: 'G23', title: 'An execute session’s suite and its orchestration, in its session event', change: 'the `session` event gains `suite` and `orchestrating` (the count the terminal’s banner says, `orchestrating N`) -- additive.', when: 'Later', fallback: 'Say the suite from the door’s own flags and the config’s default, and orchestrating with no count.' },
  { id: 'G24', title: 'Post the batch a dry run found', change: '`review mr <ref> --post <document>`: a kept `review.run` document’s findings posted as its batch, the head checked unchanged -- additive.', when: 'Later', fallback: 'Say the post reviews again and posts what that run finds, and draw the posted document after.' },
  { id: 'G25', title: 'The GPU’s use isn’t read', change: '`system`’s `gpu` gains how busy it is, as `cpu.utilization` says the CPU’s, over the same window -- additive.', when: 'Later', fallback: 'Draw the GPU’s name and its model budget, with no percent and no history bars.' },
  { id: 'G26', title: 'A held model’s size isn’t said', change: '`system`’s `apogee` gains, beside `models_held` (which stays a list of names), each held model’s bytes: its weights, its cache at its window, and the window -- additive.', when: 'Later', fallback: 'Name each held model with no size, and no weights-and-cache split or window.' },
  { id: 'G27', title: 'Apogee’s other processes aren’t counted', change: '`system`’s `apogee` gains the bytes every `apogee` process holds, a session’s child and the media runs among them, beside `process_bytes`, the reading process’s own -- additive.', when: 'Later', fallback: 'Count Apogee’s share as `process_bytes` alone, and other apps as memory used less it, so Apogee’s share reads a few MB.' },
  { id: 'G28', title: 'The store’s size is one total', change: '`disk` gains the store’s bytes by format, or `models list --all` carries `bytes` on every row -- additive.', when: 'Later', fallback: 'Draw the one `disk.store_bytes` total, not a row per format.' },
  { id: 'G29', title: 'The store’s volume has no name', change: '`disk` gains the volume’s name beside `volume_available_bytes` and `volume_capacity_bytes` -- additive.', when: 'Later', fallback: 'Say “free on the store’s volume”, naming no volume.' },
  { id: 'G30', title: 'No reading names the models the sessions hold', change: '`system` gains the models each running session holds, by its process -- additive.', when: 'Later', fallback: 'Say “none by this process”, the CLI’s words, never “No models loaded”.' },
  { id: 'G31', title: 'A repository’s kind isn’t said before its pull', change: 'a read that says what a repository holds (a model, a diffusion pipeline, a voice model) before `models pull` stores it -- additive.', when: 'Later', fallback: 'The download sheet draws the model form for every repository; what it holds is said once its pull has stored it.' },
  { id: 'G32', title: 'The store’s free space comes only from `system`', change: '`models list --output-format json` gains the store volume’s free bytes, as `system` says them in `disk.volume_available_bytes` -- additive.', when: 'Later', fallback: 'The Models header says the sets’ stated total and no free space; the system monitor says the volume’s.' },
]

/** The decisions the design makes, each one vetoable. */
export const contractDecisions: readonly Lead[] = [
  { lead: 'Pipes only.', text: 'The app starts `apogee` as children and speaks over their stdin and stdout. No localhost server, ever (SPEC: interactive = pipes).' },
  { lead: 'One live chat child per session in view or mid-turn.', text: 'Moving away from an idle session ends its child and frees its model; coming back starts it again with `--resume`. A local model loads inside each child, so two live children on one model would hold it twice: the app never does that.' },
  { lead: 'Read back; don’t mirror.', text: 'After every change the app runs the read that shows it. It holds no copy of Apogee’s state beyond what is on screen. There is no push channel in protocol 1.' },
  { lead: 'The app’s own files are the app’s.', text: 'Themes, window layout, open sessions and watched folders live in `~/Library/Application Support/Apogee`. `~/.apogee` stays the CLI’s, so `reset` and `uninstall` keep meaning what they mean.' },
  { lead: 'Media goes to ~/Movies/Apogee and ~/Music/Apogee.', text: 'The library is the records found there and in folders the person adds. Nothing is ever written over; a taken name gets a number.' },
  { lead: 'Sessions ask; tasks never do.', text: 'Sessions run with `--tools` and `--permission-mode manual`, and the permission card answers. Tasks get explicit `--allow` grants up front, because a task in machine mode reads nothing.' },
  { lead: 'Types come from the schema.', text: 'The Swift protocol types are generated from `machine-schema.json` and regenerated when `capabilities.schema` moves. A different `protocol_version` is refused with the build it needs.' },
  { lead: 'SwiftUI first, the core kept portable.', text: 'macOS 27 or later, on Apple silicon. The protocol client, the process supervisor and the stores stay free of SwiftUI so the Qt and WinUI apps can follow the same shape.' },
  { lead: 'Where it lives.', text: '`lib/src/darwin`, then `linux` and `windows`, one app per platform, beside the CLI (SPEC: the GUI sibling applications).' },
  { lead: 'Narrow windows.', text: 'At least 960 × 640. Below 900 wide the sidebar becomes icons and side panes close.' },
]

/** The calls for you, each answered or recommended. */
export const contractCalls: readonly Lead[] = [
  { lead: 'Which gaps land before the app’s first release?', text: 'Answered: G1, G8, G9, G11 and G12 shipped with the CLI’s track 52 (52a to 52c), and G2 to G7 and G13 to G17 with them. Still open and marked before v1: G18, G19, G20 and G21.' },
  { lead: 'The lowest macOS version', text: 'Answered: macOS 27 or later, on Apple silicon only.' },
  { lead: 'If the CLI isn’t installed', text: 'Recommended: the app asks for it and links the installer, rather than carrying its own copy, so there is one binary to update.' },
]
