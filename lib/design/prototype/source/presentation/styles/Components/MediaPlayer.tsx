import type { MediaPlayerFace } from '../Media/Playback'
import { PlayToggle } from './PlayToggle'

/**
 * A clip or a composition, played (design 1k: the old prototype's player,
 * promoted to `styles` for Video, Compose and Voice, as the Mac's
 * `MediaPlayer` sits in its `styles`): the frame with its size and badge,
 * the play button while paused, the line under it, a caption when one is
 * said, then the controls -- play or pause, the time, the bar with its
 * marks, and what is after it. It decodes no video: its face is `Playback`'s,
 * computed from the clock.
 */
export function MediaPlayer({ face, toggle }: { readonly face: MediaPlayerFace; readonly toggle: () => void }) {
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 8 }}>
      <div style={{ position: 'relative', width: '100%', aspectRatio: face.aspect, boxSizing: 'border-box', border: '1px solid var(--line-border)', borderRadius: 8, overflow: 'hidden', background: 'var(--surface-code)' }}>
        <span className="mono" style={{ position: 'absolute', left: 12, top: 10, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
          {face.dims}
        </span>
        <span className="mono" style={{ position: 'absolute', right: 12, top: 10, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
          {face.badge}
        </span>
        {face.playing ? null : (
          <button type="button" onClick={toggle} aria-label="Play" style={{ position: 'absolute', left: '50%', top: '50%', transform: 'translate(-50%, -50%)', width: 56, height: 56, display: 'grid', placeItems: 'center', padding: 0, border: '1px solid var(--line-strong)', borderRadius: '50%', background: 'var(--surface-card)', color: 'var(--text-primary)' }}>
            <svg width="18" height="18" viewBox="0 0 16 16" aria-hidden="true">
              <path d="M5.5 3.2v9.6L13 8z" fill="currentColor" />
            </svg>
          </button>
        )}
        <span className="mono" style={{ position: 'absolute', left: 16, right: 16, top: 'calc(50% + 40px)', textAlign: 'center', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
          {face.label}
        </span>
        {face.caption === '' ? null : (
          <span style={{ position: 'absolute', left: '50%', bottom: 16, transform: 'translateX(-50%)', width: 'max-content', maxWidth: '84%', padding: '4px 10px', borderRadius: 4, background: 'color-mix(in srgb, var(--media-caption-fill) 75%, transparent)', color: 'var(--media-caption)', fontSize: 'var(--t-body)', lineHeight: 1.4, textAlign: 'center' }}>
            {face.caption}
          </span>
        )}
      </div>
      <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
        <PlayToggle face={face} toggle={toggle} />
        <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', whiteSpace: 'nowrap' }}>
          {face.time}
        </span>
        <span aria-hidden="true" style={{ position: 'relative', flex: '1 1 auto', height: 4, borderRadius: 2, background: 'var(--line-border)' }}>
          {face.marks.map((left, index) => (
            <span key={index} style={{ position: 'absolute', left: `${left}%`, top: -3, width: 1, height: 10, background: 'var(--text-tertiary)' }} />
          ))}
          <span style={{ position: 'absolute', left: 0, top: 0, bottom: 0, width: `${face.pct}%`, borderRadius: 2, background: 'var(--status-accent)' }} />
        </span>
        <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', whiteSpace: 'nowrap' }}>
          {face.right}
        </span>
      </div>
    </div>
  )
}
