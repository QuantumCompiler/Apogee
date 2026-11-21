import type { ReactNode } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import type { VideoViewModel } from '../ViewModels/VideoViewModel'
import { ClipPage, RunCard } from '../Views/ClipPage'
import { ClipsList } from '../Views/ClipsList'
import { GenerateForm } from '../Views/GenerateForm'
import { CompositionsPlaceholder } from '../Views/VideoScreen'
import { clipPath, clips } from './VideoWorlds'
import { useVideoPreview } from './VideoPreview'

/** Each part of the Video place on its own (design 1k), over the video world, as the screen draws it. */
const frame = { display: 'flex', width: 'max-content', maxWidth: 1180, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Part({ model, children }: { readonly model: VideoViewModel; readonly children: ReactNode }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={frame}>
      {children}
    </div>
  )
}

/** The clips list alone: the five records, the newest first. */
export const List = () => {
  const model = useVideoPreview()
  return (
    <Part model={model}>
      <ClipsList model={model} />
    </Part>
  )
}

/** A clip's page alone: the final, made from its draft. */
export const Page = () => {
  const model = useVideoPreview({ then: (video) => video.select(clipPath(clips.final)) })
  return (
    <Part model={model}>
      <div style={{ width: 820, display: 'flex', flexDirection: 'column', gap: 16, padding: 20 }}>
        <ClipPage model={model} />
      </div>
    </Part>
  )
}

/** The run alone: the draft made final, step 38 of 50. */
export const Run = () => {
  const model = useVideoPreview({ ticks: 43, then: (video) => (video.act('final', clipPath(clips.draft)), video.generate()) })
  return (
    <Part model={model}>
      <div style={{ width: 820, display: 'flex', flexDirection: 'column', gap: 16, padding: 20 }}>
        <RunCard model={model} />
      </div>
    </Part>
  )
}

/** The generate form alone: the paper boat's draft. */
export const Form = () => {
  const model = useVideoPreview()
  return (
    <Part model={model}>
      <GenerateForm model={model} />
    </Part>
  )
}

/** The compositions slot's placeholder, until 1l fills it. */
export const CompositionsSlot = () => (
  <div className="ap" data-theme="dark" style={{ ...frame, width: 820, height: 400 }}>
    <CompositionsPlaceholder />
  </div>
)
