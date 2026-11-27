import { useEffect } from 'react'
import type { ReactNode } from 'react'
import { kindColor, ModelGlyph, ModelTag } from '../../session/Views/SessionExtras'
import { SessionWindow } from '../../session/Views/SessionWindow'
import { useObserved } from '../../styles/Observation/useObserved'
import type { InspectorFace, InspectorSection, Tone } from '../ViewModels/Inspector'
import type { SessionLibraryViewModel } from '../ViewModels/SessionLibraryViewModel'
import { Icon } from '../../styles/Components/Icon'

/** A tone as the old prototype colors it. */
const toneColor: Readonly<Record<Tone, string>> = {
  accent: 'var(--status-accent)',
  warning: 'var(--status-warning)',
  error: 'var(--status-error)',
  tertiary: 'var(--text-tertiary)',
  primary: 'var(--text-primary)',
}

const grid = { display: 'grid', gridTemplateColumns: '84px minmax(0, 1fr)', gap: '6px 10px' } as const
const small = { marginTop: 12, height: 26, fontSize: 'var(--t-small)' } as const

/** One folding section: its heading button, what it says beside its title, and its body while open (`main.html:770-853` at `b375fac9`). */
function Section({ title, open, toggle, aside, children }: { readonly title: string; readonly open: boolean; readonly toggle: () => void; readonly aside?: ReactNode; readonly children: ReactNode }) {
  return (
    <section style={{ borderBottom: '1px solid var(--line-border)' }}>
      <button
        type="button"
        className="hv"
        onClick={toggle}
        aria-expanded={open}
        style={{ width: '100%', display: 'flex', alignItems: 'center', gap: 8, height: 38, padding: '0 16px 0 12px', border: 0, background: 'transparent', color: 'var(--text-tertiary)', textAlign: 'left' }}
      >
        <Icon name="disclosure" size={10} style={{ transform: `rotate(${open ? 0 : -90}deg)`, transition: 'transform var(--motion-chevron)' }} />
        <span className="lbl" style={{ flex: '1 1 auto' }}>
          {title}
        </span>
        {aside}
      </button>
      {open ? <div style={{ padding: '2px 18px 16px' }}>{children}</div> : null}
    </section>
  )
}

