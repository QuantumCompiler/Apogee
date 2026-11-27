import type { SpecimenSection } from '../../styles/Specimens/Specimen'

/**
 * The gallery's board table (design 1q): the claude.ai canvas's index, the
 * old prototype's `canvas.json` (its blob `cf7633cb` at 1c's baseline
 * revision `b375fac9`), converted once and committed here. Since 1t retired
 * the old prototype and the canvas, with the test that held this table to
 * that file, the table is the record. Its pages and the headings over their
 * rows, and one entry per board, in table order: a page's rows top to
 * bottom, each row's boards by their canvas position, left to right.
 *
 * Exploration's five boards are not ported (the item's default): they stay in
 * git history and in 1c's baseline pictures. Every start preset the
 * stand-in's table holds has one frame: each Screen board's, and after them, at
 * the end of their row, the presets no old board draws, titled in the
 * canvas's "Place · state" form and marked `noOldBoard`, for 1s to judge
 * against its old pair.
 */

/** The gallery's two pages, the canvas's Build and Foundations. */
export type GalleryPage = 'build' | 'foundations'

export const galleryPages: ReadonlyArray<{ readonly id: GalleryPage; readonly name: string }> = [
  { id: 'build', name: 'Build' },
  { id: 'foundations', name: 'Foundations' },
]

/**
 * What a board draws: the app mounted at a start preset, still (`screen`);
 * the app live (`app`, the canvas's App board, `main.html` itself); a Build
 * page over its typed data (`contract`, `layout`, `processes`, design 1r); a
 * foundations page generated from the tokens (`colors`, `type`), a
 * Components board's section of the modules' own specimens
 * (`components`), or the logo (`logo`). Since 1r no board waits on an item:
 * the plain "Not ported yet" card is a Components section's with no specimen.
 */
export type BoardDraws = 'screen' | 'app' | 'contract' | 'layout' | 'processes' | 'colors' | 'type' | 'components' | 'logo'

/** One board: the canvas's entry for it, as the gallery lays it out. */
export interface BoardEntry {
  /** The old board's name (`ScreenModels`), so 1s pairs it with its baseline picture; a board-less preset's own name. */
  readonly id: string
  /** A screen board's start preset, the one line its file varies (`<dc-import name="App" start="…">`). */
  readonly start?: string
  /** The canvas's title: its curated one, not the file's `<title>`. */
  readonly title: string
  readonly page: GalleryPage
  /** Its row on its page, from the top: the canvas's rows at y 0, 1400, 2720, 4040 and 5360 on Build, and 0 on Foundations. */
  readonly row: number
  /** Its height on the canvas, in px; every board is `boardWidth` wide. */
  readonly height: number
  readonly draws: BoardDraws
  /** A Components board's section. */
  readonly section?: SpecimenSection
  /** On a Components board, the item that first ported its section, named on the card a section with no specimen shows. */
  readonly item?: string
  /** The old board's prop menu, each prop's choices (1c shot one picture per choice). The gallery's own switcher is the theme everywhere and the `apogee` mode on the app. */
  readonly props?: Readonly<Record<string, readonly string[]>>
  /** A preset no old board drew (the item's default): 1s settles it. */
  readonly noOldBoard?: true
}

/** A heading over a row, the canvas's `title1` note 300 px above it. */
export interface BoardHeading {
  readonly text: string
  readonly page: GalleryPage
  readonly row: number
}

/** Every board's width on the canvas: a 1440 px frame (the column step is 1520, with its 80 px gutter). */
export const boardWidth = 1440

