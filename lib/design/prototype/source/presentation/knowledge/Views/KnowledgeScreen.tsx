import { useObserved } from '../../styles/Observation/useObserved'
import type { CollectionsViewModel } from '../ViewModels/CollectionsViewModel'
import type { CollectionPageFace, KnowledgeTab, RecordFace } from '../ViewModels/KnowledgeFaces'
import type { RecordsViewModel } from '../ViewModels/RecordsViewModel'

export interface KnowledgeScreenProps {
  readonly records: RecordsViewModel
  readonly collections: CollectionsViewModel
  /** The tab shown, the route's: its owner keeps it, as the Mac's screen keeps its `@State tab`. */
  readonly tab: KnowledgeTab
  readonly pickTab: (tab: KnowledgeTab) => void
}

const meta = { fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' } as const
const code = { padding: '10px 12px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6 } as const

/**
 * The Knowledge place (design 1o, ⌘8): the old prototype's screen (`main.html`
 * at `b375fac9`, `ui.isKnowledge`) -- its header with Records and
 * Collections, Capture… and Export… or Ingest…; the Records tab (the
 * question, the branch, the list, the record's pane with its raw
 * conversation) and the Collections tab (the list, a collection's page with
 * its search and sources) -- drawn from the two view-models' faces.
 */
export function KnowledgeScreen({ records, collections, tab, pickTab }: KnowledgeScreenProps) {
  useObserved(records)
  useObserved(collections)
  const onRecords = tab === 'records'
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex', flexDirection: 'column' }}>
      <header className="ap-tools" style={{ flex: '0 0 52px', display: 'flex', alignItems: 'center', gap: 14, padding: '0 16px 0 24px', borderBottom: '1px solid var(--line-border)' }}>
        <h1 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>Knowledge</h1>
        <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Decision records and the collections they live in</span>
        <span style={{ flex: '1 1 auto' }} />
        <div role="group" aria-label="Show" className="seg">
          {(['records', 'collections'] as const).map((each) => (
            <button key={each} type="button" aria-pressed={tab === each} onClick={() => pickTab(each)} style={{ background: tab === each ? 'var(--surface-control)' : 'transparent', color: 'var(--text-primary)' }}>
              {each === 'records' ? 'Records' : 'Collections'}
            </button>
          ))}
        </div>
        {onRecords ? (
          <>
            <button type="button" className="btn-g" onClick={() => records.capture()}>
              Capture…
            </button>
            <button type="button" className="btn-g" onClick={() => records.exportRecords()}>
              Export…
            </button>
          </>
        ) : (
          <button type="button" className="btn-g" onClick={() => collections.ingest()}>
            Ingest…
          </button>
        )}
      </header>
      {onRecords ? <RecordsTab model={records} /> : <CollectionsTab model={collections} />}
    </div>
  )
}

/** Records: the question and the branch over the list, and the record's pane. */
function RecordsTab({ model }: { readonly model: RecordsViewModel }) {
  const face = model.face()
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
      <div style={{ flex: '1 1 0', minWidth: 0, display: 'flex', flexDirection: 'column' }}>
        <div style={{ display: 'flex', gap: 8, padding: '12px 16px' }}>
          <input
            type="text"
            className="inp"
            value={face.question}
            onChange={(event) => model.setQuestion(event.target.value)}
            onKeyDown={(event) => {
              if (event.key === 'Enter') void model.ask()
            }}
            aria-label="Ask the records"
            placeholder="Ask the records"
            style={{ flex: '1 1 auto', minWidth: 0 }}
          />
          <button type="button" className="btn-g" onClick={() => void model.ask()}>
            Ask
          </button>
          <select className="inp" aria-label="Branch" value={face.branch} onChange={(event) => model.setBranch(event.target.value)} style={{ flex: '0 0 auto' }}>
            {model.branches.map(([value, label]) => (
              <option key={value} value={value}>
                {label}
              </option>
            ))}
          </select>
        </div>
        {face.found !== '' ? <div style={{ padding: '0 18px 6px', ...meta }}>{face.found}</div> : null}
        {face.failure !== '' ? <div style={{ padding: '0 18px 6px', fontSize: 'var(--t-meta)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{face.failure}</div> : null}
        <div style={{ flex: '1 1 0', minHeight: 0, overflowY: 'auto', padding: '0 10px 12px' }}>
          {face.rows.map((row) => (
            <button
              key={row.id}
              type="button"
              className="hv"
              onClick={() => model.select(row.id)}
              aria-current={row.current}
              style={{ width: '100%', display: 'grid', gridTemplateColumns: 'minmax(0, 1fr) auto', gap: '3px 12px', padding: '10px 12px', marginBottom: 2, border: 0, borderRadius: 7, background: row.current ? 'var(--surface-selection)' : '', textAlign: 'left', color: 'var(--text-primary)' }}
            >
              <span style={{ fontSize: 'var(--t-body)' }}>{row.decision}</span>
              <span className="mono" style={meta}>
                {row.score}
              </span>
              <span style={meta}>{row.meta}</span>
            </button>
          ))}
          {face.empty !== '' ? <p style={{ margin: '6px 12px', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{face.empty}</p> : null}
        </div>
      </div>
      <aside className="ap-detail" aria-label="Record" style={{ flex: '0 0 380px', minHeight: 0, overflowY: 'auto', padding: '18px 20px', background: 'var(--surface-inspector)', borderLeft: '1px solid var(--line-border)' }}>
        {face.record.shown ? <RecordPane model={model} record={face.record} /> : <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>{face.noSelection}</p>}
      </aside>
    </div>
  )
}

/** A record's pane: its decision, id and facts, its raw conversation, and its changes. */
function RecordPane({ model, record }: { readonly model: RecordsViewModel; readonly record: RecordFace }) {
  const raw = record.raw
  return (
    <>
      <h2 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>{record.decision}</h2>
      <div className="mono" style={{ marginTop: 2, ...meta }}>
        {record.id}
      </div>
      {record.facts.map((fact) => (
        <div key={fact.label}>
          <h3 className="lbl" style={{ margin: '16px 0 4px' }}>
            {fact.label}
          </h3>
          <div style={{ color: 'var(--text-primary)' }}>{fact.value}</div>
        </div>
      ))}
      {raw.shown ? (
        <>
          <div style={{ display: 'flex', alignItems: 'center', gap: 8, margin: '16px 0 6px' }}>
            <h3 className="lbl" style={{ flex: '1 1 auto', margin: 0 }}>
              RAW CONVERSATION
            </h3>
            {raw.open ? (
              <button type="button" className="btn-g" onClick={() => model.hideRaw()} style={{ height: 24, fontSize: 'var(--t-small)' }}>
                Hide
              </button>
            ) : null}
          </div>
          {raw.open ? null : (
            <button type="button" className="btn-g" onClick={() => void model.showRaw()} title={raw.command} style={{ height: 26, fontSize: 'var(--t-small)' }}>
              Show the conversation it was distilled from
            </button>
          )}
          {raw.text !== '' ? (
            <div className="mono" style={{ ...code, color: 'var(--text-primary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
              {raw.text}
            </div>
          ) : null}
          {raw.note !== '' ? <div style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{raw.note}</div> : null}
          {raw.failure !== '' ? <div style={{ fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{raw.failure}</div> : null}
        </>
      ) : null}
      <div style={{ display: 'flex', gap: 8, marginTop: 18 }}>
        <input type="text" className="inp" aria-label="Link it to a ticket, a commit, a file" placeholder="Link it to a ticket, a commit, a file" style={{ flex: '1 1 auto', minWidth: 0 }} />
        <button type="button" className="btn-g" onClick={() => model.link()}>
          Link
        </button>
      </div>
      <div style={{ display: 'flex', gap: 8, marginTop: 10 }}>
        <button type="button" className="btn-g" onClick={() => model.move()}>
          Move to…
        </button>
        <span style={{ flex: '1 1 auto' }} />
        <button type="button" className="btn-g" onClick={() => model.remove()} style={{ color: 'var(--status-error)' }}>
          Delete…
        </button>
      </div>
    </>
  )
}

/** Collections: the store's rows, and a collection's page. */
function CollectionsTab({ model }: { readonly model: CollectionsViewModel }) {
  const face = model.face()
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
      <nav aria-label="Collections" style={{ flex: '0 0 280px', minHeight: 0, overflowY: 'auto', padding: '12px 10px', background: 'var(--surface-sidebar)', borderRight: '1px solid var(--line-border)' }}>
        {face.rows.map((row) => (
          <button
            key={row.name}
            type="button"
            className="hv"
            onClick={() => void model.select(row.name)}
            aria-current={row.current}
            style={{ width: '100%', display: 'flex', flexDirection: 'column', gap: 3, padding: '10px 12px', marginBottom: 4, border: 0, borderRadius: 7, background: row.current ? 'var(--surface-selection)' : '', textAlign: 'left', color: 'var(--text-primary)' }}
          >
            <span style={{ fontSize: 'var(--t-body)' }}>{row.name}</span>
            <span style={meta}>{row.meta}</span>
          </button>
        ))}
        {face.note !== '' ? <p style={{ margin: '6px 12px', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{face.note}</p> : null}
      </nav>
      <div style={{ flex: '1 1 0', minWidth: 0, overflowY: 'auto', padding: '22px 28px 32px' }}>{face.page.shown ? <CollectionPane model={model} page={face.page} /> : null}</div>
    </div>
  )
}

/** A collection's page: its health, its Vectors line, its search and its sources. */
function CollectionPane({ model, page }: { readonly model: CollectionsViewModel; readonly page: CollectionPageFace }) {
  return (
    <div style={{ maxWidth: 880 }}>
      <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
        <h2 style={{ flex: '1 1 auto', margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>{page.name}</h2>
        <button type="button" className="btn-g" onClick={() => model.ingest()}>
          Ingest more…
        </button>
        <button type="button" className="btn-g" onClick={() => model.deleteCollection()} style={{ color: 'var(--status-error)' }}>
          Delete…
        </button>
      </div>
      <div style={{ display: 'flex', gap: 32, marginTop: 14 }}>
        <div style={{ flex: '0 0 auto' }}>
          <div className="lbl">CHUNKS</div>
          <div>{page.chunks}</div>
        </div>
        <div style={{ flex: '0 0 auto' }}>
          <div className="lbl">TEXT INDEX</div>
          <div>{page.index}</div>
        </div>
        <div style={{ flex: '1 1 auto', minWidth: 0 }}>
          <div className="lbl">VECTORS</div>
          <div style={{ overflowWrap: 'anywhere' }}>{page.vectors}</div>
        </div>
      </div>
      <div style={{ display: 'flex', gap: 8, marginTop: 18 }}>
        <input
          type="text"
          className="inp"
          value={page.question}
          onChange={(event) => model.setQuestion(event.target.value)}
          onKeyDown={(event) => {
            if (event.key === 'Enter') void model.search()
          }}
          aria-label={`Search ${page.name}`}
          placeholder={`Search ${page.name}`}
          style={{ flex: '1 1 auto', minWidth: 0 }}
        />
        <button type="button" className="btn-g" onClick={() => void model.search()}>
          Search
        </button>
      </div>
      {page.searched ? (
        <div className="mono" style={{ ...code, marginTop: 10, whiteSpace: 'pre', overflowX: 'auto' }}>
          {page.results.map((line, index) => (
            <div key={index}>{line}</div>
          ))}
        </div>
      ) : null}
      {page.failure !== '' ? <p style={{ margin: '10px 0 0', fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{page.failure}</p> : null}
      <h3 className="lbl" style={{ margin: '22px 0 6px' }}>
        SOURCES · {page.sourceCount}
      </h3>
      {page.sources.map((source) => (
        <div key={source} style={{ display: 'flex', alignItems: 'center', minHeight: 28, borderBottom: '1px solid var(--line-border)' }}>
          <span style={{ flex: '1 1 auto' }}>{source}</span>
          <button type="button" className="btn-g" onClick={() => model.removeSource(source)} style={{ height: 24, fontSize: 'var(--t-small)' }}>
            Remove…
          </button>
        </div>
      ))}
    </div>
  )
}
