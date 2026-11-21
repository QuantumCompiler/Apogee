import { ComposeView } from '../../compose/Views/ComposeView'
import { StrictMode, useEffect } from 'react'
import { createRoot } from 'react-dom/client'
import { ConsoleView } from '../../console/Views/ConsoleView'
import type { GraphConfirm } from '../../graph/ViewModels/GraphFaces'
import { GraphScreen } from '../../graph/Views/GraphScreen'
import { DedupeSheet, SavePanel } from '../../graph/Views/GraphSheets'
import { KnowledgeScreen } from '../../knowledge/Views/KnowledgeScreen'
import type { LibraryConfirm as DefinitionsConfirm } from '../../library/ViewModels/LibraryFaces'
import { EditorSheet } from '../../library/Views/EditorSheet'
import { LibraryScreen } from '../../library/Views/LibraryScreen'
import type { ModelsConfirm } from '../../models/ViewModels/ModelsViewModel'
import { AdaptersSheet } from '../../models/Views/AdaptersSheet'
import { DownloadSheet } from '../../models/Views/DownloadsView'
import { ModelsScreen, PairDrafterSheet, RoleRows, RolesNotes } from '../../models/Views/ModelsScreen'
import { StoreSheet } from '../../models/Views/StoreView'
import { MonitorView } from '../../monitor/Views/MonitorView'
import { ReviewScreen } from '../../review/Views/ReviewScreen'
import { TargetSheet } from '../../review/Views/TargetSheet'
import { sessionSamples } from '../../session/Previews/SessionPresets'
import { SuiteSheet } from '../../session/Views/SuiteSheet'
import type { SettingsConfirm } from '../../settings/ViewModels/SettingsViewModel'
import { RegisterProviderSheet } from '../../settings/Views/BackendsPane'
import { SettingsView } from '../../settings/Views/SettingsView'
import type { LibraryConfirm } from '../../sessions/ViewModels/LibraryFaces'
import { ExecuteDoorSheet } from '../../sessions/Views/ExecuteDoor'
import { NewSessionButtons, RenameSheet, SessionList } from '../../sessions/Views/SessionList'
import { ChatInfoSheet, SessionsScreen } from '../../sessions/Views/SessionsScreen'
import { placeholderOf } from '../../shell/ViewModels/Destination'
import { SuitesScreen } from '../../suites/Views/SuitesScreen'
import { NewTaskSheet } from '../../tasks/Views/NewTaskSheet'
import { TaskRefusedAlert } from '../../tasks/Views/TaskRefusedAlert'
import { TasksScreen } from '../../tasks/Views/TasksScreen'
import { trainingConfirms } from '../../training/ViewModels/TrainingViewModel'
import type { TrainingConfirm } from '../../training/ViewModels/TrainingViewModel'
import { TrainingScreen } from '../../training/Views/TrainingScreen'
import { PromoteSheet, PullSheet } from '../../training/Views/TrainingSheets'
import { startRequest } from '../../shell/ViewModels/Routing'
import { ConfirmHost, SheetHost } from '../../shell/Views/Hosts'
import { PlaceholderScreen } from '../../shell/Views/PlaceholderScreen'
import { ShellView } from '../../shell/Views/ShellView'
import { InstallConsentSheet } from '../../styles/Components/InstallConsentSheet'
import { useObserved } from '../../styles/Observation/useObserved'
import { VideoScreen } from '../../video/Views/VideoScreen'
import { VoiceScreen } from '../../voice/Views/VoiceScreen'
import '../../styles/Stylesheet'
import { modeRules } from '../Previews/ModeRules'
import { presetFor } from '../Previews/Presets'
import { AppViewModel } from '../ViewModels/AppViewModel'

