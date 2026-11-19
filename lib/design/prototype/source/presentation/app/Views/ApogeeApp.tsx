import { StrictMode, useEffect } from 'react'
import { createRoot } from 'react-dom/client'
import { ConsoleView } from '../../console/Views/ConsoleView'
import { MonitorView } from '../../monitor/Views/MonitorView'
import { sessionSamples } from '../../session/Previews/SessionPresets'
import { SuiteSheet } from '../../session/Views/SuiteSheet'
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
import { layout } from '../../styles/Tokens/Tokens'
import { modeRules } from '../Previews/ModeRules'
import { presetFor } from '../Previews/Presets'
import { AppViewModel } from '../ViewModels/AppViewModel'

const libraryConfirms: readonly string[] = ['deleteChat', 'compactChat', 'forget'] satisfies readonly LibraryConfirm[]

/**
 * The application's window (design 1e): the shell, handed each place's screen,
 * each overlay and Settings by the composition, as `ApogeeApp.swift` hands
 * `ShellView` its screens. Sessions is the Sessions place (1g): the session
 * window with the inspector beside it, and the session list and the
 * new-session buttons in the sidebar on every screen; a place not yet ported
 * is its placeholder; the monitor and the console are their own views. The
 * sheet and confirm hosts get their content by kind: the suite chooser and
 * the `endPrivate` confirm are the session's; the details, rename and
 * execute sheets and the delete, compact and forget confirms the library's.
 * It starts the app once it is on screen.
 */
export function ApogeeApp({ model }: { readonly model: AppViewModel }) {
  useObserved(model)
  useObserved(model.monitor)
  useObserved(model.library)
  useObserved(model.session)
  useEffect(() => {
    void model.start()
    return () => model.stop()
  }, [model])
  const shell = model.shell
  const session = model.session
  const library = model.library
  return (
    <ShellView
      model={shell}
      strip={model.strip}
      sessions={() => <SessionList model={library} />}
      newSessions={() => <NewSessionButtons model={library} />}
      screen={(destination) => {
        // Each feature item puts its place's screen here, and takes its placeholder out of Destination.ts.
        if (destination === 'sessions') return <SessionsScreen model={library} />
        const placeholder = placeholderOf(destination)
        return placeholder ? <PlaceholderScreen placeholder={placeholder} /> : null
      }}
      overlay={(overlay) => (overlay === 'monitor' ? <MonitorView model={model.monitor} close={() => shell.closeOverlay()} /> : <ConsoleView model={model.console} close={() => shell.closeOverlay()} />)}
      settings={() => (
        <SheetHost title="Settings" width={layout.dialog.confirm} right={[{ label: 'Close', kind: 'btn', run: () => shell.closeSettings() }]}>
          <p style={{ margin: 0, color: 'var(--text-secondary)' }}>Settings isn’t drawn here yet. Design item 1i ports it.</p>
        </SheetHost>
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
