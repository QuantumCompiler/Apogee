import type { ReactNode } from 'react'
import type { ThemeId } from '../../styles/Tokens/Tokens'
import { CodeText } from '../Components/CodeText'
import { BuildContractViewModel } from '../ViewModels/BuildContractViewModel'
import { BuildLayoutViewModel } from '../ViewModels/BuildLayoutViewModel'
import { BuildProcessesViewModel } from '../ViewModels/BuildProcessesViewModel'
import { BuildContractPage } from '../Views/BuildContractPage'
import { BuildLayoutPage } from '../Views/BuildLayoutPage'
import { BuildProcessesPage } from '../Views/BuildProcessesPage'
import { previewKeys } from './GalleryPreview'

/** A page in a theme, as its board draws it. */
const themed = (theme: ThemeId, page: ReactNode) => (
  <div className="ap" data-theme={theme} style={{ background: 'var(--surface-window)' }}>
    {page}
  </div>
)

const contract = () => <BuildContractPage model={new BuildContractViewModel()} />
const layout = () => <BuildLayoutPage model={new BuildLayoutViewModel(previewKeys)} />
const processes = () => <BuildProcessesPage model={new BuildProcessesViewModel()} />

/** The contract page in Graphite Dark, the old board's only theme: its stat cards counted from its rows. */
export const ContractDark = () => themed('dark', contract())

/** The contract page in Graphite Light. */
export const ContractLight = () => themed('light', contract())

/** The contract page in High Contrast. */
export const ContractContrast = () => themed('contrast', contract())

/** The layout page in Graphite Dark: its sizes from the tokens, its keys from the key map. */
export const LayoutDark = () => themed('dark', layout())

/** The layout page in Graphite Light. */
export const LayoutLight = () => themed('light', layout())

/** The layout page in High Contrast. */
export const LayoutContrast = () => themed('contrast', layout())

/** The processes page in Graphite Dark: every color a token. */
export const ProcessesDark = () => themed('dark', processes())

/** The processes page in Graphite Light, which the old board, written in hex, could not draw. */
export const ProcessesLight = () => themed('light', processes())

/** The processes page in High Contrast. */
export const ProcessesContrast = () => themed('contrast', processes())

/** The one code span: a row's words with their inline code. */
export const InlineCode = () => themed('dark', <CodeText text={'send `{"type":"cancel"}`; the turn still ends in one `result`'} />)
