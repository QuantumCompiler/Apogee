import { Segments } from '../../styles/Components/Segments'
import { useObserved } from '../../styles/Observation/useObserved'
import type { ThemeId } from '../../styles/Tokens/Tokens'
import type { ApogeeMode, GalleryViewModel } from '../ViewModels/GalleryViewModel'

const label = { fontFamily: 'var(--font-mono)', fontSize: 'var(--t-label)', letterSpacing: '.08em', color: 'var(--text-tertiary)' } as const

/**
 * A board's prop switcher (design 1q), the canvas's prop menu: its theme
 * on every board, and on the screens and the App board how the stand-in
 * finds `apogee` -- installed, missing or a config it cannot read.
 */
export function PropSwitcher({ model, id }: { readonly model: GalleryViewModel; readonly id: string }) {
  useObserved(model)
  const face = model.switcher(id)
  const theme = face.themes.find((each) => each.chosen)?.id ?? 'dark'
  const mode = face.modes?.find((each) => each.chosen)?.id ?? 'installed'
  return (
    <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 16 }}>
      <span style={{ display: 'inline-flex', alignItems: 'center', gap: 8 }}>
        <span aria-hidden="true" style={label}>
          THEME
        </span>
        <Segments label="Theme" options={face.themes.map((each) => [each.id, each.name] as const)} current={theme} pick={(value) => model.setTheme(id, value as ThemeId)} />
      </span>
      {face.modes ? (
        <span style={{ display: 'inline-flex', alignItems: 'center', gap: 8 }}>
          <span aria-hidden="true" style={label}>
            APOGEE
          </span>
          <Segments label="How apogee is found" options={face.modes.map((each) => [each.id, each.id] as const)} current={mode} pick={(value) => model.setApogee(id, value as ApogeeMode)} />
        </span>
      ) : null}
    </div>
  )
}
