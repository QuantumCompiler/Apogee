import { Card, CardLabel } from '../Components/Card'

/** The session's card on its own (design 1v): accented for what it asks, and its label. */
const frame = { width: 560, padding: 24, display: 'flex', flexDirection: 'column', gap: 16, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

/** A card accented in the warning color, its label and its words: Not sent's shape. */
export const AccentedCard = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <Card accent="var(--status-warning)" padded label="Not sent">
      <CardLabel text="Not sent" />
      <p style={{ margin: 0 }}>apogee had stopped taking input, so this message never reached it.</p>
    </Card>
  </div>
)

/** A card edge to edge, its parts laid out by what it holds: the permission and failed-load cards' shape. */
export const PlainCard = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <Card>
      <div style={{ padding: '12px 14px' }}>A card's own parts, edge to edge.</div>
    </Card>
  </div>
)

/** A card's label alone: small capitals in the label face. */
export const Label = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <CardLabel text="Session ended" />
  </div>
)
