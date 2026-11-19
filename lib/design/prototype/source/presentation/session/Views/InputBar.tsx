import type { KeyboardEvent } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import type { SessionViewModel } from '../ViewModels/SessionViewModel'
import { AttachmentChips, SuggestionList } from './SessionExtras'

/**
 * The composer (`main.html:693-759` at `b375fac9`): the `/`, `@` and attach
 * menus over it, the attached chips, the input, and its row -- attach, the
 * keys, the turn's status, and Send, or Stop while a turn runs. The Mac app's
 * `InputBar`. The held lines' strip is 1v's.
 */
export function InputBar({ model }: { readonly model: SessionViewModel }) {
  useObserved(model)
  const face = model.face().composer
  const slash = model.slashOpen ? model.slashItems : undefined
  const mention = model.mentionOpen ? model.mentionItems : undefined

  const onKey = (event: KeyboardEvent<HTMLTextAreaElement>) => {
    if ((event.key === 'ArrowDown' || event.key === 'ArrowUp') && model.moveSelection(event.key === 'ArrowDown' ? 1 : -1)) {
      event.preventDefault()
      return
    }
    if ((event.key === 'Tab' || (event.key === 'Enter' && !event.shiftKey)) && model.completeSelection()) {
      event.preventDefault()
      return
    }
    if (event.key === 'Escape') {
      event.preventDefault()
      // Esc closes the menus; with none open it stops the turn, as the Mac app's does.
      if (model.slashOpen || model.mentionOpen || model.menu) model.dismissMenus()
      else model.cancel()
      return
    }
    if (event.key === '.' && event.metaKey) {
      event.preventDefault()
      model.cancel()
      return
    }
    if (event.key === 'Enter' && !event.shiftKey) {
      event.preventDefault()
      if (!face.streaming) model.submit()
    }
  }

  return (
    <div className="ap-compose" style={{ position: 'relative', zIndex: 30, flex: '0 0 auto', padding: '0 28px 18px' }}>
      <div style={{ position: 'relative', maxWidth: 860, margin: '0 auto' }}>
        {slash ? (
          <SuggestionList suggestions={{ kind: 'commands', items: slash, query: model.slashQuery }} active={model.activeIndex} pickCommand={(item) => model.pickSlash(item)} pickFile={() => {}} />
        ) : null}
        {mention ? (
          <SuggestionList suggestions={{ kind: 'files', items: mention, folder: model.listing.folder }} active={model.activeIndex} pickCommand={() => {}} pickFile={(file) => model.pickMention(file)} />
        ) : null}
        {model.menu === 'attach' ? (
          <div className="menu" role="menu" aria-label="Attach" style={{ position: 'absolute', left: 0, bottom: 'calc(100% + 6px)', zIndex: 40, width: 280 }}>
            <div style={{ padding: 4 }}>
              {model.attachChoices.map((choice) => (
                <button
                  key={choice.label}
                  type="button"
                  role="menuitem"
                  onClick={() => model.attach(choice.path)}
                  className="hv"
                  style={{ width: '100%', display: 'flex', alignItems: 'center', gap: 10, height: 32, padding: '0 10px', border: 0, borderRadius: 6, color: 'var(--text-primary)', fontSize: 'var(--t-body)', textAlign: 'left' }}
                >
                  <span style={{ flex: '1 1 auto' }}>{choice.label}</span>
                  <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                    {choice.hint}
                  </span>
                </button>
              ))}
            </div>
            <div style={{ padding: '8px 12px', borderTop: '1px solid var(--line-border)', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>
              Or drag files in, or type @. In this prototype each item attaches a sample.
            </div>
          </div>
        ) : null}
        <div style={{ border: '1px solid var(--line-strong)', borderRadius: 8, background: 'var(--surface-card)' }}>
          <AttachmentChips paths={face.attachments} detach={model.transcript.accepts.includes('detach') ? (path) => model.detach(path) : undefined} />
          <div style={{ display: 'flex', gap: 10, padding: '12px 14px 2px' }}>
            <span className="mono" aria-hidden="true" style={{ color: 'var(--status-accent)', fontSize: 'var(--t-prose)', lineHeight: '22px' }}>
              ›
            </span>
            <textarea
              aria-label="Message"
              rows={2}
              value={face.draft}
              onChange={(event) => model.setDraft(event.target.value)}
              onKeyDown={onKey}
              placeholder={face.placeholder}
              disabled={model.transcript.status.case === 'ended' || model.connection.case === 'missing'}
              style={{ flex: '1 1 auto', minWidth: 0, resize: 'none', padding: 0, border: 0, outline: 'none', background: 'transparent', fontSize: 'var(--t-prose)', lineHeight: '22px', color: 'var(--text-primary)' }}
            />
          </div>
          <div style={{ display: 'flex', alignItems: 'center', gap: 6, padding: '4px 8px 8px' }}>
            <button
              type="button"
              className="ib"
              onClick={() => model.toggleMenu('attach')}
              disabled={!face.canAttach}
              aria-expanded={model.menu === 'attach'}
              aria-label="Attach files, folders, images, audio or video"
              style={{ width: 28, height: 28 }}
            >
              <svg className="i" width="15" height="15" viewBox="0 0 16 16" aria-hidden="true">
                <path d="M13 7.5l-5.2 5.2a3 3 0 0 1-4.3-4.3l5.6-5.6a2 2 0 0 1 2.9 2.9l-5.5 5.5a1 1 0 0 1-1.4-1.4l5-5" />
              </svg>
            </button>
            <span className="ap-hints mono" style={{ display: 'flex', gap: 14, marginLeft: 4, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
              <span>
                <span style={{ color: 'var(--text-secondary)' }}>/</span> commands
              </span>
              <span>
                <span style={{ color: 'var(--text-secondary)' }}>@</span> files
              </span>
              <span>
                <span style={{ color: 'var(--text-secondary)' }}>⏎</span> send
              </span>
              <span>
                <span style={{ color: 'var(--text-secondary)' }}>⇧⏎</span> newline
              </span>
            </span>
            <span style={{ flex: '1 1 auto' }} />
            <span className="mono" role="status" style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 'var(--t-meta)', color: model.transcript.status.case === 'ended' ? 'var(--status-error)' : 'var(--text-tertiary)' }}>
              {face.status}
            </span>
            {face.streaming ? (
              <button
                type="button"
                className="mono"
                onClick={() => model.cancel()}
                aria-label="Stop (Command period)"
                style={{ display: 'flex', alignItems: 'center', gap: 8, height: 28, padding: '0 10px', background: 'var(--text-primary)', color: 'var(--surface-window)', border: 0, borderRadius: 5, fontSize: 'var(--t-ui)', fontWeight: 600 }}
              >
                <span aria-hidden="true" style={{ width: 8, height: 8, borderRadius: 1, background: 'currentColor' }} />
                stop<span style={{ fontWeight: 400, opacity: 0.7 }}>⌘.</span>
              </button>
            ) : (
              <button
                type="button"
                className="mono"
                onClick={() => model.submit()}
                disabled={!face.canSend && !face.draft.trim().startsWith('/')}
                aria-label="Send (Return)"
                style={{ display: 'flex', alignItems: 'center', gap: 8, height: 28, padding: '0 10px', background: 'var(--text-primary)', color: 'var(--surface-window)', border: 0, borderRadius: 5, fontSize: 'var(--t-ui)', fontWeight: 600 }}
              >
                <svg className="i" width="12" height="12" viewBox="0 0 16 16" aria-hidden="true">
                  <path d="M8 13V3.5M4 7.5l4-4 4 4" />
                </svg>
                send<span style={{ fontWeight: 400, opacity: 0.7 }}>⏎</span>
              </button>
            )}
          </div>
        </div>
      </div>
    </div>
  )
}
