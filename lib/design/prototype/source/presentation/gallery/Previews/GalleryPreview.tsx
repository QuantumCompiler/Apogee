import { useMemo } from 'react'
import type { Specimen } from '../../styles/Specimens/Specimen'
import type { KeyLine } from '../ViewModels/BuildLayoutViewModel'
import { GalleryViewModel } from '../ViewModels/GalleryViewModel'
import type { AddressBar, BoardMount } from '../ViewModels/GalleryViewModel'
import type { GalleryParts, MountSize } from '../Views/GalleryView'

/** An address bar that only remembers what it was told: a preview writes no real address. */
export function previewAddress(search: string): AddressBar {
  let now = search
  return {
    get search() {
      return now
    },
    replace(next: string) {
      now = next
    },
  }
}

/** A gallery at `search`, with `then` done to it, once per preview. */
export function useGalleryPreview(search = '?gallery', then?: (gallery: GalleryViewModel) => void): GalleryViewModel {
  return useMemo(() => {
    const gallery = new GalleryViewModel(previewAddress(search))
    then?.(gallery)
    return gallery
  }, [search, then])
}

/** Stands in for the app a frame mounts, saying what it would mount: `app` hands the gallery the real one. */
function StandInApp({ mount, size }: { readonly mount: BoardMount; readonly size: MountSize }) {
  return (
    <div className="ap" data-theme={mount.theme} style={{ width: size.width, height: size.height, display: 'grid', placeItems: 'center', background: 'var(--surface-window)', color: 'var(--text-secondary)', fontFamily: 'var(--font-mono)', fontSize: 'var(--t-h2)' }}>
      {mount.start ?? 'the default start'} · {mount.still ? 'still' : 'live'} · {mount.apogee}
    </div>
  )
}

/** Two specimens, one each in two sections, standing in for the modules' own. */
export const previewSpecimens: readonly Specimen[] = [
  { name: 'A plain line', section: 'turn', module: 'session', item: '1u', render: () => <p style={{ margin: 0 }}>A line of the turn.</p> },
  { name: 'A label', section: 'media', module: 'styles', item: '1k', render: () => <span className="lbl">MAKING MEDIA</span> },
]

/**
 * The key map, standing in: the design's keyboard list as the shell's
 * `KeyMap.reference` writes it, which `app` hands the gallery (1r) -- copied
 * here because the gallery never imports the shell, and held to the real
 * map by a test.
 */
export const previewKeys: readonly KeyLine[] = [
  { label: 'New session', keys: '⌘N' },
  { label: 'New private session', keys: '⇧⌘N' },
  { label: 'New execute session', keys: '⌥⌘N' },
  { label: 'Switch model', keys: '⇧⌘M' },
  { label: 'Show or hide the inspector', keys: '⌥⌘I' },
  { label: 'System monitor', keys: '⌥⌘M' },
  { label: 'Console', keys: '⇧⌘K' },
  { label: 'Settings', keys: '⌘,' },
  { label: 'Stop the answer', keys: '⌘.' },
  { label: 'Search sessions', keys: '⇧⌘F' },
  { label: 'Sessions, Models, Suites, Tasks, Code graph, Video, Voice, Knowledge, Training, Library', keys: '⌘1 – ⌘9, ⌘0' },
  { label: 'Review', keys: '⇧⌘R' },
  { label: 'Models library', keys: '⇧⌘L' },
  { label: 'Send', keys: '⏎' },
  { label: 'New line', keys: '⇧⏎' },
  { label: 'Commands', keys: '/' },
  { label: 'Mention a file', keys: '@' },
  { label: 'Allow once on a permission card', keys: '⏎' },
  { label: 'Deny on a permission card', keys: 'esc' },
  { label: 'Pick an answer on a card', keys: '1 – 4' },
  { label: 'Play or pause the focused player', keys: 'space' },
  { label: 'Generate, Speak, Plan or Render, from its form', keys: '⌘⏎' },
  { label: 'Hide the sidebar', keys: '⌃⌘S' },
]

/** The parts `app` hands the gallery, standing in: the app as a labelled box, two specimens, no logo, and the key map. */
export const previewParts: GalleryParts = {
  mount: (mount, size) => <StandInApp mount={mount} size={size} />,
  specimens: previewSpecimens,
  logo: '',
  keys: previewKeys,
}
