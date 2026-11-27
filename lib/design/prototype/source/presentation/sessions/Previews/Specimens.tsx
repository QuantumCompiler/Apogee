import type { Specimen } from '../../styles/Specimens/Specimen'
import { InspectorPane } from '../Views/SessionsScreen'
import { useLibraryPreview } from './LibraryPreview'

/**
 * The Sessions place's specimens for the gallery's Components pages (design
 * 1q): the inspector beside the open session -- its context section, what
 * the turn holds -- over the stand-in's saved chats and the session it
 * replays, as its previews draw it, with no frame or theme of its own.
 */
export const specimens: readonly Specimen[] = [
  {
    name: 'Inspector · context and what it holds',
    section: 'model',
    module: 'sessions',
    item: '1g',
    render: () => (
      <div style={{ display: 'flex', width: 304, height: 760 }}>
        <InspectorPane model={useLibraryPreview()} />
      </div>
    ),
  },
]
