import { kindColor } from '../../styles/Components/ModelGlyph'
import { AttachmentKind } from '../ViewModels/SessionActions'
import type { FolderFile, ModelFace, SessionFace, SlashItem } from '../ViewModels/SessionViewModel'
import { Icon } from '../../styles/Components/Icon'
import type { IconName } from '../../styles/Icons/IconNames'

/** The glyph and its color, promoted to `styles` by design 1h (its second user, the Models place): re-exported, so the session's views keep their imports. */
export { kindColor, ModelGlyph } from '../../styles/Components/ModelGlyph'

/** The kind's tag: `local · this Mac`, `API · billed per use`, in its color; nothing where the kind is not known. */
export function ModelTag({ model }: { readonly model: ModelFace }) {
  if (model.tag === '') return null
  return (
    <span className="mono" style={{ padding: '1px 6px', borderRadius: 4, background: 'var(--surface-control)', fontSize: 'var(--t-label)', whiteSpace: 'nowrap', color: kindColor(model.kind) }}>
      {model.tag}
    </span>
  )
}

export interface EmptySessionActions {
  readonly attachFolder: () => void
  readonly chooseModel: () => void
  readonly chooseSuite: () => void
  readonly privateInstead: () => void
}

/**
 * A session with nothing said yet (`main.html:251-270` at `b375fac9`): what it
 * is, the model it runs on, the first acts -- or, private, what is and is not
 * kept -- and what the CLI said at launch, under it. The Mac app's
 * `EmptySessionView`. A session nobody launched draws the acts that need one
 * off.
 */
