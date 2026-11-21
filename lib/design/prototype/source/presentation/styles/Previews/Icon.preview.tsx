import { Icon } from '../Components/Icon'
import { iconNames } from '../Icons/IconNames'

const frame = { display: 'flex', flexWrap: 'wrap', gap: 16, padding: 24, background: 'var(--surface-window)', color: 'var(--text-primary)' } as const

/** Every icon in the set at 16 px, in the type page's order. */
export const EverySet = () => (
  <div className="ap" data-theme="dark" style={frame}>
    {iconNames.map((name) => (
      <Icon key={name} name={name} />
    ))}
  </div>
)

/** One read as a picture, its label said: a phase done. */
export const Labelled = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <Icon name="check" size={14} label="done" style={{ color: 'var(--status-accent)' }} />
  </div>
)
