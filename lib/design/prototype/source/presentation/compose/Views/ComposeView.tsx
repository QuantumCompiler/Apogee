import { useObserved } from '../../styles/Observation/useObserved'
import type { ComposeViewModel } from '../ViewModels/ComposeViewModel'
import { EditPane } from './EditPane'
import { CompositionSettings, NewComposition } from './NewComposition'
import { ProjectList } from './ProjectList'
import { ProjectPage } from './ProjectPage'

/**
 * The compositions (design 1l): the old prototype's Compositions tab
 * (`main.html` at `b375fac9`, `vid.isCompose`) -- the list, the new
 * composition or a project's page, and its settings or Make changes beside
 * it. Video's second tab: the app root hands it to `VideoScreen`'s
 * `compositions` slot, as the Mac's `ApogeeApp` fills
 * `VideoScreen<Compositions>`, so `compose` never imports `video`.
 */
export function ComposeView({ model }: { readonly model: ComposeViewModel }) {
  useObserved(model)
  const face = model.face()
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
      <ProjectList model={model} />
      <div style={{ flex: '1 1 0', minWidth: 0, minHeight: 0, overflowY: 'auto', padding: '20px 28px 32px' }}>
        <div style={{ maxWidth: 860, display: 'flex', flexDirection: 'column', gap: 16 }}>
          {face.isNew ? <NewComposition model={model} /> : null}
          {!face.isNew && face.page.shown ? <ProjectPage model={model} /> : null}
        </div>
      </div>
      <aside className="ap-detail" aria-label={face.paneLabel} style={{ flex: '0 0 340px', minHeight: 0, overflowY: 'auto', padding: '16px 20px 28px', background: 'var(--surface-inspector)', borderLeft: '1px solid var(--line-border)', fontSize: 'var(--t-small)' }}>
        {face.isNew ? <CompositionSettings model={model} /> : null}
        {!face.isNew && face.page.shown ? <EditPane model={model} /> : null}
      </aside>
    </div>
  )
}
