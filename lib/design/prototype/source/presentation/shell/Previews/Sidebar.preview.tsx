import { useState } from 'react'
import { Clock } from '../../../infrastructure/clock/Clock'
import { ThemeService } from '../../styles/Appearance/ThemeService'
import { ShellViewModel } from '../ViewModels/ShellViewModel'
import { Sidebar } from '../Views/Sidebar'

/** The sidebar on its own (design 1e), in a 256 × 640 frame. */
function useShell(prepare: (shell: ShellViewModel) => void = () => {}) {
  const [shell] = useState(() => {
    const model = new ShellViewModel(new Clock({ still: true }), new ThemeService())
    prepare(model)
    return model
  })
  return shell
}

const frame = { display: 'flex', width: 256, height: 640 } as const

/** At Sessions, the strip closed, nothing read yet. */
export const Plain = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <Sidebar model={useShell()} />
  </div>
)

/** The strip open with no reading: it says so. */
export const StripNotRead = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <Sidebar model={useShell((model) => model.setSystemOpen(true))} />
  </div>
)

/** The strip, when the monitor cannot read: its failure, in the Mac app's words. */
export const StripNotInstalled = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <Sidebar model={useShell((model) => model.setSystemOpen(true))} strip={{ summary: 'Apogee isn’t installed, so the system can’t be read.', warning: false, rows: [] }} />
  </div>
)

/** A place with a run going: its badge. */
export const Running = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <Sidebar model={useShell((model) => model.go('models'))} running={{ models: 1 }} />
  </div>
)
