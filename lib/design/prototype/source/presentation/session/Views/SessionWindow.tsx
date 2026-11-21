import type { ReactNode } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import type { RowFace, SessionFace, SessionViewModel } from '../ViewModels/SessionViewModel'
import { efforts, thinkModes } from '../ViewModels/SessionViewModel'
import { InputBar } from './InputBar'
import { EmptySessionView, kindColor, MeterView, ModelGlyph, ModelTag } from './SessionExtras'
import { TranscriptRow } from './TranscriptRow'
import type { RowActs } from './TranscriptRow'
import { Icon } from '../../styles/Components/Icon'

const check = (
  <Icon name="check" size={13} />
)
const chevron = (
  <Icon name="chevronDown" size={12} style={{ color: 'var(--text-tertiary)' }} />
)

/** The model menu (`main.html:192-212` at `b375fac9`): the chat backends `models list` names, by kind, each with the CLI's own words, then Models library…. */
export function ModelMenu({ model }: { readonly model: SessionViewModel }) {
  return (
    <div className="menu" role="menu" aria-label="Switch model" style={{ position: 'absolute', top: 38, right: 0, zIndex: 40, width: 340 }}>
      <div style={{ padding: 6, maxHeight: 440, overflowY: 'auto' }}>
        {model.menuGroups.length === 0 ? <p style={{ margin: 0, padding: '8px', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>apogee has not listed its models.</p> : null}
        {model.menuGroups.map((group) => (
          <div key={group.title}>
            <div className="lbl" style={{ padding: '8px 8px 4px' }}>
              {group.title}
            </div>
            {group.items.map((item) => (
              <button
                key={item.name}
                type="button"
                role="menuitemradio"
                aria-checked={item.current}
                onClick={() => model.pickModel(item.name)}
                className="hv"
                style={{ width: '100%', display: 'grid', gridTemplateColumns: '14px 14px minmax(0, 1fr)', columnGap: 8, alignItems: 'center', minHeight: 40, padding: '4px 8px', background: item.current ? 'var(--surface-selection)' : undefined, border: 0, borderRadius: 6, color: 'var(--text-primary)', textAlign: 'left' }}
              >
                <span style={{ width: 14, height: 14, display: 'grid', placeItems: 'center', color: 'var(--status-accent)' }}>{item.current ? check : null}</span>
                <span style={{ width: 14, height: 14, display: 'grid', placeItems: 'center' }}>
                  <ModelGlyph kind={item.kind} size={13} />
                </span>
                <span style={{ display: 'flex', flexDirection: 'column', lineHeight: 1.3, minWidth: 0 }}>
                  <span style={{ fontSize: 'var(--t-body)' }}>{item.name}</span>
                  {item.sub !== '' ? (
                    <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                      {item.sub}
                    </span>
                  ) : null}
                </span>
              </button>
            ))}
          </div>
        ))}
      </div>
      <button
        type="button"
        className="hv"
        onClick={() => model.openLibrary()}
        style={{ width: '100%', display: 'flex', justifyContent: 'space-between', alignItems: 'center', height: 36, padding: '0 14px', border: 0, borderTop: '1px solid var(--line-border)', fontSize: 'var(--t-dense)', color: 'var(--text-secondary)' }}
      >
        <span>Models library…</span>
        <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
          ⇧⌘L
        </span>
      </button>
    </div>
  )
}

/** The thinking menu (`main.html:217-236` at `b375fac9`): the three modes, the effort row, and when a change takes. */
function ThinkMenu({ model }: { readonly model: SessionViewModel }) {
  const think = model.settings.think
  const effort = model.settings.effort === undefined || model.settings.effort === 'auto' ? 'default' : model.settings.effort
  return (
    <div className="menu" role="menu" aria-label="Thinking" style={{ position: 'absolute', top: 38, right: 0, zIndex: 40, width: 380 }}>
      <div className="lbl" style={{ padding: '12px 14px 4px' }}>
        THINKING
      </div>
      <div style={{ padding: '0 6px' }}>
        {thinkModes.map((mode) => (
          <button
            key={mode.mode}
            type="button"
            role="menuitemradio"
            aria-checked={think === mode.mode}
            onClick={() => model.pickThink(mode.mode)}
            className="hv"
            style={{ width: '100%', display: 'grid', gridTemplateColumns: '14px minmax(0, 1fr)', columnGap: 8, alignItems: 'start', padding: '7px 8px', border: 0, borderRadius: 6, background: think === mode.mode ? 'var(--surface-selection)' : undefined, color: 'var(--text-primary)', textAlign: 'left' }}
          >
            <span style={{ width: 14, height: 18, display: 'grid', placeItems: 'center', color: 'var(--status-accent)' }}>{think === mode.mode ? check : null}</span>
            <span>
              <span style={{ display: 'block', fontSize: 'var(--t-body)' }}>{mode.label}</span>
              <span style={{ display: 'block', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{mode.description}</span>
            </span>
          </button>
        ))}
      </div>
      <div className="lbl" style={{ marginTop: 6, padding: '10px 14px 6px', borderTop: '1px solid var(--line-border)' }}>
        EFFORT · HOW HARD A REASONING MODEL THINKS
      </div>
      <div style={{ display: 'flex', gap: 4, padding: '0 14px 8px' }}>
        {efforts.map((level) => (
          <button
            key={level}
            type="button"
            aria-pressed={effort === level}
            onClick={() => model.pickEffort(level)}
            className="mono"
            style={{ flex: '1 1 0', height: 28, padding: 0, border: `1px solid ${effort === level ? 'var(--status-accent)' : 'var(--line-strong)'}`, borderRadius: 6, background: effort === level ? 'var(--surface-selection)' : undefined, color: 'var(--text-primary)', fontSize: 'var(--t-meta)' }}
          >
            {level}
          </button>
        ))}
      </div>
      <p style={{ margin: 0, padding: '0 14px 14px', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{model.settingsFooter}</p>
    </div>
  )
}

/** The header (`main.html:175-246` at `b375fac9`): private, the title, the execute banner or the suite, the model, thinking, the context meter and the inspector button. */
function Header({ model, face }: { readonly model: SessionViewModel; readonly face: SessionFace['header'] }) {
  return (
    <header
      className="ap-tools"
      style={{ position: 'relative', zIndex: 30, flex: '0 0 var(--layout-header)', display: 'flex', alignItems: 'center', gap: 12, padding: '0 14px 0 24px', borderBottom: '1px solid var(--line-border)', background: 'var(--surface-window)' }}
    >
      <div style={{ flex: '1 1 auto', minWidth: 0, display: 'flex', alignItems: 'center', gap: 8 }}>
        {face.privateBadge ? (
          <span className="mono" style={{ display: 'inline-flex', alignItems: 'center', gap: 5, height: 20, padding: '0 7px', border: '1px solid var(--line-strong)', borderRadius: 4, fontSize: 'var(--t-tag)', fontWeight: 600, letterSpacing: '.08em', color: 'var(--text-secondary)' }}>
            <Icon name="private" size={11} />
            PRIVATE
          </span>
        ) : null}
        <h1 style={{ margin: 0, minWidth: 0, fontSize: 'var(--t-title)', fontWeight: 600, whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>{face.title}</h1>
        {face.banner !== '' ? (
          <span className="mono" title="Opened as apogee execute: its suite’s symphonies play with /play" style={{ flex: '0 1 auto', minWidth: 0, fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
            {face.banner}
          </span>
        ) : null}
        {face.suiteWords !== '' ? (
          <button
            type="button"
            className="mono hv"
            onClick={() => model.chooseSuite()}
            disabled={!face.canSwitchSuite}
            title="The suite this session runs under -- /suite, or click, to run it under another, or none"
            style={{ flex: '0 1 auto', minWidth: 0, height: 24, padding: '0 6px', border: 0, borderRadius: 5, fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}
          >
            {face.suiteWords}
          </button>
        ) : null}
      </div>

      <div style={{ position: 'relative' }}>
        <button
          type="button"
          onClick={() => model.toggleMenu('model')}
          aria-haspopup="menu"
          aria-expanded={model.menu === 'model'}
          aria-label={`Model: ${face.model.name}${face.model.tag === '' ? '' : `, ${face.model.tag}`}. Switch model`}
          title="Switch model ⇧⌘M"
          style={{ display: 'flex', alignItems: 'center', gap: 8, height: 32, padding: '0 8px 0 10px', background: 'var(--surface-card)', border: '1px solid var(--line-strong)', borderRadius: 7, color: 'var(--text-primary)', fontSize: 'var(--t-body)' }}
        >
          <ModelGlyph kind={face.model.kind} />
          <span style={{ fontWeight: 600, whiteSpace: 'nowrap' }}>{face.model.name}</span>
          <ModelTag model={face.model} />
          {chevron}
        </button>
        {model.menu === 'model' ? <ModelMenu model={model} /> : null}
      </div>

      <div style={{ position: 'relative' }}>
        <button
          type="button"
          onClick={() => model.toggleMenu('think')}
          aria-haspopup="menu"
          aria-expanded={model.menu === 'think'}
          aria-label={`Thinking: ${face.think}`}
          style={{ display: 'flex', alignItems: 'center', gap: 7, height: 32, padding: '0 8px 0 10px', border: '1px solid var(--line-strong)', borderRadius: 7, color: 'var(--text-primary)', fontSize: 'var(--t-body)' }}
        >
          <span>Thinking</span>
          <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)', whiteSpace: 'nowrap' }}>
            {face.think}
          </span>
          {chevron}
        </button>
        {model.menu === 'think' ? <ThinkMenu model={model} /> : null}
      </div>

      <MeterView meter={face.meter} open={() => model.showContext()} />
      <span aria-hidden="true" style={{ width: 1, height: 20, background: 'var(--line-border)' }} />
      <button
        type="button"
        className="ib"
        onClick={() => model.toggleInspector()}
        aria-pressed={face.inspectorOpen}
        aria-label="Inspector (Option Command I)"
        title="Inspector ⌥⌘I"
        style={{ background: face.inspectorOpen ? 'var(--surface-control)' : undefined, color: 'var(--text-primary)' }}
      >
        <Icon name="inspector" size={16} />
      </button>
    </header>
  )
}

/** What a turn's reply is made of: drawn together in one column, as the old prototype draws a reply. */
const replyKinds: ReadonlySet<RowFace['kind']> = new Set(['activity', 'answer', 'sources', 'recalled', 'question', 'permission', 'error'])

/**
 * The rows, a turn's reply -- its recall, activity, cards, answer and sources
 * -- drawn together, as the old prototype draws a turn's reply in one column.
 * A recall heads its reply, as the old frame draws it above the reasoning,
 * though the CLI says it after the turn's query rewrite and retrieval.
 */
function Conversation({ rows, pending, acts }: { readonly rows: readonly RowFace[]; readonly pending: ReactNode; readonly acts: RowActs }) {
  const groups: RowFace[][] = []
  for (const row of rows) {
    const last = groups.at(-1)
    const together = (kind: RowFace['kind']) => replyKinds.has(kind)
    if (last && together(row.kind) && together(last.at(-1)?.kind ?? 'user')) last.push(row)
    else groups.push([row])
  }
  for (const [index, group] of groups.entries()) groups[index] = [...group.filter((row) => row.kind === 'recalled'), ...group.filter((row) => row.kind !== 'recalled')]
  return (
    <section className="ap-scroll" aria-label="Conversation" style={{ flex: '1 1 0', minHeight: 0, overflowY: 'auto', display: 'flex', flexDirection: 'column-reverse', padding: '24px 28px 20px' }}>
      <div style={{ width: '100%', maxWidth: 860, margin: '0 auto', display: 'flex', flexDirection: 'column', gap: 24 }}>
        {groups.map((group) =>
          group.length === 1 && !replyKinds.has(group[0]?.kind ?? 'user') ? (
            <TranscriptRow key={group[0]?.id} row={group[0] as RowFace} acts={acts} />
          ) : (
            <div key={group[0]?.id} className="row">
              <div style={{ minWidth: 0, display: 'flex', flexDirection: 'column', gap: 10 }}>
                {group.map((row) => (
                  <TranscriptRow key={row.id} row={row} acts={acts} />
                ))}
              </div>
            </div>
          ),
        )}
        {pending}
      </div>
    </section>
  )
}

/** The turn sent, nothing of its answer yet (`main.html:682-688` at `b375fac9`): what it is doing, how long, and the model. */
function PendingLine({ pending }: { readonly pending: SessionFace['pending'] }) {
  const dot = kindColor(pending.model.kind)
  return (
    <div className="row">
      <div style={{ border: '1px solid var(--line-border)', borderRadius: 6, background: 'var(--surface-card)' }}>
        <div className="mono" style={{ display: 'flex', alignItems: 'center', gap: 8, height: 32, padding: '0 12px', fontSize: 'var(--t-ui)', color: 'var(--text-secondary)' }}>
          <span className="pulse" aria-hidden="true" style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--status-accent)' }} />
          <span style={{ color: 'var(--text-primary)', fontWeight: 500 }}>{pending.word}</span>
          <span>{pending.seconds}s</span>
          <span style={{ flex: '1 1 auto' }} />
          <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, color: 'var(--text-tertiary)' }}>
            {dot ? <span style={{ width: 6, height: 6, borderRadius: '50%', background: dot }} /> : null}
            {pending.model.name}
          </span>
        </div>
      </div>
    </div>
  )
}

/** No binary where the app looks (the Mac app's `MissingBinaryView`): how to install it. */
function MissingBinary() {
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex', flexDirection: 'column', alignItems: 'center', justifyContent: 'center', gap: 14, padding: 32, textAlign: 'center' }}>
      <h2 style={{ margin: 0, fontSize: 'var(--t-display)', lineHeight: 1.25, fontWeight: 600 }}>Apogee isn’t installed</h2>
      <p style={{ margin: 0, maxWidth: 460, fontSize: 'var(--t-prose)', color: 'var(--text-secondary)' }}>The app drives the apogee command, and found none where it looks.</p>
      <p className="mono" style={{ margin: 0, maxWidth: 620, padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
        Install Apogee with: curl -fsSL https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.sh | bash
      </p>
    </div>
  )
}

/**
 * One chat session (design 1u): the old prototype's chat screen
 * (`main.html:174-858` at `b375fac9`) -- the header, then the conversation
 * (or why there is none yet) with its pending line and the composer, and
 * beside them, while it is open, the inspector its owner hands in (the
 * Sessions place's, design 1g: the Mac's `SessionWindow` takes its
 * inspector button the same way). The Mac app's `SessionWindow`, its names.
 * Everything drawn is the view-model's `face()`; every act is a method of it.
 */
export function SessionWindow({ model, inspector }: { readonly model: SessionViewModel; readonly inspector?: ReactNode }) {
  useObserved(model)
  const face = model.face()
  const acts: RowActs = {
    answer: (id, answers) => model.answer(id, answers),
    skip: (id) => model.skip(id),
    openCitation: (row, index) => model.openCitation(row, index),
    closeCitation: () => model.closeCitation(),
    openSource: () => model.openSource(),
    resend: (text) => model.resend(text),
    startAgain: () => void model.start(),
    retry: () => model.retry(),
    askInstead: (name) => model.askInstead(name),
    openChat: (id) => model.leave({ type: 'open', id }),
  }
  return (
    <>
      <Header model={model} face={face.header} />
      <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
        <div style={{ flex: '1 1 0', minWidth: 0, minHeight: 0, display: 'flex', flexDirection: 'column' }}>
          {face.state === 'missing' ? (
            <MissingBinary />
          ) : face.state === 'empty' ? (
            <EmptySessionView
              face={face.empty}
              model={face.header.model}
              acts={{
                attachFolder: () => {
                  const folder = model.attachChoices.find((choice) => choice.path.endsWith('/'))
                  if (folder) model.attach(folder.path)
                },
                chooseModel: () => model.toggleMenu('model'),
                chooseSuite: () => model.chooseSuite(),
                privateInstead: () => model.leave({ type: 'private' }),
              }}
            />
          ) : face.state === 'starting' ? (
            <div style={{ flex: '1 1 0', display: 'grid', placeItems: 'center', color: 'var(--text-tertiary)', fontSize: 'var(--t-small)' }}>Starting apogee…</div>
          ) : (
            <Conversation rows={face.rows} pending={face.pending.shown ? <PendingLine pending={face.pending} /> : null} acts={acts} />
          )}
          <InputBar model={model} />
        </div>
        {face.header.inspectorOpen ? inspector : null}
      </div>
    </>
  )
}
