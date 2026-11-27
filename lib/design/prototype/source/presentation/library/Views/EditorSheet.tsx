import { useObserved } from '../../styles/Observation/useObserved'
import type { LibraryViewModel } from '../ViewModels/LibraryViewModel'

/**
 * The editor sheet's content (4h's `DefinitionEditor`, 6f's close prompt; the
 * old `gs.kind === 'editor'` sheet at `b375fac9`): the file as typed, read
 * only while a save waits for apogee's word -- said by its busy line -- and
 * that word after it, the CLI's refusal in the error color. The shell's
 * sheet host frames it, its title, the file's path and its buttons handed in
 * by `app`.
 */
export function EditorSheet({ model }: { readonly model: LibraryViewModel }) {
  useObserved(model)
  const editor = model.editorFace()
  if (!editor.shown) return null
  return (
    <>
      <textarea
        rows={12}
        value={editor.text}
        onChange={(event) => model.setEditorText(event.target.value)}
        disabled={editor.saving}
        aria-label="The file"
        style={{ boxSizing: 'border-box', width: '100%', minHeight: 320, resize: 'vertical', padding: '10px 12px', border: '1px solid var(--line-strong)', borderRadius: 6, background: 'var(--surface-window)', fontFamily: 'var(--font-mono)', fontSize: 'var(--t-code)', lineHeight: 1.6, color: 'var(--text-primary)', opacity: editor.saving ? 0.6 : 1 }}
      />
      {editor.verdict.text !== '' ? <p style={{ margin: 0, color: editor.verdict.refused ? 'var(--status-error)' : 'var(--text-secondary)', overflowWrap: 'anywhere', whiteSpace: 'pre-wrap' }}>{editor.verdict.text}</p> : null}
      {editor.busy !== '' ? (
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, color: 'var(--text-secondary)' }}>
          <span className="pulse" aria-hidden="true" style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--status-accent)' }} />
          <span className="mono" style={{ fontSize: 'var(--t-meta)', overflowWrap: 'anywhere' }}>
            {editor.busy}
          </span>
        </div>
      ) : null}
    </>
  )
}
