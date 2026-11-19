import { useState } from 'react'
import { Clock } from '../../../infrastructure/clock/Clock'
import { defaultAppearance } from '../../styles/Appearance/Appearance'
import { ThemeService } from '../../styles/Appearance/ThemeService'
import { placeholderOf } from '../ViewModels/Destination'
import type { Destination } from '../ViewModels/Destination'
import { ShellViewModel } from '../ViewModels/ShellViewModel'
import { PlaceholderPanel, PlaceholderScreen } from '../Views/PlaceholderScreen'
import { ShellView } from '../Views/ShellView'

/**
 * The shell's window, seen on its own (design 1e): a still clock, each place
 * its placeholder, as the app draws them until each place is ported. The
 * strip's numbers are the monitor's, which the shell never imports; the app's
 * preview shows them.
 */
function useShell(prepare: (shell: ShellViewModel) => void = () => {}, theme: ThemeService = new ThemeService()) {
  const [shell] = useState(() => {
    const model = new ShellViewModel(new Clock({ still: true }), theme)
    prepare(model)
    return model
  })
  return shell
}

const screen = (destination: Destination) => {
  const placeholder = placeholderOf(destination)
  return placeholder ? <PlaceholderScreen placeholder={placeholder} /> : null
}

const overlay = (close: () => void) => () => <PlaceholderPanel title="Console" says="An overlay's place over the window: its view is its module's, handed in by app." onClose={close} />

/** The window at Sessions, Graphite Dark, nothing over it. */
export const Window = () => {
  const shell = useShell()
  return <ShellView model={shell} screen={screen} overlay={overlay(() => shell.closeOverlay())} />
}

/** At Models, its key pressed, the strip open before anything was read. */
export const AtModels = () => {
  const shell = useShell((model) => {
    model.go('models')
    model.setSystemOpen(true)
  })
  return <ShellView model={shell} screen={screen} overlay={overlay(() => shell.closeOverlay())} />
}

/** Graphite Light. */
export const Light = () => {
  const shell = useShell(() => {}, new ThemeService({ ...defaultAppearance, themeChoice: 'light' }))
  return <ShellView model={shell} screen={screen} overlay={overlay(() => shell.closeOverlay())} />
}

/** High Contrast, the largest text. */
export const ContrastLargest = () => {
  const shell = useShell(() => {}, new ThemeService({ ...defaultAppearance, themeChoice: 'contrast', textSize: 'xl' }))
  return <ShellView model={shell} screen={screen} overlay={overlay(() => shell.closeOverlay())} />
}

/** The theme button pressed: its toast. */
export const ThemeToast = () => {
  const shell = useShell((model) => model.cycleTheme())
  return <ShellView model={shell} screen={screen} overlay={overlay(() => shell.closeOverlay())} />
}

/** A menu open: the scrim covers the window. */
export const MenuOpen = () => {
  const shell = useShell((model) => model.openMenu('row:preview'))
  return <ShellView model={shell} screen={screen} overlay={overlay(() => shell.closeOverlay())} />
}

/** An overlay's place over the window: the shell's frame (the console's own view is handed in by `app`, 1g). */
export const ConsoleOpen = () => {
  const shell = useShell((model) => model.open('console'))
  return <ShellView model={shell} screen={screen} overlay={overlay(() => shell.closeOverlay())} />
}

/** The sidebar hidden (⌃⌘S). */
export const SidebarHidden = () => {
  const shell = useShell((model) => model.perform({ kind: 'toggleSidebar' }))
  return <ShellView model={shell} screen={screen} overlay={overlay(() => shell.closeOverlay())} />
}
