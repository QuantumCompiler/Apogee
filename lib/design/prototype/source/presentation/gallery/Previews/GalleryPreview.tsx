import { useMemo } from 'react'
import type { Specimen } from '../../styles/Specimens/Specimen'
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

/** The parts `app` hands the gallery, standing in: the app as a labelled box, two specimens, and no logo. */
export const previewParts: GalleryParts = {
  mount: (mount, size) => <StandInApp mount={mount} size={size} />,
  specimens: previewSpecimens,
  logo: '',
}