/** The boards, in table order. */
export const boards: readonly BoardEntry[] = [
  { id: 'BuildContract', title: 'Build · how the app talks to Apogee', page: 'build', row: 0, height: 5600, draws: 'contract' },
  { id: 'BuildLayout', title: 'Build · layout, navigation and native controls', page: 'build', row: 0, height: 3700, draws: 'layout' },
  { id: 'App', title: 'Apogee · the whole app (interactive)', page: 'build', row: 0, height: 900, draws: 'app', props: { apogee: ['installed', 'missing', 'config-typo'] } },
  { id: 'BuildProcesses', title: 'Build · how the app runs Apogee', page: 'build', row: 0, height: 1040, draws: 'processes' },
  { id: 'ScreenSessionStreaming', start: 'chat', title: 'Session · streaming, thinking open', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenSessionPermission', start: 'chat:retry', title: 'Session · asking permission', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenSessionSources', start: 'chat:wal', title: 'Session · a source opened', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenSessionError', start: 'chat:crash', title: 'Session · a model didn’t load', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenSessionQuestion', start: 'chat:pr', title: 'Session · the model asks you', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenSessionNew', start: 'chat:new', title: 'Session · new and empty', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenSessionPrivate', start: 'chat:private', title: 'Session · private', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenMonitor', start: 'monitor', title: 'System monitor', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenConsole', start: 'console', title: 'Console · any command, as typed', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenSessionPlay', start: 'chat:play', title: 'Session · a symphony played', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenSessionExecute', start: 'execute', title: 'Sessions · the execute door', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenSessionSuite', start: 'chat:suite', title: 'Session · choosing a suite', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenSessionRestarting', start: 'chat:restarting', title: 'Session · restarting, a line held', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenSessionNotSent', start: 'chat:notsent', title: 'Session · a message not sent', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'ScreenSessionEnded', start: 'chat:ended', title: 'Session · ended, its lines kept', page: 'build', row: 1, height: 900, draws: 'screen' },
  { id: 'chat:compacting', start: 'chat:compacting', title: 'Session · saved chats compacting', page: 'build', row: 1, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'ScreenModels', start: 'models', title: 'Models · backends, one open', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenModelsStore', start: 'models:store', title: 'Models · stored weights, one open', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenModelsConvert', start: 'models:convert', title: 'Models · converting weights', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenModelsRegister', start: 'models:register', title: 'Models · registering a stored set', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenModelsDownloads', start: 'models:downloads', title: 'Models · jobs', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenModelsRoles', start: 'models:roles', title: 'Models · roles', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenDownload', start: 'download', title: 'Models · downloading a pipeline', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenSuites', start: 'suites', title: 'Suites', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenTasks', start: 'tasks', title: 'Tasks · running', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenNewTask', start: 'newtask', title: 'Tasks · new task', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenGraph', start: 'graph', title: 'Code graph', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenGraphProse', start: 'graph:prose', title: 'Code graph · a collection’s prose graph', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenKnowledge', start: 'knowledge', title: 'Knowledge · records, a question answered', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenCollections', start: 'knowledge:collections', title: 'Knowledge · collections, a search', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenTraining', start: 'training', title: 'Training · a run, gated', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenTrainingDatasets', start: 'training:datasets', title: 'Training · datasets and kits', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenLibrary', start: 'library', title: 'Library · agents', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenLibrarySymphonies', start: 'library:symphonies', title: 'Library · a symphony played', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenLibraryMCP', start: 'library:mcp', title: 'Library · MCP servers and presets', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenNewTaskRecipe', start: 'newtask:recipe', title: 'Tasks · new task from a recipe', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenReview', start: 'review', title: 'Review · a dry run, its findings', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenReviewPost', start: 'review:post', title: 'Review · posting, asked first', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenReviewTargets', start: 'review:targets', title: 'Review · targets and the last sweep', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenReviewSkills', start: 'review:skills', title: 'Review · the skills', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenTrainingPipelines', start: 'training:pipelines', title: 'Training · a pipeline run, stage by stage', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenTrainingCycle', start: 'training:cycle', title: 'Training · the cycle and its passes', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenModelsDelete', start: 'models:delete', title: 'Models · a delete, the CLI’s plan first', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenLibraryRefused', start: 'library:refused', title: 'Library · a refused save, asked on Close', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenLibraryImage', start: 'library:image', title: 'Library · a symphony that takes an image', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenTasksDenied', start: 'tasks:denied', title: 'Tasks · grants, allowed and denied', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenTasksRefused', start: 'tasks:refused', title: 'Tasks · a refused start, the CLI’s words', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenGraphRefused', start: 'graph:refused', title: 'Code graph · an explain refused, the CLI’s words', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenTrainingStaleLock', start: 'training:stalelock', title: 'Training · a dead pass’s lock, Run held', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenTrainingCyclePass', start: 'training:cyclepass', title: 'Training · a cycle pass, never resumed', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'ScreenTrainingResumeHeld', start: 'training:resumeheld', title: 'Training · a Resume held, the CLI’s words', page: 'build', row: 2, height: 900, draws: 'screen' },
  { id: 'models:notran', start: 'models:notran', title: 'Models · a job stopped before it ran', page: 'build', row: 2, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'training:unreadable', start: 'training:unreadable', title: 'Training · a cycle history it cannot read', page: 'build', row: 2, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'training:passrunning', start: 'training:passrunning', title: 'Training · a cycle pass running', page: 'build', row: 2, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'training:regimes', start: 'training:regimes', title: 'Training · regimes', page: 'build', row: 2, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'library:unsaved', start: 'library:unsaved', title: 'Library · unsaved typing, asked on Close', page: 'build', row: 2, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'knowledge:mixed', start: 'knowledge:mixed', title: 'Knowledge · a collection of two models’ vectors', page: 'build', row: 2, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'tasks:more', start: 'tasks:more', title: 'Tasks · a ledger past 50', page: 'build', row: 2, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'tasks:resuming', start: 'tasks:resuming', title: 'Tasks · a resume starting up', page: 'build', row: 2, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'tasks:resumerefused', start: 'tasks:resumerefused', title: 'Tasks · a refused resume, the CLI’s words', page: 'build', row: 2, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'knowledge:ingested-with-a-note', start: 'knowledge:ingested-with-a-note', title: 'Knowledge · an ingest that left a note, the CLI’s words', page: 'build', row: 2, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'knowledge:captured-with-a-note', start: 'knowledge:captured-with-a-note', title: 'Knowledge · a capture that left a note, the CLI’s words', page: 'build', row: 2, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'graph:built-with-a-note', start: 'graph:built-with-a-note', title: 'Graph · a rebuild that left a note, the CLI’s words', page: 'build', row: 2, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'ScreenVideoClip', start: 'video', title: 'Video · a clip and its record', page: 'build', row: 3, height: 900, draws: 'screen' },
  { id: 'ScreenVideoRun', start: 'video:run', title: 'Video · generating a final', page: 'build', row: 3, height: 900, draws: 'screen' },
  { id: 'ScreenVideoConsent', start: 'consent', title: 'Video · LoRAs need peft', page: 'build', row: 3, height: 900, draws: 'screen' },
  { id: 'ScreenCompose', start: 'compose', title: 'Video · a composition', page: 'build', row: 3, height: 900, draws: 'screen' },
  { id: 'ScreenComposeEdit', start: 'compose:edit', title: 'Video · previewing an edit', page: 'build', row: 3, height: 900, draws: 'screen' },
  { id: 'ScreenComposeNew', start: 'compose:new', title: 'Video · a new composition, planned', page: 'build', row: 3, height: 900, draws: 'screen' },
  { id: 'ScreenComposeStopped', start: 'compose:tide', title: 'Video · a stopped composition', page: 'build', row: 3, height: 900, draws: 'screen' },
  { id: 'ScreenVoice', start: 'voice', title: 'Voice', page: 'build', row: 3, height: 900, draws: 'screen' },
  { id: 'ScreenVideoInstall', start: 'video:install', title: 'Video · installing what a run needs', page: 'build', row: 3, height: 900, draws: 'screen' },
  { id: 'ScreenComposeDiffers', start: 'compose:differs', title: 'Video · a rendered edit that differs from its preview', page: 'build', row: 3, height: 900, draws: 'screen' },
  { id: 'ScreenSettingsGeneral', start: 'settings:general', title: 'Settings · General', page: 'build', row: 4, height: 900, draws: 'screen' },
  { id: 'ScreenSettingsAppearance', start: 'settings:appearance', title: 'Settings · Appearance', page: 'build', row: 4, height: 900, draws: 'screen' },
  { id: 'ScreenSettingsMedia', start: 'settings:media', title: 'Settings · Video & voice', page: 'build', row: 4, height: 900, draws: 'screen' },
  { id: 'ScreenSettingsBackends', start: 'settings:backends', title: 'Settings · Backends & keys', page: 'build', row: 4, height: 900, draws: 'screen' },
  { id: 'ScreenSettingsPermissions', start: 'settings:permissions', title: 'Settings · Permissions', page: 'build', row: 4, height: 900, draws: 'screen' },
  { id: 'ScreenSettingsXcode', start: 'settings:xcode', title: 'Settings · Xcode', page: 'build', row: 4, height: 900, draws: 'screen' },
  { id: 'settings:fixing', start: 'settings:fixing', title: 'Settings · Backends & keys, fixing', page: 'build', row: 4, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'settings:fixfailed', start: 'settings:fixfailed', title: 'Settings · a fix that failed', page: 'build', row: 4, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'settings:nosearch', start: 'settings:nosearch', title: 'Settings · Web access, no search address', page: 'build', row: 4, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'settings:roles', start: 'settings:roles', title: 'Settings · Models & roles', page: 'build', row: 4, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'settings:web', start: 'settings:web', title: 'Settings · Web access', page: 'build', row: 4, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'settings:memory', start: 'settings:memory', title: 'Settings · Memory', page: 'build', row: 4, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'settings:keyboard', start: 'settings:keyboard', title: 'Settings · Keyboard', page: 'build', row: 4, height: 900, draws: 'screen', noOldBoard: true },
  { id: 'ColorTokens', title: 'Foundations · Color tokens', page: 'foundations', row: 0, height: 1520, draws: 'colors', props: { chrome: ['dark', 'light'] } },
  { id: 'TypeSpace', title: 'Foundations · Type, space, shape', page: 'foundations', row: 0, height: 2850, draws: 'type', props: { theme: ['dark', 'light'] } },
  { id: 'ComponentsModel', title: 'Components · Model, context, sources', page: 'foundations', row: 0, height: 2400, draws: 'components', section: 'model', item: '1u', props: { theme: ['dark', 'light', 'contrast'] } },
  { id: 'ComponentsTurn', title: 'Components · The turn', page: 'foundations', row: 0, height: 2250, draws: 'components', section: 'turn', item: '1u', props: { theme: ['dark', 'light', 'contrast'] } },
  { id: 'ComponentsPrompts', title: 'Components · Asking and failing', page: 'foundations', row: 0, height: 2050, draws: 'components', section: 'asking', item: '1v', props: { theme: ['dark', 'light', 'contrast'] } },
  { id: 'ComponentsMedia', title: 'Components · Making media', page: 'foundations', row: 0, height: 3300, draws: 'components', section: 'media', item: '1k', props: { theme: ['dark', 'light', 'contrast'] } },
  { id: 'Logo', title: 'Foundations · The logo', page: 'foundations', row: 0, height: 760, draws: 'logo', props: { theme: ['dark', 'light'] } },
]

/** The headings: Build's five and Foundations' one (Exploration's two are left out with its boards). */
export const boardHeadings: readonly BoardHeading[] = [
  { text: 'Build — the contract, the measurements, and the app itself', page: 'build', row: 0 },
  { text: 'Screens — sessions, the system monitor and the console', page: 'build', row: 1 },
  { text: 'Screens — models, suites, tasks, the code graph, knowledge, training, the library and review', page: 'build', row: 2 },
  { text: 'Screens — video and voice', page: 'build', row: 3 },
  { text: 'Screens — settings', page: 'build', row: 4 },
  { text: 'Foundations — color, type, space and components', page: 'foundations', row: 0 },
]
