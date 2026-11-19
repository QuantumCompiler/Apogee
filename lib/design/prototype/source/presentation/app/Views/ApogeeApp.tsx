import { StrictMode } from 'react'
import { createRoot } from 'react-dom/client'
import { AppViewModel } from '../ViewModels/AppViewModel'

/** The application's window. Empty until 1e mounts the shell and its places in it. */
export function ApogeeApp({ model }: { model: AppViewModel }) {
  return (
    <main data-apogee-app="" aria-label={model.name}>
      <h1>{model.name}</h1>
    </main>
  )
}

/** Mounts the app in `element`: what the entry point, source/main.tsx, calls and nothing else. */
export function start(element: HTMLElement | null): void {
  if (!element) throw new Error('main.html has no #root element to mount the app in')
  createRoot(element).render(
    <StrictMode>
      <ApogeeApp model={new AppViewModel()} />
    </StrictMode>,
  )
}
