import chatsList from '@recorded/business/reads/Fixtures/chats-list.json?raw'
import { ConfirmHost, MenuScrim, SheetHost, ToastHost } from '../Views/Hosts'
import { PlaceholderPanel, PlaceholderScreen } from '../Views/PlaceholderScreen'
import { placeholderOf } from '../ViewModels/Destination'

/**
 * The hosts on their own (design 1e), each in a window-sized frame. What a
 * sheet or a confirm says is its place's; the confirm here says what the old
 * prototype's `deleteChat` says, over a recorded chat.
 */
const frame = { position: 'relative', width: 960, height: 640, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)' } as const

const chat = (JSON.parse(chatsList) as { data: Array<{ id: string; name: string }> }).data[0] ?? { id: '', name: '' }

/** One line at the bottom of the window. */
export const Toast = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <ToastHost text="Theme: Graphite Light" />
  </div>
)

/** The scrim behind an open menu. */
export const Scrim = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <MenuScrim onClose={() => {}} />
  </div>
)

/** A sheet: Settings' place until 1i ports it. */
export const Sheet = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <SheetHost title="Settings" width={460} right={[{ label: 'Close', kind: 'btn', run: () => {} }]}>
      <p style={{ margin: 0, color: 'var(--text-secondary)' }}>Settings isn’t drawn here yet. Design item 1i ports it.</p>
    </SheetHost>
  </div>
)

/** A confirm: the old prototype's words for deleting a saved chat, over a recorded one. */
export const Confirm = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <ConfirmHost
      title={`Delete “${chat.name}”?`}
      body={`apogee chats delete ${chat.id} -- the conversation and its turns are removed, and this can’t be undone.`}
      cancel={{ label: 'Cancel', run: () => {} }}
      confirm={{ label: 'Delete', destructive: true, run: () => {} }}
    />
  </div>
)

/** A place not yet ported. */
export const Placeholder = () => {
  const placeholder = placeholderOf('training')
  return (
    <div className="ap" data-theme="dark" style={{ ...frame, display: 'flex', flexDirection: 'column' }}>
      {placeholder ? <PlaceholderScreen placeholder={placeholder} /> : null}
    </div>
  )
}

/** An overlay not yet ported, as the console's was until 1g: its title, what it says, and Close. */
export const Panel = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <PlaceholderPanel title="Console" says="An overlay's place over the window: its view is its module's, handed in by app." onClose={() => {}} />
  </div>
)
