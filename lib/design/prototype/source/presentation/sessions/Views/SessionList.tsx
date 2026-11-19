import { useEffect, useRef } from 'react'
import { ModelGlyph } from '../../session/Views/SessionExtras'
import { useObserved } from '../../styles/Observation/useObserved'
import type { ChatRowFace } from '../ViewModels/LibraryFaces'
import type { SessionLibraryViewModel } from '../ViewModels/SessionLibraryViewModel'

const chevron = (open: boolean, color?: string) => (
  <svg className="i" width="10" height="10" viewBox="0 0 16 16" aria-hidden="true" style={{ color, transform: `rotate(${open ? 0 : -90}deg)`, transition: 'transform var(--motion-chevron)' }}>
    <path d="M4 6l4 4 4-4" />
  </svg>
)

const eye = (
  <>
    <path d="M2 8s2.2-4 6-4c1 0 1.9.3 2.7.7M14 8s-2.2 4-6 4c-1 0-1.9-.3-2.7-.7" />
    <path d="M6.6 9.4a2 2 0 0 1 2.8-2.8" />
    <path d="M3 13L13 3" />
  </>
)

/** What each glyph says to a screen reader, as the old list names it. */
const glyphLabels = { local: 'local', api: 'cloud API, billed', subscription: 'subscription', unknown: '' } as const

/**
 * The window's top row's three new-session buttons (design 1g): New execute
 * session (⌥⌘N), New private session (⇧⌘N) and New session (⌘N), the old
 * sidebar's (`main.html:85-87` at `b375fac9`). Each asks through the open
 * session first, as the old `leaveFor` does, where a private conversation
 * would be lost.
 */
export function NewSessionButtons({ model }: { readonly model: SessionLibraryViewModel }) {
  return (
    <>
      <button type="button" className="ib ap-lab" onClick={() => model.current.leave({ type: 'execute' })} aria-label="New execute session (Option Command N)" title="New execute session ⌥⌘N">
        <svg className="i" width="15" height="15" viewBox="0 0 16 16" aria-hidden="true">
          <path d="M2.5 13.5l6-6" />
          <path d="M7.5 3.5l3-1.5 3.5 3.5-1.5 3-1.5-1.5-2 2-2.5-2.5 2-2z" />
        </svg>
      </button>
      <button type="button" className="ib ap-lab" onClick={() => model.newPrivateSession()} aria-label="New private session (Shift Command N)" title="New private session ⇧⌘N">
        <svg className="i" width="15" height="15" viewBox="0 0 16 16" aria-hidden="true">
          {eye}
        </svg>
      </button>
      <button type="button" className="ib" onClick={() => model.newSession()} aria-label="New session (Command N)" title="New session ⌘N">
        <svg className="i" width="15" height="15" viewBox="0 0 16 16" aria-hidden="true">
          <path d="M8 3H4.5A1.5 1.5 0 0 0 3 4.5v7A1.5 1.5 0 0 0 4.5 13h7a1.5 1.5 0 0 0 1.5-1.5V8" />
          <path d="M11.5 2.5l2 2L8 10H6V8z" />
        </svg>
      </button>
    </>
  )
}

