import { StrictMode, useEffect } from 'react'
import { createRoot } from 'react-dom/client'
import { ConsoleView } from '../../console/Views/ConsoleView'
import type { ModelsConfirm } from '../../models/ViewModels/ModelsViewModel'
import { AdaptersSheet } from '../../models/Views/AdaptersSheet'
import { DownloadSheet } from '../../models/Views/DownloadsView'
import { ModelsScreen, PairDrafterSheet, RoleRows, RolesNotes } from '../../models/Views/ModelsScreen'
import { StoreSheet } from '../../models/Views/StoreView'
import { MonitorView } from '../../monitor/Views/MonitorView'
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
import { startRequest } from '../../shell/ViewModels/Routing'
import { ConfirmHost, SheetHost } from '../../shell/Views/Hosts'
import { PlaceholderScreen } from '../../shell/Views/PlaceholderScreen'
import { ShellView } from '../../shell/Views/ShellView'
import { useObserved } from '../../styles/Observation/useObserved'
import '../../styles/Stylesheet'
import { modeRules } from '../Previews/ModeRules'
import { presetFor } from '../Previews/Presets'
import { AppViewModel } from '../ViewModels/AppViewModel'

const libraryConfirms: readonly string[] = ['deleteChat', 'compactChat', 'forget'] satisfies readonly LibraryConfirm[]
const modelsConfirms: readonly string[] = ['model', 'storeDelete', 'storeModelDelete', 'unregister', 'registerAll'] satisfies readonly ModelsConfirm[]
const storeSheets: readonly string[] = ['storeConvert', 'storeQuantize', 'storeRegister', 'storeRename']
const settingsConfirms: readonly string[] = ['settingsForget', 'xcodeRemove'] satisfies readonly SettingsConfirm[]

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
 * roles slot, with its Register… sheet and its two confirms.
 * It starts the app once it is on screen.
 */
export function ApogeeApp({ model }: { readonly model: AppViewModel }) {
  useObserved(model)
  useObserved(model.monitor)
  useObserved(model.library)
  useObserved(model.session)
  useObserved(model.models)
  useEffect(() => {
    void model.start()
    return () => model.stop()
  }, [model])
  const shell = model.shell
  const session = model.session
  const library = model.library
  const models = model.models
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
        if (settingsConfirms.includes(kind)) {
          const confirm = kind as SettingsConfirm
          const words = model.settings.confirmWords(confirm)
          if (!words) return null
          return <ConfirmHost title={words.title} body={words.body} cancel={{ label: 'Cancel', run: () => model.dismissSettings(confirm) }} confirm={{ label: words.confirm, run: () => void model.confirmSettings(confirm), destructive: true }} />
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
