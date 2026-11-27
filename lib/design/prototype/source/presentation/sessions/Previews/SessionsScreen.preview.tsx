import privateUnsent from '@recorded/data/protocol/Fixtures/private-unsent.jsonl?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import { chatRetryPreset, kvChat } from '../../session/Previews/SessionPresets'
import type { SessionLibraryViewModel } from '../ViewModels/SessionLibraryViewModel'
import { ChatInfoSheet, InspectorPane, RecallSummaries, SessionsScreen } from '../Views/SessionsScreen'
import { useLibraryPreview } from './LibraryPreview'
import { withLibraryRules } from './LibraryRules'

/**
 * The Sessions place on its own (design 1g): the session window with the
 * inspector beside it, the inspector's states, recall's summaries and the
 * details sheet, each over the stand-in.
 */
const window = { position: 'relative', width: 1184, height: 760, display: 'flex', flexDirection: 'column', background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Screen({ model }: { readonly model: SessionLibraryViewModel }) {
  return (
    <div className="ap" data-theme="dark" style={window}>
      <SessionsScreen model={model} />
    </div>
  )
}

function Pane({ model }: { readonly model: SessionLibraryViewModel }) {
  return (
    <div className="ap" data-theme="dark" style={{ ...window, width: 304, flexDirection: 'row' }}>
      <InspectorPane model={model} />
    </div>
  )
}

const newChat = withLibraryRules({ chats: [{ arguments: ['chat'], answer: { stdout: newUnsent } }] })
const privateChat = withLibraryRules({ chats: [{ arguments: ['chat'], answer: { stdout: privateUnsent } }] })

/** `chat`: Streaming mid-reasoning, the inspector open beside it. */
export const Streaming = () => <Screen model={useLibraryPreview()} />

/** The inspector hidden (⌥⌘I): the session takes the width. */
export const InspectorHidden = () => <Screen model={useLibraryPreview({ then: (library) => library.toggleInspector() })} />

/** The inspector's default sections: SESSION, THIS TURN, CONTEXT and MODEL open, HELPERS and RECALL closed. */
export const Inspector = () => <Pane model={useLibraryPreview()} />

/** Expand all: every section open, the helpers from models status and what recall holds. */
export const InspectorExpanded = () => <Pane model={useLibraryPreview({ then: (library) => library.toggleAllSections() })} />

/** Collapse all: every section closed, each heading's summary still said. */
export const InspectorCollapsed = () => (
  <Pane
    model={useLibraryPreview({
      then: (library) => {
        library.toggleAllSections()
        library.toggleAllSections()
      },
    })}
  />
)

/** A new chat, unsent: not saved yet, no turn yet. */
export const InspectorNew = () => <Pane model={useLibraryPreview({ rules: newChat, launch: {} })} />

/** A private chat: its name alone. */
export const InspectorPrivate = () => <Pane model={useLibraryPreview({ rules: privateChat, launch: { privately: true } })} />

/** `chat:retry`: the turn waiting for you at the permission gate. */
export const InspectorWaiting = () => (
  <Pane
    model={useLibraryPreview({
      rules: withLibraryRules(chatRetryPreset({ still: true, apogee: 'installed' }).rules),
      launch: {},
      ticks: 60,
      before: (library) => library.current.writeAhead('Make `_fetch()` retry timeouts and connection errors, using `settings.fetch.retries` (it’s already in config.toml). Go ahead and edit the file.'),
    })}
  />
)

function Recall({ model }: { readonly model: SessionLibraryViewModel }) {
  const face = model.inspector()
  return (
    <div className="ap" data-theme="dark" style={{ width: 268, padding: 18, background: 'var(--surface-inspector)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-small)' }}>
      <RecallSummaries summaries={face.recall.summaries} choose={(chat) => model.selectRecall(chat)} />
    </div>
  )
}

/** What recall holds, summary by summary, none chosen. */
export const RecallListed = () => <Recall model={useLibraryPreview()} />

/** One chosen: the text a later session is handed. */
export const RecallChosen = () => <Recall model={useLibraryPreview({ then: (library) => library.selectRecall(kvChat) })} />

function Details({ model }: { readonly model: SessionLibraryViewModel }) {
  return (
    <div className="ap" data-theme="dark" style={{ width: 460, padding: 20, display: 'flex', flexDirection: 'column', gap: 12, background: 'var(--surface-card)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-small)' }}>
      <ChatInfoSheet model={model} />
    </div>
  )
}

/** Details…: chats info's facts under the text's labels, and the command it read. */
export const ChatDetails = () => (
  <Details
    model={useLibraryPreview({
      then: async (library) => {
        const chat = library.chats.find((entry) => entry.id === kvChat)
        if (chat) await library.showInfo(chat)
      },
    })}
  />
)

/** Details… on a chat nobody recorded: the stand-in's word. */
export const ChatDetailsUnread = () => (
  <Details
    model={useLibraryPreview({
      then: async (library) => {
        await library.showInfo({ id: '20261001-000000-0000', name: 'Not recorded', updated: undefined, turns: 1, backend: undefined, model: undefined })
      },
    })}
  />
)
