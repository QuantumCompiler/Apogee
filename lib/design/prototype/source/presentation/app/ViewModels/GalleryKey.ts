import { galleryAddress, withGallery, withoutGallery } from '../../gallery/ViewModels/GalleryViewModel'
import type { AddressBar } from '../../gallery/ViewModels/GalleryViewModel'
import { Observable } from '../../styles/Observation/Observable'
import { chord, chordOf, symbols } from '../../shell/ViewModels/KeyMap'
import type { BoundKey, KeyChord, KeyPress } from '../../shell/ViewModels/KeyMap'

/**
 * The gallery's key (design 1q): ⌥⌘G, "G" for gallery, which app handles
 * outside the shell's key map -- the gallery is the design's review tool,
 * not a product screen, so it is in no key map and no line of Settings ›
 * Keyboard. Chosen from the chords the browsers leave free: Chrome, Safari
 * and Firefox keep ⌘G and ⇧⌘G for find, and bind nothing to ⌥⌘G; the app
 * binds nothing to it either (a test holds that). Read by its physical key,
 * as the shell's keys are, so Option's © never matters.
 */
export const galleryKey: KeyChord = chord('g', 'option', 'command')

/** Whether a key pressed is the gallery's. */
export function opensGallery(press: KeyPress): boolean {
  return symbols(chordOf(press)) === symbols(galleryKey)
}

/** Each key the app binds that is the gallery's, as a sentence; none when the gallery's key is free. */
export function galleryKeyClashes(bound: readonly BoundKey[]): string[] {
  const key = symbols(galleryKey)
  return bound.filter((each) => symbols(each.chord) === key).map((each) => `the app binds ${key} to ${each.label}, and ${key} is the gallery's: the gallery opens outside the app's key map`)
}

/** What the window shows at a page's query: the gallery on `?gallery` (and its page and board), the app otherwise. */
export function windowShows(search: string): 'gallery' | 'app' {
  return galleryAddress(search) ? 'gallery' : 'app'
}

/**
 * What the window shows (design 1q): the app, or the gallery, which is drawn
 * outside the app's frame. The gallery's key turns it from one to the other,
 * and the address with it, so a reload opens what was shown.
 */
export class WindowChoice extends Observable {
  private showing: 'gallery' | 'app'

  constructor(private readonly address: AddressBar) {
    super()
    this.showing = windowShows(address.search)
  }

  get shows(): 'gallery' | 'app' {
    return this.showing
  }

  /** The gallery's key: the gallery from the app, the app from the gallery. */
  toggle(): void {
    this.showing = this.showing === 'app' ? 'gallery' : 'app'
    this.address.replace(this.showing === 'gallery' ? withGallery(this.address.search) : withoutGallery(this.address.search))
    this.changed()
  }
}
