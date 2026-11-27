import { ModelGlyph } from '../Components/ModelGlyph'

const frame = { display: 'flex', gap: 16, padding: 24, background: 'var(--surface-window)', color: 'var(--text-primary)' } as const

/** Each kind's glyph in its color; a kind not known draws nothing, keeping no place of its own (design 1h promoted it here). */
export const Kinds = () => (
  <div className="ap" data-theme="dark" style={frame}>
    {(['local', 'api', 'subscription', 'none'] as const).map((kind) => (
      <ModelGlyph key={kind} kind={kind} label={kind} />
    ))}
  </div>
)

/** The session list's quiet local chip, at its 12 px. */
export const QuietLocal = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <ModelGlyph kind="local" size={12} color="var(--text-tertiary)" label="local" />
  </div>
)