function ChatRow({ model, row }: { readonly model: SessionLibraryViewModel; readonly row: ChatRowFace }) {
  return (
    <div className="ap-crow" style={{ position: 'relative' }}>
      <button
        type="button"
        className="hv"
        onClick={() => model.open(row.id)}
        aria-current={row.current ? 'true' : undefined}
        title={row.help}
        style={{ width: '100%', display: 'flex', alignItems: 'center', gap: 8, height: 28, padding: '0 8px 0 24px', marginBottom: 1, border: 0, borderRadius: 'var(--radius-row)', background: row.current ? 'var(--surface-selection)' : undefined, color: row.current ? 'var(--text-primary)' : 'var(--text-secondary)', fontSize: 'var(--t-dense)', textAlign: 'left' }}
      >
        {row.kind === 'unknown' ? (
          // A backend the config types as none of the three (the mock, or one this config lists no row for): the glyph's place kept, so titles start in line.
          <span data-glyph="none" aria-hidden="true" style={{ flex: '0 0 auto', width: 12, height: 12 }} />
        ) : (
          <ModelGlyph kind={row.kind} size={12} label={glyphLabels[row.kind]} color={row.kind === 'local' && !row.current ? 'var(--text-tertiary)' : undefined} />
        )}
        <span style={{ flex: '1 1 auto', minWidth: 0, overflow: 'hidden', whiteSpace: 'nowrap', textOverflow: 'ellipsis' }}>{row.title}</span>
        {row.busy ? (
          <span className="pulse" role="img" aria-label="compacting" style={{ flex: '0 0 auto', width: 6, height: 6, marginRight: 4, borderRadius: 'var(--radius-dot)', background: 'var(--status-accent)' }} />
        ) : (
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
            {row.time}
          </span>
        )}
      </button>
      <button
        type="button"
        className="ib ap-more"
        onClick={() => model.toggleRowMenu(row.id)}
        aria-haspopup="menu"
        aria-expanded={row.menuOpen}
        aria-label={`Actions for ${row.title}`}
        style={{ position: 'absolute', top: 2, right: 4, width: 24, height: 24, background: 'var(--surface-control)', color: 'var(--text-secondary)' }}
      >
        <svg className="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true">
          <path d="M3.5 8h.1M8 8h.1M12.5 8h.1" strokeWidth="2.4" />
        </svg>
      </button>
      {row.menuOpen ? (
        <div className="menu" role="menu" aria-label={row.title} style={{ position: 'absolute', top: 28, right: 0, zIndex: 41, width: 170, padding: 4 }}>
          {row.items.map((item) => (
            <button
              key={item.label}
              type="button"
              role="menuitem"
              className="hv"
              onClick={() => model.rowAct(row.id, item.act)}
              style={{ width: '100%', display: 'flex', alignItems: 'center', height: 28, padding: '0 10px', border: 0, borderRadius: 'var(--radius-row)', background: 'transparent', color: item.destructive ? 'var(--status-error)' : 'var(--text-primary)', fontSize: 'var(--t-dense)', textAlign: 'left' }}
            >
              {item.label}
            </button>
          ))}
        </div>
      ) : null}
    </div>
  )
}

/**
 * The sidebar's session list (design 1g): the old prototype's (`main.html:90-127`
 * at `b375fac9`) -- the SESSIONS heading with its count and the date groups'
 * one fold, search (⇧⌘F), the private session open now, the date groups
 * (Today, Yesterday, Previous 7 days, Earlier), a row per saved conversation
 * with its model's glyph and when it changed -- or its busy mark -- and its
 * menu: Details…, Rename…, Compact… (Stop compacting while one runs) and
 * Delete…. The Mac app's `SessionList`, its names; the shell draws it in its
 * sidebar's slot on every screen.
 */
