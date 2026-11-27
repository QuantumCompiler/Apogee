import { boards } from '../Models/Boards'
import { BoardCard, FocusedBoard, GalleryView } from '../Views/GalleryView'
import { previewParts, useGalleryPreview } from './GalleryPreview'

const entry = (id: string) => boards.find((each) => each.id === id) ?? boards[0]!

/** `?gallery`: the Build page, its headings over its rows, no frame scrolled near yet. */
export const BuildPage = () => <GalleryView model={useGalleryPreview()} parts={previewParts} />

/** `?gallery=foundations`: the color, type, components and logo boards. */
export const FoundationsPage = () => <GalleryView model={useGalleryPreview('?gallery=foundations')} parts={previewParts} />

/** `&board=ScreenModels`: one screen at full size, still, its switcher offering the theme and the apogee mode. */
export const FocusedScreen = () => <GalleryView model={useGalleryPreview('?gallery&board=ScreenModels')} parts={previewParts} />

/** A screen board in its row, mounted: a quarter of its size. */
export const CardMounted = () => <BoardCard entry={entry('ScreenModels')} model={useGalleryPreview('?gallery', mountModels)} parts={previewParts} />

/** A board-less preset's card, not mounted yet: its title and its mark. */
export const CardNoOldBoard = () => <BoardCard entry={entry('settings:keyboard')} model={useGalleryPreview()} parts={previewParts} />

/** The App board at full size: live, at the app's own start, in High Contrast. */
export const FocusedApp = () => <FocusedBoard entry={entry('App')} model={useGalleryPreview('?gallery&board=App', contrastApp)} parts={previewParts} />

/** The Contract board at full size: its page (1r), with the theme alone on its switcher. */
export const FocusedContract = () => <FocusedBoard entry={entry('BuildContract')} model={useGalleryPreview('?gallery&board=BuildContract')} parts={previewParts} />

/** The Processes board at full size in Graphite Light: the page follows its board's theme. */
export const FocusedProcessesLight = () => <FocusedBoard entry={entry('BuildProcesses')} model={useGalleryPreview('?gallery&board=BuildProcesses', lightProcesses)} parts={previewParts} />

function mountModels(gallery: { near(id: string, near: boolean): void }) {
  gallery.near('ScreenModels', true)
}

function lightProcesses(gallery: { setTheme(id: string, theme: 'light'): void }) {
  gallery.setTheme('BuildProcesses', 'light')
}

function contrastApp(gallery: { setTheme(id: string, theme: 'contrast'): void }) {
  gallery.setTheme('App', 'contrast')
}
