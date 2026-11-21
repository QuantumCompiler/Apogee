import type { MediaPlayerFace } from '../Media/Playback'

/**
 * The player's play or pause button: the 28 px control the old prototype's
 * `player()` face drives under a clip, a composition and a speech's waveform
 * (`main.html` at `b375fac9`, "media: shared" and `vox.sel.pl`). Design 1k's
 * `MediaPlayer` drew it in its controls; promoted here on Voice's waveform
 * (1m), so the one player's button is drawn once. Its label and icon are the
 * face's: Play while paused, Pause while playing.
 */
export function PlayToggle({ face, toggle }: { readonly face: Pick<MediaPlayerFace, 'playing' | 'toggleLabel'>; readonly toggle: () => void }) {
  return (
    <button type="button" className="ib" onClick={toggle} aria-label={face.toggleLabel} style={{ width: 28, height: 28, color: 'var(--text-primary)' }}>
      <svg width="14" height="14" viewBox="0 0 16 16" aria-hidden="true">
        <path d={face.playing ? 'M4.5 3h2.5v10H4.5zM9 3h2.5v10H9z' : 'M5 3.2v9.6L13 8z'} fill="currentColor" />
      </svg>
    </button>
  )
}
