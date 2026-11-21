import { FoundationsHeader, foundationsPage, sectionLabel } from '../Components/FoundationsHeader'

const card = { padding: 24, border: '1px solid var(--line-border)', borderRadius: 'var(--radius-card)', background: 'var(--surface-card)' } as const

/**
 * The Foundations logo page (design 1q): the old Logo board (the Apple apps'
 * 11a, the board `Logo`), its words as it wrote them -- the artwork full
 * bleed, and its rounded icon form at 128 and 64 points -- drawn from
 * `lib/design/assets/Logo-320.png`, which the build inlines and `app` hands
 * in (`logo`, a `data:` URL), so the built file stays one file.
 */
export function LogoPage({ logo }: { readonly logo: string }) {
  return (
    <div style={{ ...foundationsPage, paddingBottom: 64 }}>
      <FoundationsHeader label="FOUNDATIONS · THE LOGO" title="The logo">
        A white rocket on a black field. It is Apogee's official logo and the app's icon, wherever macOS shows the app: the Dock, Finder, the app switcher and the About panel. The app's own windows don't draw it.
      </FoundationsHeader>
      <section style={{ marginTop: 36, display: 'flex', flexWrap: 'wrap', gap: 20, alignItems: 'stretch' }}>
        <div style={card}>
          <img src={logo} alt="The Apogee logo: a white outlined rocket, angled up and to the right, on a black field of small gray technology glyphs" width={320} height={320} style={{ display: 'block', width: 320, height: 320 }} />
          <div style={{ marginTop: 14, fontSize: 'var(--t-h3)', fontWeight: 600 }}>The artwork</div>
          <div style={{ fontSize: 'var(--t-dense)', color: 'var(--text-secondary)' }}>Full bleed, as drawn</div>
        </div>
        <div style={{ ...card, flex: '1 1 360px' }}>
          <div style={{ display: 'flex', alignItems: 'flex-end', gap: 24 }}>
            <img src={logo} alt="The logo in a rounded icon shape, 128 points" width={128} height={128} style={{ display: 'block', width: 128, height: 128, borderRadius: '22.4%' }} />
            <img src={logo} alt="The logo in a rounded icon shape, 64 points" width={64} height={64} style={{ display: 'block', width: 64, height: 64, borderRadius: '22.4%' }} />
          </div>
          <div style={{ marginTop: 14, fontSize: 'var(--t-h3)', fontWeight: 600 }}>As the app's icon</div>
          <p style={{ margin: '2px 0 0', fontSize: 'var(--t-dense)', lineHeight: 1.6, color: 'var(--text-secondary)' }}>
            macOS gives it the rounded icon shape. At 32 pixels and below, the icon is the rocket alone, its outline thickened so it stays readable.
          </p>
          <h2 style={{ ...sectionLabel, margin: '24px 0 8px' }}>ONE SOURCE</h2>
          <p style={{ margin: 0, fontSize: 'var(--t-dense)', lineHeight: 1.6, color: 'var(--text-secondary)' }}>
            Every copy is made from <span className="mono">lib/design/assets/Logo.png</span> by <span className="mono">lib/src/darwin/scripts/icon.sh</span>, never redrawn by hand.
          </p>
        </div>
      </section>
    </div>
  )
}
