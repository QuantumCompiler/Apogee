import { kvChat, retryChat, streamingRules } from '../../session/Previews/SessionPresets'
import type { SessionLibraryViewModel } from '../ViewModels/SessionLibraryViewModel'
import { NewSessionButtons, RenameSheet, SessionList } from '../Views/SessionList'
import { useLibraryPreview } from './LibraryPreview'
import { withLibraryRules } from './LibraryRules'

/**
 * The sidebar's session list on its own (design 1g), one preview per state a
 * person sees differently, each over the stand-in answering the library's
 * hand-made `chats list` and the Streaming session.
 */
function Sidebar({ model }: { readonly model: SessionLibraryViewModel }) {
  return (
    <div className="ap" data-theme="dark" style={{ position: 'relative', width: 256, height: 640, display: 'flex', flexDirection: 'column', background: 'var(--surface-sidebar)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 }}>
      <div style={{ flex: '0 0 var(--layout-header)', display: 'flex', alignItems: 'center', gap: 8, padding: '0 10px 0 18px' }}>
        <span style={{ flex: '1 1 auto' }} />
        <NewSessionButtons model={model} />
      </div>
      <nav className="ap-list" aria-label="Sessions" style={{ flex: '1 1 auto', minHeight: 0, overflowY: 'auto', padding: '0 8px 10px' }}>
        <SessionList model={model} />
      </nav>
    </div>
  )
}

/** The thirteen by date: Today and Yesterday open, the open session's row current. */
export const Listed = () => <Sidebar model={useLibraryPreview()} />

/** Every date group open. */
export const AllOpen = () => <Sidebar model={useLibraryPreview({ then: (library) => library.toggleDates() })} />

/** Every date group closed: Today says it holds the open session. */
export const AllClosed = () => (
  <Sidebar
    model={useLibraryPreview({
      then: (library) => {
        library.toggleDates()
        library.toggleDates()
      },
    })}
  />
)

/** The SESSIONS heading folded: no search, no groups. */
export const Folded = () => <Sidebar model={useLibraryPreview({ then: (library) => library.toggleList() })} />

/** A search with matches: every group open while it searches, the count of matches. */
export const Searching = () => <Sidebar model={useLibraryPreview({ then: (library) => library.setQuery('timeouts') })} />

/** A search with no match: "No sessions match …". */
export const NoMatch = () => <Sidebar model={useLibraryPreview({ then: (library) => library.setQuery('kubernetes') })} />

/** A row's menu open: Details…, Rename…, Compact…, Delete…. */
export const RowMenu = () => <Sidebar model={useLibraryPreview({ then: (library) => library.toggleRowMenu(kvChat) })} />

/** Two saved chats compacting as jobs: their busy marks, and Stop compacting in kv's menu (`chat:compacting`). */
export const Compacting = () => {
  const streaming = streamingRules('start')
  const rules = withLibraryRules({ ...streaming, jobs: [retryChat, kvChat].map((id) => ({ arguments: ['chats', 'compact', id, '--yes'], answer: { stdout: 'compacted', exitAfter: 30 } })) })
  return (
    <Sidebar
      model={useLibraryPreview({
        rules,
        then: (library) => {
          void library.compactions.compact(retryChat)
          void library.compactions.compact(kvChat)
          library.toggleRowMenu(kvChat)
        },
      })}
    />
  )
}

/** A private session open: its row under Open now, in no saved list. */
export const PrivateOpen = () => <Sidebar model={useLibraryPreview({ launch: { privately: true }, rules: withLibraryRules({ chats: [{ arguments: ['chat'], answer: { stdout: '' } }] }) })} />

/** No binary: the Mac app's words, no rows. */
export const Missing = () => <Sidebar model={useLibraryPreview({ apogee: 'missing', unopened: true })} />

/** A config the CLI refuses: why the sessions could not be read, in its words. */
export const Refused = () => <Sidebar model={useLibraryPreview({ apogee: 'config-typo', unopened: true })} />

function Rename({ model }: { readonly model: SessionLibraryViewModel }) {
  return (
    <div className="ap" data-theme="dark" style={{ width: 460, padding: 20, display: 'flex', flexDirection: 'column', gap: 12, background: 'var(--surface-card)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-small)' }}>
      <RenameSheet model={model} />
    </div>
  )
}

/** Rename…: the sheet on the chat's name, and the command it runs. */
export const Renaming = () => (
  <Rename
    model={useLibraryPreview({
      then: (library) => {
        const chat = library.chats.find((entry) => entry.id === kvChat)
        if (chat) library.askToRename(chat)
      },
    })}
  />
)

/** The name cleared: Rename is off. */
export const RenamingEmpty = () => (
  <Rename
    model={useLibraryPreview({
      then: (library) => {
        const chat = library.chats.find((entry) => entry.id === kvChat)
        if (chat) library.askToRename(chat)
        library.setRenameName('')
      },
    })}
  />
)