const libraryConfirms: readonly string[] = ['deleteChat', 'compactChat', 'forget'] satisfies readonly LibraryConfirm[]
const modelsConfirms: readonly string[] = ['model', 'storeDelete', 'storeModelDelete', 'unregister', 'registerAll'] satisfies readonly ModelsConfirm[]
const storeSheets: readonly string[] = ['storeConvert', 'storeQuantize', 'storeRegister', 'storeRename']
const settingsConfirms: readonly string[] = ['settingsForget', 'xcodeRemove'] satisfies readonly SettingsConfirm[]
const definitionsConfirms: readonly string[] = ['editorClose', 'mcpRemove'] satisfies readonly DefinitionsConfirm[]
const graphConfirms: readonly string[] = ['graphDelete', 'explainRefused'] satisfies readonly GraphConfirm[]

/**
 * The application's window (design 1e): the shell, handed each place's screen,
 * each overlay and Settings by the composition, as `ApogeeApp.swift` hands
 * `ShellView` its screens. Sessions is the Sessions place (1g): the session
 * window with the inspector beside it, and the session list and the
 * new-session buttons in the sidebar on every screen; a place not yet ported
 * is its placeholder; the monitor and the console are their own views. The
 * sheet and confirm hosts get their content by kind: the suite chooser and
 * the `endPrivate` confirm are the session's; the details, rename and
 * execute sheets and the delete, compact and forget confirms the library's;
 * Settings (1i) is its window, the Models place's roles in its Models &
 * roles slot, with its Register… sheet and its two confirms; Training (1j)
 * is its screen, with the promote and pull sheets, the five train confirms
 * and a refused promote's; Video (1k) is its screen, with the install
 * consent sheet, its compositions slot the compose module's view (1l);
 * Voice (1m) is its screen; Library and Review (1n) are their screens, with
 * the editor and the add-target sheet, the editor's close prompt, a
 * server's removal and the post sheet; Knowledge and the Code graph (1o) are
 * their screens, the Knowledge tab the composition's, with the graph's save
 * panel and dedupe sheet, its delete and a refused explain's alert; Suites
 * and Tasks (1p) are their screens, with the New task sheet and a refusal's
 * alert. It starts the app once it is on screen.
 */
