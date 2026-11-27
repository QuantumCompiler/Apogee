import { useState } from 'react'
import { PlayToggle } from '../Components/PlayToggle'
import { Playback } from '../Media/Playback'

/** The player's button standing where `stand` puts it, on a clock that does not move: a preview's still frame. */
function Toggle({ stand }: { readonly stand: (playback: Playback) => void }) {
  const [playback] = useState(() => {
    const made = new Playback(() => 0)
    stand(made)
    return made
  })
  const face = playback.face('speech:preview', 0.1, { width: 1, height: 1 })
  return (
    <div className="ap" data-theme="dark" style={{ padding: 12 }}>
      <PlayToggle face={face} toggle={() => playback.toggle('speech:preview', 0.1)} />
    </div>
  )
}

/** Paused on a speech's second sentence, as the `voice` preset stands: Play. */
export const Paused = () => <Toggle stand={(playback) => playback.hold('speech:preview', 0.06)} />

/** Playing from the second sentence: Pause. */
export const Playing = () => <Toggle stand={(playback) => playback.seek('speech:preview', 0.05)} />