/** The recall section's summaries (GUI M15, the CLI's M31): each one's name and the date it was summarised, newest first; the one chosen shows the text a later session is handed. The Mac app's `RecallSummaries`. */
export function RecallSummaries({ summaries, choose }: { readonly summaries: InspectorFace['recall']['summaries']; readonly choose: (chat: string) => void }) {
  if (summaries.length === 0) return null
  return (
    <div style={{ display: 'flex', flexDirection: 'column', gap: 4, marginTop: 10 }}>
      {summaries.map((summary) => (
        <div key={summary.chat}>
          <button
            type="button"
            className="hv"
            onClick={() => choose(summary.chat)}
            aria-expanded={summary.chosen}
            title={summary.chosen ? 'Hide its summary' : 'Show its summary'}
            style={{ width: '100%', display: 'flex', alignItems: 'baseline', gap: 8, padding: '2px 4px', border: 0, borderRadius: 'var(--radius-row)', background: 'transparent', color: 'var(--text-primary)', textAlign: 'left', fontSize: 'var(--t-small)' }}
          >
            <Icon name="disclosure" size={9} style={{ color: 'var(--text-tertiary)', transform: `rotate(${summary.chosen ? 0 : -90}deg)`, transition: 'transform var(--motion-chevron)' }} />
            <span style={{ flex: '1 1 auto', minWidth: 0, overflow: 'hidden', whiteSpace: 'nowrap', textOverflow: 'ellipsis' }}>{summary.title}</span>
            <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
              {summary.updated.slice(0, 10)}
            </span>
          </button>
          {summary.chosen ? <p style={{ margin: '2px 0 6px 21px', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{summary.text}</p> : null}
        </div>
      ))}
    </div>
  )
}

/**
 * The inspector (design 1g): the old prototype's pane (`chat.inspectorOn`,
 * `main.html:762-856` at `b375fac9`), 304 px beside the session, its six
 * sections folding -- the session's saved row with Details… and Compact…,
 * this turn from the replayed events, the context, the model from `models
 * info`, the helpers from `models status`, and what recall holds -- and
 * Expand all. The Mac app's `InspectorPane`, its names. Its reads are made
 * as it appears, as the Mac's `.task { refreshInspector() }`.
 */
export function InspectorPane({ model }: { readonly model: SessionLibraryViewModel }) {
  useObserved(model)
  useObserved(model.current)
  const current = model.current
  useEffect(() => {
    void model.inspectorAppeared()
  }, [model, current])
  const face = model.inspector()
  const toggle = (section: InspectorSection) => () => model.toggleSection(section)
  const chat = model.currentChat
  return (
    <aside className="ap-insp" aria-label="Inspector" style={{ flex: '0 0 304px', minHeight: 0, overflowY: 'auto', background: 'var(--surface-inspector)', borderLeft: '1px solid var(--line-border)', fontSize: 'var(--t-small)', lineHeight: 1.45 }}>
      <div style={{ position: 'sticky', top: 0, zIndex: 1, display: 'flex', alignItems: 'center', gap: 8, height: 38, padding: '0 8px 0 16px', borderBottom: '1px solid var(--line-border)', background: 'var(--surface-inspector)' }}>
        <span className="lbl">INSPECTOR</span>
        <span className="mono" style={{ flex: '1 1 auto', minWidth: 0, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
          {face.turnLabel}
        </span>
        <button type="button" className="mono hv" onClick={() => model.toggleAllSections()} style={{ height: 24, padding: '0 8px', border: 0, borderRadius: 'var(--radius-row)', background: 'transparent', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', whiteSpace: 'nowrap' }}>
          {face.allLabel}
        </button>
      </div>

      <Section title="SESSION" open={face.sections.session} toggle={toggle('session')}>
        <div style={grid}>
          {face.session.lines.map((line) => [
            <span key={`${line.label}-label`} style={{ color: 'var(--text-tertiary)' }}>
              {line.label}
            </span>,
            <span key={`${line.label}-value`} className={line.mono ? 'mono' : undefined} style={{ overflowWrap: 'anywhere' }}>
              {line.value}
            </span>,
          ])}
        </div>
        {face.session.chatActs && chat ? (
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 12 }}>
            <button type="button" className="btn-g" onClick={() => void model.showInfo(chat)} style={{ height: 26, fontSize: 'var(--t-small)' }}>
              Details…
            </button>
            <button type="button" className="btn-g" onClick={() => model.askToCompact(chat)} disabled={!face.session.canCompact} style={{ height: 26, fontSize: 'var(--t-small)' }}>
              Compact…
            </button>
          </div>
        ) : null}
      </Section>

      <Section
        title="THIS TURN"
        open={face.sections.turn}
        toggle={toggle('turn')}
        aside={
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 'var(--t-small)', color: toneColor[face.turn.status.tone] }}>
            <span style={{ width: 6, height: 6, borderRadius: 'var(--radius-dot)', background: toneColor[face.turn.status.tone] }} />
            {face.turn.status.words}
          </span>
        }
      >
        <div className="kv">
          {face.turn.rows.map((row, index) => [
            <span key={`${index}-label`} style={{ color: 'var(--text-tertiary)', fontFamily: row.tool ? 'var(--font-mono)' : undefined, fontSize: row.tool ? 'var(--t-meta)' : undefined }}>
              {row.label}
            </span>,
            <span key={`${index}-value`} style={{ minWidth: 0, overflowWrap: 'anywhere' }}>
              <span style={{ fontFamily: row.tool ? 'var(--font-mono)' : undefined, color: toneColor[row.tone] }}>{row.value}</span>
              {row.sub !== '' ? (
                <>
                  <br />
                  <span style={{ color: 'var(--text-tertiary)' }}>{row.sub}</span>
                </>
              ) : null}
            </span>,
            <span key={`${index}-time`} className="mono" style={{ color: 'var(--text-tertiary)' }}>
              {row.time}
            </span>,
          ])}
        </div>
      </Section>

      <Section
        title="CONTEXT"
        open={face.sections.context}
        toggle={toggle('context')}
        aside={
          <>
            {face.context.known ? (
              <span aria-hidden="true" style={{ display: 'block', width: 44, height: 4, borderRadius: 2, background: 'var(--line-border)', overflow: 'hidden' }}>
                <span style={{ display: 'block', width: `${face.context.percent}%`, height: '100%', background: face.context.warns ? 'var(--status-warning)' : 'var(--status-accent)' }} />
              </span>
            ) : null}
            <span className="mono" style={{ fontSize: 'var(--t-meta)', fontWeight: 600, color: face.context.known ? (face.context.warns ? 'var(--status-warning)' : 'var(--status-accent)') : 'var(--text-tertiary)' }}>
              {face.context.summary}
            </span>
          </>
        }
      >
        <div className="mono" style={{ marginBottom: 10, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
          {face.context.window}
        </div>
        {face.context.known ? (
          <>
            <div role="meter" aria-label={`Context window, ${face.context.percent} percent full`} aria-valuemin={0} aria-valuemax={100} aria-valuenow={face.context.percent} style={{ position: 'relative', height: 8, borderRadius: 4, background: 'var(--line-border)' }}>
              <div style={{ width: `${face.context.percent}%`, height: '100%', borderRadius: 4, background: face.context.warns ? 'var(--status-warning)' : 'var(--status-accent)' }} />
              <span aria-hidden="true" style={{ position: 'absolute', left: '80%', top: -3, width: 1, height: 14, background: 'var(--text-tertiary)' }} />
              <span aria-hidden="true" style={{ position: 'absolute', left: '90%', top: -3, width: 1, height: 14, background: 'var(--text-tertiary)' }} />
            </div>
            <div className="mono" style={{ display: 'flex', justifyContent: 'space-between', marginTop: 8, fontSize: 'var(--t-ui)' }}>
              <span>{face.context.used} tokens used</span>
              <span style={{ color: face.context.warns ? 'var(--status-warning)' : 'var(--text-primary)', fontWeight: 600 }}>{face.context.percent}%</span>
            </div>
            <p style={{ margin: '6px 0 12px', color: 'var(--text-tertiary)' }}>Warns at 80%. At 90%, older turns are summarized and the session notes it.</p>
          </>
        ) : (
          <p style={{ margin: '0 0 12px', color: 'var(--text-tertiary)' }}>{face.context.unknownWords}</p>
        )}
        <div style={grid}>
          {face.context.lines.map((line) => [
            <span key={`${line.label}-label`} style={{ color: 'var(--text-tertiary)' }}>
              {line.label}
            </span>,
            <span key={`${line.label}-value`} style={{ overflowWrap: 'anywhere' }}>
              {line.value}
            </span>,
          ])}
        </div>
        <button type="button" className="btn-g" onClick={() => model.current.compact()} disabled={!face.context.canCompact} style={small}>
          Summarize older turns now
        </button>
      </Section>

      <Section
        title="MODEL"
        open={face.sections.model}
        toggle={toggle('model')}
        aside={
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, minWidth: 0, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
            <span style={{ flex: '0 0 auto', width: 6, height: 6, borderRadius: 'var(--radius-dot)', background: kindColor(face.model.summary.kind) ?? 'var(--text-tertiary)' }} />
            <span style={{ whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{face.model.summary.name}</span>
          </span>
        }
      >
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginBottom: 10 }}>
          <ModelGlyph kind={face.model.summary.kind} size={16} />
          <span style={{ fontSize: 'var(--t-title)', fontWeight: 600 }}>{face.model.summary.name}</span>
          <span style={{ marginLeft: 'auto' }}>
            <ModelTag model={face.model.summary} />
          </span>
        </div>
        <div style={grid}>
          {face.model.facts.map((fact) => [
            <span key={`${fact.label}-label`} style={{ color: 'var(--text-tertiary)' }}>
              {fact.label}
            </span>,
            <span key={`${fact.label}-value`} style={{ overflowWrap: 'anywhere', color: 'var(--text-primary)' }}>
              {fact.value}
            </span>,
          ])}
        </div>
        <button type="button" className="btn-g" onClick={() => model.openModels?.('backends', face.model.summary.name)} style={small}>
          Open in library
        </button>
      </Section>

      <Section
        title="HELPERS"
        open={face.sections.helpers}
        toggle={toggle('helpers')}
        aside={
          <span className="mono" style={{ fontSize: 'var(--t-meta)' }}>
            {face.helpers.summary}
          </span>
        }
      >
        <div style={grid}>
          {face.helpers.rows.map((row) => [
            <span key={`${row.label}-label`} style={{ color: 'var(--text-tertiary)' }}>
              {row.label}
            </span>,
            <span key={`${row.label}-value`} style={{ overflowWrap: 'anywhere', color: row.set ? 'var(--text-primary)' : 'var(--text-tertiary)' }}>
              {row.value}
            </span>,
          ])}
        </div>
        {face.helpers.failure !== '' ? <p style={{ margin: '8px 0 0', fontSize: 'var(--t-meta)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{face.helpers.failure}</p> : null}
        <button type="button" className="btn-g" onClick={() => model.openModels?.('roles')} style={small}>
          Change helpers
        </button>
      </Section>

      <Section title="RECALL" open={face.sections.recall} toggle={toggle('recall')}>
        {face.recall.line !== '' ? (
          <p className="mono" style={{ margin: 0, fontSize: 'var(--t-meta)', color: 'var(--text-primary)', overflowWrap: 'anywhere' }}>
            {face.recall.line}
          </p>
        ) : null}
        <RecallSummaries summaries={face.recall.summaries} choose={(id) => model.selectRecall(id)} />
        {face.recall.failure !== '' ? <p style={{ margin: '8px 0 0', fontSize: 'var(--t-meta)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{face.recall.failure}</p> : null}
        <p style={{ margin: '8px 0 0', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>Each finished chat is summarised so later sessions can recall it. Forgetting removes the summaries; every transcript stays.</p>
        <button type="button" className="btn-g" onClick={() => void model.askToForgetRecall()} disabled={!face.recall.canForget} style={small}>
          Forget every summary…
        </button>
      </Section>
    </aside>
  )
}

/**
 * The Sessions place (design 1g): 1u's session window with the inspector
 * beside it, as the Mac app's `SessionsScreen` holds `SessionWindow` and its
 * `InspectorPane`. A new session is a new view.
 */
export function SessionsScreen({ model }: { readonly model: SessionLibraryViewModel }) {
  useObserved(model)
  return <SessionWindow key={model.generation} model={model.current} inspector={<InspectorPane model={model} />} />
}

/**
 * A saved conversation's details (design 1g): `chats info`'s facts as the
 * sheet lists them, and the command it read -- the old prototype's
 * `chatInfo` sheet (`main.html`'s sheets section at `b375fac9`); its title
 * and buttons are the shell's sheet host's. The Mac app's `ChatInfoSheet`.
 */
export function ChatInfoSheet({ model }: { readonly model: SessionLibraryViewModel }) {
  useObserved(model)
  const info = model.info
  if (!info) return null
  return (
    <>
      {info.failure !== '' ? (
        <p role="alert" style={{ margin: 0, color: 'var(--status-error)', overflowWrap: 'anywhere' }}>
          {info.failure}
        </p>
      ) : null}
      <div style={{ display: 'grid', gridTemplateColumns: 'minmax(84px, auto) minmax(0, 1fr)', gap: '6px 12px' }}>
        {info.fields.map((field) => [
          <span key={`${field.label}-label`} style={{ color: 'var(--text-tertiary)' }}>
            {field.label}
          </span>,
          <span key={`${field.label}-value`} style={{ overflowWrap: 'anywhere' }}>
            {field.value}
          </span>,
        ])}
      </div>
      <p className="mono" style={{ margin: 0, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>
        {info.command}
      </p>
    </>
  )
}