export function ApogeeApp({ model }: { readonly model: AppViewModel }) {
  useObserved(model)
  useObserved(model.monitor)
  useObserved(model.library)
  useObserved(model.session)
  useObserved(model.models)
  useObserved(model.training)
  useObserved(model.video)
  useObserved(model.compose)
  useObserved(model.voice)
  useObserved(model.definitions)
  useObserved(model.review)
  useObserved(model.graph)
  useObserved(model.suites)
  useObserved(model.tasks)
  useEffect(() => {
    void model.start()
    return () => model.stop()
  }, [model])
  const shell = model.shell
  const session = model.session
  const library = model.library
  const models = model.models
  const training = model.training
  const video = model.video
  const compose = model.compose
  return (
    <ShellView
      model={shell}
      strip={model.strip}
      running={model.placeRuns}
      sessions={() => <SessionList model={library} />}
      newSessions={() => <NewSessionButtons model={library} />}
      screen={(destination) => {
        // Each feature item puts its place's screen here, and takes its placeholder out of Destination.ts.
        if (destination === 'sessions') return <SessionsScreen model={library} />
        if (destination === 'models') return <ModelsScreen model={models} />
        if (destination === 'training') return <TrainingScreen model={training} />
        // Video (1k), its compositions slot filled with the compose module's view (1l), so neither imports the other.
        if (destination === 'video') return <VideoScreen model={video} compositions={() => <ComposeView model={compose} />} />
        // Voice (1m): its speeches, the speak card inline and the selected speech.
        if (destination === 'voice') return <VoiceScreen model={model.voice} />
        // Library and Review (1n).
        if (destination === 'library') return <LibraryScreen model={model.definitions} />
        if (destination === 'review') return <ReviewScreen model={model.review} />
        // Knowledge and the Code graph (1o): the tab shown is the composition's, as the Mac's screen keeps it.
        if (destination === 'knowledge') return <KnowledgeScreen records={model.records} collections={model.collections} tab={model.knowledgeTab} pickTab={(tab) => model.pickKnowledgeTab(tab)} />
        if (destination === 'graph') return <GraphScreen model={model.graph} />
        // Suites and Tasks (1p): a chat under a suite, the session's switch and the default through the composition's doors.
        if (destination === 'suites') return <SuitesScreen model={model.suites} />
        if (destination === 'tasks') return <TasksScreen model={model.tasks} />
        const placeholder = placeholderOf(destination)
        return placeholder ? <PlaceholderScreen placeholder={placeholder} /> : null
      }}
      overlay={(overlay) => (overlay === 'monitor' ? <MonitorView model={model.monitor} close={() => shell.closeOverlay()} /> : <ConsoleView model={model.console} close={() => shell.closeOverlay()} />)}
      settings={() => (
        <SettingsView
          model={model.settings}
          close={() => model.closeSettings()}
          // Models & roles: the Models place's roles, so settings never imports models (1i).
          roles={() => (
            <>
              <RolesNotes model={models} margin="0 0 6px" />
              <RoleRows model={models} picker={240} />
            </>
          )}
        />
      )}
      sheet={(kind) => {
        if (kind === 'suite') {
          const sheet = session.face().suiteSheet
          return (
            <SheetHost
              title="Run this session under"
              subtitle={sheet.now}
              width={560}
              right={[
                { label: 'Cancel', run: () => model.closeSuites() },
                { label: 'Switch', kind: 'btn-p', run: () => model.switchSuite(), disabled: !sheet.canSwitch },
              ]}
            >
              <SuiteSheet model={session} />
            </SheetHost>
          )
        }
        if (kind === 'chatInfo' && library.info) {
          const chat = library.info.chat
          return (
            <SheetHost
              title={library.info.title}
              width={460}
              left={[
                {
                  label: 'Compact…',
                  run: () => {
                    library.closeInfo()
                    library.askToCompact(chat)
                  },
                  disabled: !library.canCompact(chat),
                },
              ]}
              right={[{ label: 'Done', kind: 'btn-p', run: () => library.closeInfo() }]}
            >
              <ChatInfoSheet model={library} />
            </SheetHost>
          )
        }
        if (kind === 'rename' && library.renaming) {
          return (
            <SheetHost
              title="Rename session"
              width={460}
              right={[
                { label: 'Cancel', run: () => library.cancelRename() },
                { label: 'Rename', kind: 'btn-p', run: () => void library.rename(), disabled: !library.renaming.canRename },
              ]}
            >
              <RenameSheet model={library} />
            </SheetHost>
          )
        }
        // The Models place's sheets (1h): the four store sheets, the download sheet, the adapters and the pair sheets.
        const store = models.store.sheet()
        if (storeSheets.includes(kind) && store) {
          return (
            <SheetHost
              title={store.title}
              width={600}
              right={[
                { label: 'Cancel', run: () => models.store.cancelSheet() },
                { label: store.goLabel, kind: 'btn-p', run: () => models.store.go(), disabled: !store.canGo },
              ]}
            >
              <StoreSheet model={models.store} />
            </SheetHost>
          )
        }
        if (kind === 'download' && models.downloadForm) {
          return (
            <SheetHost
              title="Download from Hugging Face"
              subtitle={models.face().downloadSheet.intro}
              width={580}
              right={[
                { label: 'Cancel', run: () => models.cancelDownload() },
                { label: 'Start download', kind: 'btn-p', run: () => models.startDownload(), disabled: !models.canStartDownload },
              ]}
            >
              <DownloadSheet model={models} />
            </SheetHost>
          )
        }
        if (kind === 'adapters' && models.editingAdapters) {
          const sheet = models.face().adaptersSheet
          return (
            <SheetHost
              title={sheet.title}
              width={560}
              right={[
                { label: 'Cancel', run: () => models.closeAdapters() },
                { label: sheet.applyLabel, kind: 'btn-p', run: () => void models.applyAdapters() },
              ]}
            >
              <AdaptersSheet model={models} />
            </SheetHost>
          )
        }
        if (kind === 'pairDrafter' && models.pairingDrafter) {
          const sheet = models.face().pairSheet
          return (
            <SheetHost
              title={sheet.title}
              width={560}
              right={[
                { label: 'Cancel', run: () => models.closePairing() },
                { label: 'Pair', kind: 'btn-p', run: () => void models.pairDrafter(), disabled: !sheet.canPair },
              ]}
            >
              <PairDrafterSheet model={models} />
            </SheetHost>
          )
        }
        // Training's sheets (1j): the promote sheet and the pull sheet, the old prototype's.
        if (kind === 'promote') {
          const sheet = training.face().promoteSheet
          return (
            <SheetHost
              title={sheet.title}
              width={560}
              right={[
                { label: 'Cancel', run: () => training.cancelPromote() },
                { label: 'Promote', kind: 'btn-p', run: () => training.promote(), disabled: !sheet.canPromote },
              ]}
            >
              <PromoteSheet model={training} />
            </SheetHost>
          )
        }
        if (kind === 'pull') {
          return (
            <SheetHost
              title="Pull a dataset"
              width={560}
              right={[
                { label: 'Cancel', run: () => training.cancelPull() },
                { label: 'Pull', kind: 'btn-p', run: () => training.pull(), disabled: !training.face().pullSheet.canPull },
              ]}
            >
              <PullSheet model={training} />
            </SheetHost>
          )
        }
        // The install consent (1k): the old rtc sheet, its content styles' InstallConsentSheet, asked by Video's refused run or Settings' Install….
        if (kind === 'runtimeConsent') {
          const consent = video.face().consent
          if (!consent.shown) return null
          return (
            <SheetHost
              title={consent.title}
              width={580}
              right={[
                { label: consent.cancel, run: () => video.decline() },
                { label: consent.confirm, kind: 'btn-p', run: () => video.install() },
              ]}
            >
              <InstallConsentSheet said={consent.said} body={consent.body} command={consent.command} />
            </SheetHost>
          )
        }
        // Settings' Register… (1i): the old registerProvider sheet, in the TUI's words.
        if (kind === 'registerProvider' && model.settings.face().register.shown) {
          const register = model.settings.face().register
          return (
            <SheetHost
              title={register.title}
              subtitle={register.subtitle}
              width={520}
              right={[
                { label: 'Cancel', run: () => model.cancelRegister() },
                { label: 'Register', kind: 'btn-p', run: () => void model.settings.register(), disabled: !register.canRegister },
              ]}
            >
              <RegisterProviderSheet model={model.settings} />
            </SheetHost>
          )
        }
        // Library's editor (1n): the file's path under its title in the code font, Put it back as it was on the left once offered, Close and Save.
        if (kind === 'editor') {
          const editor = model.definitions.editorFace()
          if (!editor.shown) return null
          return (
            <SheetHost
              title={editor.title}
              subtitle={editor.path}
              subtitleMono
              width={720}
              left={editor.restore.shown ? [{ label: 'Put it back as it was', run: () => void model.definitions.restore(), disabled: !editor.restore.enabled }] : []}
              right={[
                { label: 'Close', run: () => model.definitions.requestClose() },
                { label: 'Save', kind: 'btn-p', run: () => void model.definitions.save(), disabled: !editor.canSave },
              ]}
            >
              <EditorSheet model={model.definitions} />
            </SheetHost>
          )
        }
        // Review's add-target sheet (1n): its command live, Add on once it names a target and where.
        if (kind === 'target' && model.review.targetSheet().shown) {
          return (
            <SheetHost
              title="Add a sweep target"
              width={520}
              right={[
                { label: 'Cancel', run: () => model.review.cancelTarget() },
                { label: 'Add', kind: 'btn-p', run: () => void model.review.addTarget(), disabled: !model.review.targetSheet().canAdd },
              ]}
            >
              <TargetSheet model={model.review} />
            </SheetHost>
          )
        }
        // The Code graph's save panel and dedupe sheet (1o): the old generic sheet's savePanel and dedupe kinds.
        if (kind === 'savePanel' && model.graph.savePanel().shown) {
          return (
            <SheetHost
              title="Save As"
              width={460}
              right={[
                { label: 'Cancel', run: () => model.graph.cancelSave() },
                { label: 'Save', kind: 'btn-p', run: () => void model.graph.save(), disabled: !model.graph.savePanel().canSave },
              ]}
            >
              <SavePanel model={model.graph} />
            </SheetHost>
          )
        }
        if (kind === 'dedupe' && model.graph.dedupeSheet().shown) {
          const dedupe = model.graph.dedupeSheet()
          return (
            <SheetHost
              title={dedupe.title}
              width={560}
              right={
                dedupe.merges
                  ? [
                      { label: 'Cancel', run: () => model.graph.dismissDedupe() },
                      { label: 'Merge them', kind: 'btn-p', run: () => model.graph.confirmDedupe(), destructive: true },
                    ]
                  : [{ label: 'Done', kind: 'btn-p', run: () => model.graph.dismissDedupe(), disabled: !dedupe.done }]
              }
            >
              <DedupeSheet model={model.graph} />
            </SheetHost>
          )
        }
        // Tasks' New task sheet (1p): the old one's, its Start task off until it has a goal, or a recipe's input.
        if (kind === 'newtask' && model.tasks.sheetFace().shown) {
          return (
            <SheetHost
              title="New task"
              subtitle="Say what done looks like. The task plans, then works in rounds, checking itself after each, until its checks pass or its rounds run out."
              width={600}
              right={[
                { label: 'Cancel', run: () => model.tasks.cancelNew() },
                { label: 'Start task', kind: 'btn-p', run: () => model.tasks.start(), disabled: model.tasks.sheetFace().cantStart },
              ]}
            >
              <NewTaskSheet model={model.tasks} />
            </SheetHost>
          )
        }
        if (kind === 'execute' && library.executeDoorOpen) {
          return (
            <SheetHost
              title="New execute session"
              subtitle="A session under a suite, its symphonies playable with /play -- or bare on one model."
              width={560}
              right={[
                { label: 'Cancel', run: () => library.closeExecuteDoor() },
                { label: 'Open', kind: 'btn-p', run: () => void library.openExecute() },
              ]}
            >
              <ExecuteDoorSheet model={library} />
            </SheetHost>
          )
        }
        return null
      }}
      confirm={(kind) => {
        if (modelsConfirms.includes(kind)) {
          const confirm = kind as ModelsConfirm
          const words = models.confirmWords(confirm)
          if (!words) return null
          return (
            <ConfirmHost
              title={words.title}
              body={words.body}
              detail={words.detail}
              cancel={{ label: 'Cancel', run: () => model.dismissModels(confirm) }}
              {...(words.alternative !== '' ? { alternative: { label: words.alternative, run: () => void model.confirmModels(confirm, true) } } : {})}
              confirm={{ label: words.confirm, run: () => void model.confirmModels(confirm, false), destructive: words.destructive }}
            />
          )
        }
        // Training's (1j): the five train acts, and a promotion the CLI refused, its line word for word.
        if ((trainingConfirms as readonly string[]).includes(kind)) {
          const confirm = kind as TrainingConfirm
          const words = training.confirmWords(confirm)
          if (!words) return null
          return (
            <ConfirmHost
              title={words.title}
              body={words.body}
              {...(words.detail === undefined ? {} : { detail: words.detail })}
              cancel={{ label: words.cancel, run: () => model.dismissTraining(confirm) }}
              confirm={{ label: words.confirm, run: () => void model.confirmTraining(confirm) }}
            />
          )
        }
        if (settingsConfirms.includes(kind)) {
          const confirm = kind as SettingsConfirm
          const words = model.settings.confirmWords(confirm)
          if (!words) return null
          return <ConfirmHost title={words.title} body={words.body} cancel={{ label: 'Cancel', run: () => model.dismissSettings(confirm) }} confirm={{ label: words.confirm, run: () => void model.confirmSettings(confirm), destructive: true }} />
        }
        // Library's (1n): the editor's close prompt -- Discard, or after a refused save Put it back as it was and Leave it -- and a server's removal.
        if (definitionsConfirms.includes(kind)) {
          const confirm = kind as DefinitionsConfirm
          const words = model.definitions.confirmWords(confirm)
          if (!words) return null
          return (
            <ConfirmHost
              title={words.title}
              body={words.body}
              cancel={{ label: words.cancel, run: () => model.dismissDefinitions(confirm) }}
              {...(words.alternative !== '' ? { alternative: { label: words.alternative, run: () => void model.confirmDefinitions(confirm, true) } } : {})}
              confirm={{ label: words.confirm, run: () => void model.confirmDefinitions(confirm, false), destructive: words.destructive }}
            />
          )
        }
        // The Code graph's (1o): Delete the graph in the CLI's words, and a node's explain refused -- OK alone, the refusal's Clear.
        if (graphConfirms.includes(kind)) {
          const confirm = kind as GraphConfirm
          const words = model.graph.confirmWords(confirm)
          if (!words) return null
          return (
            <ConfirmHost
              title={words.title}
              body={words.body}
              {...(words.cancel === '' ? {} : { cancel: { label: words.cancel, run: () => model.dismissGraph(confirm) } })}
              confirm={{ label: words.confirm, run: () => void model.confirmGraph(confirm), destructive: words.destructive }}
            />
          )
        }
        // Tasks' alert (1p): a start or resume the CLI refused before it began, its line word for word, OK alone.
        if (kind === 'taskRefused') return <TaskRefusedAlert model={model.tasks} ok={() => model.confirmTasks()} />
        // Review's post sheet (1n): the batch said, the command that posts it, and Post closing it as the old one does.
        if (kind === 'reviewPost') {
          const words = model.review.postWords
          if (!words) return null
          return <ConfirmHost title={words.title} body={words.body} detail={words.command} cancel={{ label: 'Cancel', run: () => model.dismissReviewPost() }} confirm={{ label: 'Post', run: () => model.confirmReviewPost() }} />
        }
        if (libraryConfirms.includes(kind)) {
          const words = library.confirmWords(kind as LibraryConfirm)
          if (!words) return null
          return <ConfirmHost title={words.title} body={words.body} cancel={{ label: words.cancel, run: () => model.dismissLibrary() }} confirm={{ label: words.confirm, run: () => model.confirmLibrary(), destructive: true }} />
        }
        const words = kind === 'endPrivate' ? session.endPrivate : undefined
        if (!words) return null
        return (
          <ConfirmHost
            title={words.title}
            body={words.body}
            cancel={{ label: words.cancel, run: () => model.keepRunning() }}
            confirm={{ label: words.confirm, run: () => model.endPrivateAndLeave() }}
          />
        )
      }}
    />
  )
}

/**
 * Mounts the app in `element`: what the entry point, source/main.tsx, calls
 * and nothing else. The start preset is the page's to ask for, in its query
 * or its hash, as a double-clicked file keeps both.
 */
export function start(element: HTMLElement | null): void {
  if (!element) throw new Error('main.html has no #root element to mount the app in')
  const { preset } = presetFor(startRequest(window.location))
  createRoot(element).render(
    <StrictMode>
      <ApogeeApp model={new AppViewModel(preset, modeRules, undefined, sessionSamples)} />
    </StrictMode>,
  )
}
