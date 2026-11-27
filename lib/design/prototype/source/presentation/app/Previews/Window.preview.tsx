import { useState } from 'react'
import type { PageAddress } from '../Views/ApogeeApp'
import { ApogeeWindow, AppWindow, FramedApp, GalleryWindow } from '../Views/ApogeeApp'
import { WindowChoice } from '../ViewModels/GalleryKey'

/** A page's address that only remembers what it was told: a preview writes no real address. */
function useAddress(search: string): PageAddress {
  const [address] = useState(() => {
    let now = search
    return {
      get search() {
        return now
      },
      hash: '',
      replace(next: string) {
        now = next
      },
    }
  })
  return address
}

/** The window at a page with no query: the app at its default start. */
export const OpensTheApp = () => {
  const address = useAddress('')
  const [choice] = useState(() => new WindowChoice(address))
  return <ApogeeWindow choice={choice} address={address} />
}

/** The window at `?gallery` (design 1q): the gallery in the app's place, on the Build page. */
export const OpensTheGallery = () => {
  const address = useAddress('?gallery')
  const [choice] = useState(() => new WindowChoice(address))
  return <ApogeeWindow choice={choice} address={address} />
}

/** The gallery alone, on Foundations, its pages drawn from the tokens and the modules' specimens. */
export const Gallery = () => <GalleryWindow address={useAddress('?gallery=foundations')} />

/** The app alone at `?start=models&still=true`. */
export const App = () => <AppWindow address={useAddress('?start=models&still=true')} />

/** A gallery frame: the app at `models`, still, in Graphite Light, drawn at the frame's 1440 × 900. */
export const Frame = () => <FramedApp mount={{ start: 'models', still: true, theme: 'light', apogee: 'installed' }} size={{ width: 1440, height: 900, live: false }} />
