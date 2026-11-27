import type { ReactNode } from 'react'
import { boards } from '../Models/Boards'
import { FoundationsHeader } from '../Components/FoundationsHeader'
import { colorPage } from '../ViewModels/ColorPage'
import { typePage } from '../ViewModels/TypePage'
import { ColorTokensPage } from '../Views/ColorTokensPage'
import { ComponentsPage } from '../Views/ComponentsPage'
import { LogoPage } from '../Views/LogoPage'
import { NotPortedCard } from '../Views/NotPortedCard'
import { PropSwitcher } from '../Views/PropSwitcher'
import { TypeSpacePage } from '../Views/TypeSpacePage'
import { previewSpecimens, useGalleryPreview } from './GalleryPreview'

const entry = (id: string) => boards.find((each) => each.id === id) ?? boards[0]!

const themed = (theme: string, page: ReactNode) => (
  <div className="ap" data-theme={theme} style={{ background: 'var(--surface-window)' }}>
    {page}
  </div>
)

/** The color page in Graphite Dark: every ratio computed from the tokens. */
export const ColorsDark = () => themed('dark', <ColorTokensPage face={colorPage()} />)

/** The color page in Graphite Light. */
export const ColorsLight = () => themed('light', <ColorTokensPage face={colorPage()} />)

/** The type, space and shape page. */
export const TypeDark = () => themed('dark', <TypeSpacePage face={typePage()} />)

/** A Components page with its specimens: The turn, by module. */
export const ComponentsWithSpecimens = () => themed('dark', <ComponentsPage entry={entry('ComponentsTurn')} specimens={previewSpecimens} />)

/** A Components page no module draws yet: the "Not ported yet" card, naming the item that ports it. */
export const ComponentsNotPorted = () => themed('dark', <ComponentsPage entry={entry('ComponentsPrompts')} specimens={[]} />)

/** The logo page, the artwork missing (a preview has no logo; `app` hands in the built one). */
export const Logo = () => themed('light', <LogoPage logo="" />)

/** The card a Components section shows while no module draws it, naming the item that first ported it (since 1r, no board waits on one). */
export const NotPorted = () => themed('dark', <NotPortedCard item="1v" title="Components · Asking and failing" />)

/** A Foundations page's header alone. */
export const Header = () => themed('dark', <FoundationsHeader label="FOUNDATIONS · COLOR" title="Color tokens">Every color is one of these.</FoundationsHeader>)

/** A screen's switcher: the theme and the apogee mode, config-typo chosen. */
export const SwitcherScreen = () => themed('dark', <PropSwitcher model={useGalleryPreview('?gallery', typo)} id="ScreenModels" />)

/** A page's switcher: the theme alone. */
export const SwitcherPage = () => themed('dark', <PropSwitcher model={useGalleryPreview()} id="ColorTokens" />)

function typo(gallery: { setApogee(id: string, mode: 'config-typo'): void }) {
  gallery.setApogee('ScreenModels', 'config-typo')
}