export function EmptySessionView({ face, model, acts }: { readonly face: SessionFace['empty']; readonly model: ModelFace; readonly acts: EmptySessionActions }) {
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, overflowY: 'auto', display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 16, padding: 32, textAlign: 'center' }}>
      {face.privateWords !== '' ? (
        <>
          <span aria-hidden="true" style={{ width: 44, height: 44, display: 'grid', placeItems: 'center', borderRadius: 10, background: 'var(--surface-control)', color: 'var(--text-secondary)' }}>
            <Icon name="private" size={22} />
          </span>
          <h2 style={{ margin: 0, fontSize: 'var(--t-display)', lineHeight: 1.25, fontWeight: 600 }}>{face.heading}</h2>
          <p style={{ margin: 0, maxWidth: 460, fontSize: 'var(--t-prose)', color: 'var(--text-secondary)' }}>{face.privateWords}</p>
        </>
      ) : (
        <h2 style={{ margin: 0, fontSize: 'var(--t-display)', lineHeight: 1.25, fontWeight: 600 }}>{face.heading}</h2>
      )}
      {model.where !== '' ? (
        <p style={{ margin: 0, display: 'flex', alignItems: 'center', gap: 8, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
          <ModelTag model={model} />
          {model.where}
        </p>
      ) : null}
      <div style={{ display: 'flex', flexWrap: 'wrap', justifyContent: 'center', gap: 8, marginTop: 4 }}>
        <button type="button" className="btn-g" onClick={acts.attachFolder} disabled={!face.launched}>
          <Icon name="folder" size={14} />
          Attach a folder
        </button>
        <button type="button" className="btn-g" onClick={acts.chooseModel} disabled={!face.launched}>
          <Icon name="models" size={14} />
          Choose a model
        </button>
        <button type="button" className="btn-g" onClick={acts.chooseSuite} disabled={!face.chooseSuite}>
          <Icon name="suites" size={14} />
          Choose a suite
        </button>
        {face.privateInstead ? (
          <button type="button" className="btn-g" onClick={acts.privateInstead}>
            <Icon name="private" size={14} />
            Private session instead
          </button>
        ) : null}
      </div>
      <p className="mono" style={{ margin: '8px 0 0', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
        / commands · @ mention a file · ⏎ send
      </p>
      {face.notices.map((notice, index) => (
        <p key={index} role="note" className="mono" style={{ margin: 0, maxWidth: 560, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>
          {notice}
        </p>
      ))}
    </div>
  )
}

/**
 * The context meter in the header (`main.html:239-243` at `b375fac9`): the
 * bar with its 80% and 90% marks and the words, or a dashed bar where it
 * cannot be known. The Mac app's `MeterView`. Its click opens the inspector.
 */
export function MeterView({ meter, open }: { readonly meter: SessionFace['header']['meter']; readonly open: () => void }) {
  const color = meter.warns ? 'var(--status-warning)' : 'var(--text-secondary)'
  return (
    <button
      type="button"
      className="hv"
      onClick={open}
      aria-label={`Context window: ${meter.known ? `${meter.percent} percent full` : meter.text}. Show details`}
      style={{ display: 'flex', alignItems: 'center', gap: 10, height: 32, padding: '0 8px', border: 0, borderRadius: 7, color: 'var(--text-tertiary)', fontSize: 'var(--t-small)' }}
    >
      <span>Context</span>
      {meter.known ? (
        <>
          <span aria-hidden="true" style={{ position: 'relative', display: 'block', width: 140, height: 6, borderRadius: 3, background: 'var(--line-border)' }}>
            <span style={{ display: 'block', width: `${meter.percent}%`, height: '100%', borderRadius: 3, background: color }} />
            <span style={{ position: 'absolute', left: '80%', top: -4, width: 1, height: 14, background: 'var(--text-tertiary)' }} />
            <span style={{ position: 'absolute', left: '90%', top: -4, width: 1, height: 14, background: 'var(--text-tertiary)' }} />
          </span>
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', whiteSpace: 'nowrap' }}>
            {meter.used} / {meter.window}
          </span>
          <span className="mono" style={{ fontSize: 'var(--t-meta)', fontWeight: 600, color }}>
            {meter.percent}%
          </span>
        </>
      ) : (
        <>
          <span aria-hidden="true" style={{ display: 'block', width: 138, height: 4, borderRadius: 3, border: '1px dashed var(--line-strong)' }} />
          <span className="mono" style={{ fontSize: 'var(--t-meta)', whiteSpace: 'nowrap' }}>
            {meter.text}
          </span>
        </>
      )}
    </button>
  )
}

/** Each attachment kind's glyph: the old chips' own, copied as drawn (`main.html:731-736` at `b375fac9`). */
/** Each attachment kind's icon, from `styles`' set (design 1q): the old prototype's glyphs, the TypeSpace board's names. */
const attachmentIcons: Readonly<Record<AttachmentKind, IconName>> = { folder: 'folder', file: 'file', pdf: 'document', image: 'image', audio: 'audio', video: 'video' }

/** What the session holds attached, each with its kind's glyph (read from the path) and its remove where the session takes `detach` (`main.html:728-743` at `b375fac9`). The Mac app's `AttachmentChips`. */
export function AttachmentChips({ paths, detach }: { readonly paths: readonly string[]; readonly detach: ((path: string) => void) | undefined }) {
  if (paths.length === 0) return null
  return (
    <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, padding: '10px 12px 0' }}>
      {paths.map((path) => (
        <span key={path} className="chip" style={detach ? undefined : { paddingRight: 10 }}>
          <Icon name={attachmentIcons[AttachmentKind.of(path)]} size={13} style={{ color: 'var(--text-secondary)' }} />
          <span className="mono" style={{ fontSize: 'var(--t-ui)' }}>
            {path}
          </span>
          {detach ? (
            <button type="button" onClick={() => detach(path)} aria-label={`Remove ${path}`} className="ib" style={{ width: 22, height: 22, color: 'var(--text-tertiary)' }}>
              <Icon name="close" size={11} />
            </button>
          ) : null}
        </span>
      ))}
    </div>
  )
}

/** A row picked with the mouse keeps the input's focus. */
const keep = (event: { preventDefault: () => void }) => event.preventDefault()

export type Suggestions =
  | { readonly kind: 'commands'; readonly items: readonly SlashItem[]; readonly query: string }
  | { readonly kind: 'files'; readonly items: readonly FolderFile[]; readonly folder: string }

/**
 * The composer's `/` and `@` menus (`main.html:695-716` at `b375fac9`): the
 * commands the draft names, or the symphonies after `/play `, or the
 * folder's files; the highlighted row, and the keys. The Mac app's `SuggestionList`.
 */
export function SuggestionList({ suggestions, active, pickCommand, pickFile }: {
  readonly suggestions: Suggestions
  readonly active: number
  readonly pickCommand: (item: SlashItem) => void
  readonly pickFile: (file: FolderFile) => void
}) {
  const background = (index: number) => (index === active ? 'var(--surface-selection)' : undefined)
  if (suggestions.kind === 'commands') {
    return (
      <div className="menu" role="listbox" aria-label="Commands" style={{ position: 'absolute', left: 0, bottom: 'calc(100% + 6px)', zIndex: 40, width: 'min(560px, 100%)' }}>
        <div style={{ maxHeight: 320, overflowY: 'auto', padding: 4 }}>
          {suggestions.items.map((item, index) => (
            <button
              key={item.name}
              type="button"
              role="option"
              aria-selected={index === active}
              onMouseDown={keep}
              onClick={() => pickCommand(item)}
              className="hv"
              style={{ width: '100%', display: 'grid', gridTemplateColumns: '150px minmax(0, 1fr)', columnGap: 12, alignItems: 'baseline', padding: '6px 10px', border: 0, borderRadius: 6, background: background(index), color: 'var(--text-primary)', textAlign: 'left' }}
            >
              <span className="mono" style={{ fontSize: 'var(--t-ui)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                /{item.name} <span style={{ color: 'var(--text-tertiary)' }}>{item.args}</span>
              </span>
              <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{item.summary}</span>
            </button>
          ))}
          {suggestions.items.length === 0 ? (
            <p style={{ margin: 0, padding: '8px 10px', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>No command starts with “{suggestions.query}”.</p>
          ) : null}
        </div>
        <div className="mono" style={{ padding: '6px 12px', borderTop: '1px solid var(--line-border)', fontSize: 'var(--t-label)', color: 'var(--text-tertiary)' }}>
          ↑↓ choose · ⇥ or ⏎ complete · esc close
        </div>
      </div>
    )
  }
  return (
    <div className="menu" role="listbox" aria-label="Files" style={{ position: 'absolute', left: 0, bottom: 'calc(100% + 6px)', zIndex: 40, width: 'min(460px, 100%)' }}>
      <div style={{ padding: 4 }}>
        {suggestions.items.map((file, index) => (
          <button
            key={file.path}
            type="button"
            role="option"
            aria-selected={index === active}
            onMouseDown={keep}
            onClick={() => pickFile(file)}
            className="hv"
            style={{ width: '100%', display: 'flex', alignItems: 'center', gap: 8, height: 30, padding: '0 10px', border: 0, borderRadius: 6, background: background(index), color: 'var(--text-secondary)', textAlign: 'left' }}
          >
            <Icon name="file" size={13} />
            <span className="mono" style={{ flex: '1 1 auto', minWidth: 0, fontSize: 'var(--t-ui)', color: 'var(--text-primary)' }}>
              {file.path}
            </span>
            <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
              {file.size}
            </span>
          </button>
        ))}
        {suggestions.items.length === 0 ? (
          <p style={{ margin: 0, padding: '8px 10px', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>No file in {suggestions.folder} matches.</p>
        ) : null}
      </div>
      <div className="mono" style={{ padding: '6px 12px', borderTop: '1px solid var(--line-border)', fontSize: 'var(--t-label)', color: 'var(--text-tertiary)' }}>
        in {suggestions.folder} · ↑↓ choose · ⏎ attach · esc close
      </div>
    </div>
  )
}

/**
 * The held lines' strip above the input (`main.html:744` at `b375fac9`): when
 * they go and each in a few words, its help on hover -- the Mac app's
 * `HeldLines`, its `heldWords` and `heldHelp`.
 */
export function HeldLines({ held }: { readonly held: { readonly shown: boolean; readonly words: string; readonly help: string } }) {
  if (!held.shown) return null
  return (
    <div className="mono" title={held.help} style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '10px 14px 0', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>
      <Icon name="held" size={12} />
      <span>{held.words}</span>
    </div>
  )
}
