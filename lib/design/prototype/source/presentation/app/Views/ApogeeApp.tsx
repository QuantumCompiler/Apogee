import { StrictMode, useEffect } from 'react'
import { createRoot } from 'react-dom/client'
import { MonitorView } from '../../monitor/Views/MonitorView'
import { placeholderOf } from '../../shell/ViewModels/Destination'
import { startRequest } from '../../shell/ViewModels/Routing'
import { SheetHost } from '../../shell/Views/Hosts'
import { PlaceholderPanel, PlaceholderScreen } from '../../shell/Views/PlaceholderScreen'
import { ShellView } from '../../shell/Views/ShellView'
import { useObserved } from '../../styles/Observation/useObserved'
import '../../styles/Stylesheet'
import { layout } from '../../styles/Tokens/Tokens'
import { modeRules } from '../Previews/ModeRules'
import { presetFor } from '../Previews/Presets'
import { AppViewModel } from '../ViewModels/AppViewModel'

/**
 * The application's window (design 1e): the shell, handed each place's screen,
 * each overlay and Settings by the composition, as `ApogeeApp.swift` hands
 * `ShellView` its screens. A place not yet ported is its placeholder; the
 * monitor is its own view. It starts the app once it is on screen.
 */
export function ApogeeApp({ model }: { readonly model: AppViewModel }) {
  useObserved(model.monitor)
  useEffect(() => {
    model.start()
    return () => model.stop()
  }, [model])
  const shell = model.shell
  return (
    <ShellView
      model={shell}
      strip={model.strip}
      screen={(destination) => {
        // Each feature item puts its place's screen here, and takes its placeholder out of Destination.ts.
        const placeholder = placeholderOf(destination)
        return placeholder ? <PlaceholderScreen placeholder={placeholder} /> : null
      }}
      overlay={(overlay) =>
        overlay === 'monitor' ? (
          <MonitorView model={model.monitor} close={() => shell.closeOverlay()} />
        ) : (
          <PlaceholderPanel title="Console" says="The console isn’t drawn here yet. Design item 1g ports it." onClose={() => shell.closeOverlay()} />
        )
      }
      settings={() => (
        <SheetHost title="Settings" width={layout.dialog.confirm} right={[{ label: 'Close', kind: 'btn', run: () => shell.closeSettings() }]}>
          <p style={{ margin: 0, color: 'var(--text-secondary)' }}>Settings isn’t drawn here yet. Design item 1i ports it.</p>
        </SheetHost>
      )}
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
      <ApogeeApp model={new AppViewModel(preset, modeRules)} />
    </StrictMode>,
  )
}
