import { useEffect, useRef } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import type { CodeFace, ProseFace } from '../ViewModels/GraphFaces'
import { GraphLayout } from '../ViewModels/GraphLayout'
import type { GraphViewModel } from '../ViewModels/GraphViewModel'
import { Icon } from '../../styles/Components/Icon'
import { SaidCard } from '../../styles/Components/SaidCard'

const mono = { fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' } as const
const accent = { color: 'var(--status-accent)' } as const
const pane = { flex: '0 0 300px', minHeight: 0, overflowY: 'auto', padding: '18px 20px', background: 'var(--surface-inspector)', borderLeft: '1px solid var(--line-border)' } as const

/**
 * The Code graph place (design 1o, ⌘5): the old prototype's screen
 * (`main.html` at `b375fac9`, `ui.isGraph`) -- its header with the graph's
 * counts, Functions, Files and the collection's view, Rebuild, Find
 * communities and the More menu; the graph gone, said; the code graph's file
 * tree, canvas and the selected node's pane; or the collection's prose graph
 * and its busiest entity -- drawn from the view-model's face.
 */
export function GraphScreen({ model }: { readonly model: GraphViewModel }) {
  useObserved(model)
  const face = model.face()
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex', flexDirection: 'column' }}>
      <header className="ap-tools" style={{ flex: '0 0 52px', display: 'flex', alignItems: 'center', gap: 14, padding: '0 16px 0 24px', borderBottom: '1px solid var(--line-border)' }}>
        <h1 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>Code graph</h1>
        {face.header.line !== '' ? (
          <span className="mono" style={{ ...mono, whiteSpace: 'nowrap' }}>
            {face.header.line}
          </span>
        ) : null}
        <span style={{ flex: '1 1 auto' }} />
        <div role="group" aria-label="Show" className="seg">
          {face.views.map((view) => (
            <button key={view.view} type="button" aria-pressed={view.current} onClick={() => void model.show(view.view)} style={{ background: view.current ? 'var(--surface-control)' : 'transparent', color: 'var(--text-primary)' }}>
              {view.label}
            </button>
          ))}
        </div>
        <button type="button" className="btn-g" onClick={() => model.rebuild()} disabled={!face.rebuild.enabled}>
          Rebuild
        </button>
        {face.findCommunities ? (
          <button type="button" className="btn-g" onClick={() => model.findCommunities()}>
            Find communities
          </button>
        ) : null}
        <div style={{ position: 'relative' }}>
          <button type="button" className="btn-g" onClick={() => model.toggleMore()} disabled={!face.more.enabled} aria-haspopup="menu" aria-expanded={face.more.open}>
            More
            <Icon name="chevronDown" size={11} />
          </button>
          {face.more.open ? (
            <div className="menu" role="menu" aria-label="More" style={{ position: 'absolute', top: 34, right: 0, zIndex: 40, width: 240, padding: 4 }}>
              {face.more.items.map((item, index) =>
                item.rule ? (
                  <div key={`rule-${index}`} role="separator" style={{ height: 1, margin: '4px 6px', background: 'var(--line-border)' }} />
                ) : (
                  <button
                    key={item.label}
                    type="button"
                    role="menuitem"
                    className="hv"
                    onClick={() => model.act(item.act)}
                    style={{ width: '100%', display: 'flex', alignItems: 'center', height: 30, padding: '0 10px', border: 0, borderRadius: 5, background: 'transparent', color: item.destructive ? 'var(--status-error)' : 'var(--text-primary)', fontSize: 'var(--t-dense)', textAlign: 'left' }}
                  >
                    {item.label}
                  </button>
                ),
              )}
            </div>
          ) : null}
        </div>
      </header>
      {/* What the last run on this graph said on stderr, as printed (GUI M22); its toast is the run's last line. */}
      {face.notes.shown ? (
        <div style={{ padding: '4px 16px 0' }}>
          <SaidCard title={face.notes.title} lines={face.notes.lines} dismiss={() => model.dismissNotes()} />
        </div>
      ) : null}
      {face.gone.shown ? (
        <div style={{ flex: '1 1 0', display: 'grid', placeItems: 'center', padding: 32, textAlign: 'center' }}>
          {/* With no apogee the canvas stands empty: the old place has no missing state. */}
          {face.gone.text !== '' ? (
            <div>
              <h2 style={{ margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>Code graph</h2>
              <p style={{ margin: '8px 0 0', maxWidth: 460, color: 'var(--text-secondary)' }}>{face.gone.text}</p>
            </div>
          ) : null}
        </div>
      ) : null}
      {face.code.shown ? <CodeView model={model} code={face.code} /> : null}
      {face.prose.shown ? <ProseView prose={face.prose} /> : null}
    </div>
  )
}

/** The code graph: the file tree, the canvas of cards and edges, and the selected node's pane. */
function CodeView({ model, code }: { readonly model: GraphViewModel; readonly code: CodeFace }) {
  const card = code.card
  const { width, height } = GraphLayout.canvas
  // The canvas scrolls, as the old one does past its frame: the selected card is brought into it, the least that shows it.
  const scroller = useRef<HTMLDivElement>(null)
  const current = code.nodes.find((node) => node.current)
  useEffect(() => {
    const view = scroller.current
    if (!view || !current) return
    const margin = 24
    const right = current.x + margin + GraphLayout.card.width + margin
    const bottom = current.y + margin + GraphLayout.card.height + margin
    if (right > view.scrollLeft + view.clientWidth) view.scrollLeft = right - view.clientWidth
    else if (current.x < view.scrollLeft) view.scrollLeft = current.x
    if (bottom > view.scrollTop + view.clientHeight) view.scrollTop = bottom - view.clientHeight
    else if (current.y < view.scrollTop) view.scrollTop = current.y
  }, [current?.name, current?.x, current?.y])
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
      <nav aria-label="Files" style={{ flex: '0 0 230px', minHeight: 0, overflowY: 'auto', padding: '12px 8px', borderRight: '1px solid var(--line-border)' }}>
        <div className="lbl" style={{ padding: '0 8px 8px' }}>
          FILES
        </div>
        {code.tree.map((row) => (
          <button
            key={`${row.label} ${row.path}`}
            type="button"
            className="hv mono"
            onClick={() => (row.path === '' ? undefined : void model.pickFile(row.path))}
            style={{ width: '100%', display: 'flex', alignItems: 'center', gap: 6, height: 26, padding: `0 8px 0 ${row.indent}px`, border: 0, borderRadius: 5, background: row.current ? 'var(--surface-selection)' : '', color: row.path === '' ? 'var(--text-tertiary)' : row.current ? 'var(--text-primary)' : 'var(--text-secondary)', fontSize: 'var(--t-ui)', textAlign: 'left', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}
          >
            {row.label}
          </button>
        ))}
      </nav>
      <div ref={scroller} style={{ flex: '1 1 0', minWidth: 0, overflow: 'auto', background: 'var(--surface-window)' }}>
        <div style={{ position: 'relative', width, height, margin: 24 }}>
          <svg width={width} height={height} viewBox={`0 0 ${width} ${height}`} aria-hidden="true" style={{ position: 'absolute', inset: 0 }}>
            {code.edges.map((edge, index) => (
              <line key={index} x1={edge.x1} y1={edge.y1} x2={edge.x2} y2={edge.y2} style={{ stroke: edge.hot ? 'var(--status-accent)' : 'var(--line-strong)', strokeWidth: edge.hot ? 1.5 : 1 }} />
            ))}
          </svg>
          {code.nodes.map((node) => (
            <button
              key={node.name}
              type="button"
              onClick={() => void model.select(node.name)}
              aria-pressed={node.current}
              style={{
                position: 'absolute',
                left: node.x,
                top: node.y,
                width: GraphLayout.card.width,
                height: GraphLayout.card.height,
                boxSizing: 'border-box',
                display: 'flex',
                flexDirection: 'column',
                justifyContent: 'center',
                padding: '0 10px',
                border: `1px solid ${node.current ? 'var(--status-accent)' : node.near ? 'var(--line-strong)' : 'var(--line-border)'}`,
                borderRadius: 6,
                background: node.current ? 'var(--surface-selection)' : 'var(--surface-card)',
                color: 'var(--text-primary)',
                textAlign: 'left',
                opacity: node.faded ? 0.6 : 1,
              }}
            >
              <span className="mono" style={{ fontSize: 'var(--t-ui)', whiteSpace: 'nowrap', overflow: 'hidden', textOverflow: 'ellipsis' }}>
                {node.label}
              </span>
              <span className="mono" style={{ fontSize: 'var(--t-label)', color: 'var(--text-tertiary)' }}>
                {node.where}
              </span>
            </button>
          ))}
        </div>
      </div>
      {card.shown ? (
        <aside className="ap-detail" aria-label="Symbol" style={{ ...pane, fontSize: 'var(--t-small)' }}>
          <div className="lbl">{card.kind}</div>
          <h2 className="mono" style={{ margin: '4px 0 2px', fontSize: 'var(--t-title)', fontWeight: 600, overflowWrap: 'anywhere' }}>
            {card.label}
          </h2>
          <div className="mono" style={mono}>
            {card.where}
          </div>
          <div className="mono" style={{ marginTop: 10, padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', overflowWrap: 'anywhere' }}>
            {card.sig}
          </div>
          {(
            [
              [card.inLabel, card.callers, '18px 0 6px'],
              [card.outLabel, card.callees, '16px 0 6px'],
            ] as const
          ).map(([label, neighbours, margin]) => (
            <div key={label}>
              <h3 className="lbl" style={{ margin }}>
                {label} · {neighbours.length}
              </h3>
              {neighbours.map((neighbour) => (
                <button
                  key={neighbour.name}
                  type="button"
                  className="hv mono"
                  onClick={() => void model.select(neighbour.name)}
                  style={{ width: '100%', display: 'block', height: 26, padding: '0 8px', margin: '0 -8px', boxSizing: 'content-box', border: 0, borderRadius: 5, background: 'transparent', ...accent, fontSize: 'var(--t-ui)', textAlign: 'left' }}
                >
                  {neighbour.label}
                </button>
              ))}
              {neighbours.length === 0 ? <div style={{ color: 'var(--text-tertiary)' }}>None in this repo.</div> : null}
            </div>
          ))}
          <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 18 }}>
            <button type="button" className="btn-p" onClick={() => model.askAboutThis()}>
              Ask about this
            </button>
            <button type="button" className="btn-g" onClick={() => model.openInEditor()}>
              Open in editor
            </button>
          </div>
        </aside>
      ) : null}
    </div>
  )
}

/** The collection's prose graph: its report, and its busiest entity explained beside it. */
function ProseView({ prose }: { readonly prose: ProseFace }) {
  const entity = prose.entity
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
      <div style={{ flex: '1 1 0', minWidth: 0, overflowY: 'auto', padding: '22px 28px 32px' }}>
        <div style={{ maxWidth: 720 }}>
          <p style={{ margin: 0, color: 'var(--text-primary)' }}>{prose.summary}</p>
          <h3 className="lbl" style={{ margin: '22px 0 8px' }}>
            COMMUNITIES · {prose.communitiesTotal}
          </h3>
          {prose.communities.map((community) => (
            <div key={community.summary} style={{ padding: '12px 14px', borderRadius: 8, background: 'var(--surface-inspector)' }}>
              <div>{community.summary}</div>
              <div style={{ display: 'flex', gap: 10, marginTop: 6 }}>
                {community.members.map((member) => (
                  <span key={member} style={{ ...accent, fontSize: 'var(--t-small)' }}>
                    {member}
                  </span>
                ))}
              </div>
            </div>
          ))}
          <h3 className="lbl" style={{ margin: '22px 0 8px' }}>
            DECISIONS · {prose.decisionsTotal}
          </h3>
          {prose.decisions.map((decision) => (
            <div key={decision.meta}>
              <div>{decision.text}</div>
              <div className="mono" style={mono}>
                {decision.meta}
              </div>
            </div>
          ))}
          <h3 className="lbl" style={{ margin: '22px 0 8px' }}>
            BUSIEST
          </h3>
          {prose.hubs.map((hub) => (
            <div key={hub.name} style={{ display: 'flex', gap: 10, alignItems: 'baseline', minHeight: 26 }}>
              <span style={accent}>{hub.name}</span>
              <span style={mono}>{hub.meta}</span>
            </div>
          ))}
        </div>
      </div>
      {entity.shown ? (
        <aside className="ap-detail" aria-label="Entity" style={pane}>
          <div className="lbl">{entity.kind}</div>
          <h2 style={{ margin: '4px 0 2px', fontSize: 'var(--t-title)', fontWeight: 600 }}>{entity.name}</h2>
          <div className="mono" style={{ marginTop: 6, fontSize: 'var(--t-meta)' }}>
            {entity.description}
          </div>
          <div style={{ marginTop: 6, fontSize: 'var(--t-meta)', color: 'var(--text-secondary)' }}>{entity.degree}</div>
          <h3 className="lbl" style={{ margin: '16px 0 6px' }}>
            COMMUNITIES
          </h3>
          {entity.communities.map((community) => (
            <div key={community} style={{ fontSize: 'var(--t-small)' }}>
              {community}
            </div>
          ))}
          <h3 className="lbl" style={{ margin: '16px 0 6px' }}>
            DECISIONS
          </h3>
          {entity.decisions.map((decision) => (
            <div key={decision.meta}>
              <div style={{ fontSize: 'var(--t-small)' }}>{decision.text}</div>
              <div className="mono" style={mono}>
                {decision.meta}
              </div>
            </div>
          ))}
          {entity.relations.map((relation) => (
            <div key={relation.heading}>
              <h3 className="lbl" style={{ margin: '16px 0 6px' }}>
                {relation.heading}
              </h3>
              {relation.names.map((name) => (
                <div key={name} style={{ ...accent, fontSize: 'var(--t-small)' }}>
                  {name}
                </div>
              ))}
            </div>
          ))}
        </aside>
      ) : null}
    </div>
  )
}