export function SessionList({ model }: { readonly model: SessionLibraryViewModel }) {
  useObserved(model)
  useObserved(model.current)
  const face = model.listFace()
  const search = useRef<HTMLInputElement>(null)
  useEffect(() => {
    if (face.searchRequests > 0) search.current?.focus()
  }, [face.searchRequests])
  return (
    <>
      <div style={{ position: 'sticky', top: 0, zIndex: 1, margin: '0 -8px', padding: '6px 8px 4px', background: 'var(--surface-sidebar)' }}>
        <div style={{ display: 'flex', alignItems: 'center', gap: 2 }}>
          <button
            type="button"
            className="hv"
            onClick={() => model.toggleList()}
            aria-expanded={face.open}
            style={{ flex: '1 1 auto', display: 'flex', alignItems: 'center', gap: 6, height: 26, padding: '0 8px', border: 0, borderRadius: 'var(--radius-row)', background: 'transparent', color: 'var(--text-tertiary)', textAlign: 'left' }}
          >
            {chevron(face.open)}
            <span className="lbl" style={{ flex: '1 1 auto' }}>
              SESSIONS
            </span>
            <span className="mono" style={{ fontSize: 'var(--t-meta)' }}>
              {face.count}
            </span>
          </button>
          <button type="button" className="ib" onClick={() => model.toggleDates()} aria-label={face.datesLabel} title={face.datesLabel} style={{ width: 26, height: 26, color: 'var(--text-tertiary)' }}>
            <svg className="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true">
              <path d={face.anyDateOpen ? 'M4.5 6.5L8 3l3.5 3.5M4.5 9.5L8 13l3.5-3.5' : 'M4.5 3.5L8 7l3.5-3.5M4.5 12.5L8 9l3.5 3.5'} />
            </svg>
          </button>
        </div>
        {face.open ? (
          <label className="ap-search" style={{ margin: '4px 4px 2px', display: 'flex', alignItems: 'center', gap: 8, height: 28, padding: '0 8px', border: '1px solid var(--line-border)', borderRadius: 'var(--radius-control)', color: 'var(--text-tertiary)' }}>
            <svg className="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true">
              <circle cx="7" cy="7" r="4.25" />
              <path d="M10.2 10.2L13.5 13.5" />
            </svg>
            <input
              ref={search}
              type="search"
              value={face.query}
              onChange={(event) => model.setQuery(event.target.value)}
              placeholder="Search sessions"
              aria-label="Search sessions"
              style={{ flex: '1 1 auto', minWidth: 0, height: 24, padding: 0, border: 0, background: 'transparent', fontSize: 'var(--t-dense)', color: 'var(--text-primary)' }}
            />
            <span className="mono" style={{ fontSize: 'var(--t-meta)' }}>
              ⇧⌘F
            </span>
          </label>
        ) : null}
      </div>
      {face.open ? (
        <>
          {face.privateRow.shown ? (
            <>
              <div style={{ display: 'flex', alignItems: 'center', gap: 6, height: 26, marginTop: 4, padding: '0 8px 0 24px', fontSize: 'var(--t-small)', fontWeight: 500, color: 'var(--text-secondary)' }}>Open now</div>
              <button
                type="button"
                className="hv"
                onClick={() => model.showPrivate()}
                style={{ width: '100%', display: 'flex', alignItems: 'center', gap: 8, height: 28, padding: '0 8px 0 24px', border: 0, borderRadius: 'var(--radius-row)', background: face.privateRow.current ? 'var(--surface-selection)' : undefined, color: 'var(--text-primary)', fontSize: 'var(--t-dense)', textAlign: 'left' }}
              >
                <svg className="i" width="12" height="12" viewBox="0 0 16 16" aria-hidden="true" style={{ color: 'var(--text-tertiary)' }}>
                  {eye}
                </svg>
                <span style={{ flex: '1 1 auto' }}>Private session</span>
                <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                  deleted on close
                </span>
              </button>
            </>
          ) : null}
          {face.groups.map((group) => (
            <div key={group.title}>
              <button
                type="button"
                className="hv"
                onClick={() => model.toggleGroup(group.title)}
                aria-expanded={group.open}
                style={{ width: '100%', display: 'flex', alignItems: 'center', gap: 6, height: 26, marginTop: 4, padding: '0 8px', border: 0, borderRadius: 'var(--radius-row)', background: 'transparent', color: 'var(--text-secondary)', fontSize: 'var(--t-small)', fontWeight: 500, textAlign: 'left' }}
              >
                {chevron(group.open, 'var(--text-tertiary)')}
                <span style={{ flex: '1 1 auto' }}>{group.title}</span>
                {group.hasCurrent ? <span aria-label="contains the open session" role="img" style={{ width: 5, height: 5, borderRadius: 'var(--radius-dot)', background: 'var(--status-accent)' }} /> : null}
                <span className="mono" style={{ minWidth: 16, fontSize: 'var(--t-meta)', fontWeight: 400, color: 'var(--text-tertiary)', textAlign: 'right' }}>
                  {group.count}
                </span>
              </button>
              {group.open ? group.rows.map((row) => <ChatRow key={row.id} model={model} row={row} />) : null}
            </div>
          ))}
          {face.noResults !== '' ? <p style={{ margin: '10px 8px', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{face.noResults}</p> : null}
          {face.matchText !== '' ? (
            <p className="mono" style={{ margin: '8px 8px 0', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
              {face.matchText}
            </p>
          ) : null}
          {face.empty !== '' ? <p style={{ margin: '10px 8px', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>{face.empty}</p> : null}
        </>
      ) : null}
    </>
  )
}

/**
 * The rename sheet's content (design 1g): the name, and the command it runs,
 * the old prototype's (`main.html`'s sheets section, `rename`, at
 * `b375fac9`); its title and buttons are the shell's sheet host's. The Mac
 * app's `RenameSheet`.
 */
export function RenameSheet({ model }: { readonly model: SessionLibraryViewModel }) {
  useObserved(model)
  const renaming = model.renaming
  if (!renaming) return null
  return (
    <>
      <input
        type="text"
        className="inp"
        value={renaming.name}
        onChange={(event) => model.setRenameName(event.target.value)}
        onKeyDown={(event) => {
          if (event.key === 'Enter' && renaming.canRename) void model.rename()
        }}
        placeholder="Name"
        aria-label="Name"
        style={{ width: '100%' }}
      />
      <p className="mono" style={{ margin: 0, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>
        {renaming.command}
      </p>
    </>
  )
}
