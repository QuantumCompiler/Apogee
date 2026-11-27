import { useState } from 'react'
import { MediaPlayer } from '../Components/MediaPlayer'
import { Playback } from '../Media/Playback'

/** A player standing where `stand` puts it, on a clock that does not move: a preview's still frame. */
function Player({ stand, duration = 5, badge = '' }: { readonly stand: (playback: Playback) => void; readonly duration?: number; readonly badge?: string }) {
  const [playback] = useState(() => {
    const made = new Playback(() => 0)
    stand(made)
    return made
  })
  const face = playback.face('clip:preview', duration, { width: 1280, height: 704, dims: '1280×704 · 24 fps', badge, right: '121 frames', pausedLabel: '5 s · 121 frames', playingLabel: (at) => `frame ${Math.min(121, Math.floor(at * 24) + 1)} of 121` })
  return (
    <div className="ap" data-theme="dark" style={{ width: 440 }}>
      <MediaPlayer face={face} toggle={() => playback.toggle('clip:preview', duration)} />
    </div>
  )
}

/** Paused 2.1 s in, as the `video` preset stands. */
export const Paused = () => <Player stand={(playback) => playback.hold('clip:preview', 2.1)} />

/** Playing from 2.1 s: Pause, the frame it is on. */
export const Playing = () => <Player stand={(playback) => playback.seek('clip:preview', 2.1)} />

/** A draft at its start, its badge in the corner. */
export const DraftAtStart = () => <Player stand={() => undefined} badge="draft · 16 steps" />

/** Played to its end: stopped there, Play starting it over. */
export const Ended = () => <Player stand={(playback) => playback.hold('clip:preview', 5)} />
