import { PickColumn, PickRow } from '../../styles/Components/PickList'
import { useObserved } from '../../styles/Observation/useObserved'
import type { AgentPageFace, MCPPageFace, SymphonyPageFace } from '../ViewModels/LibraryFaces'
import type { LibraryViewModel } from '../ViewModels/LibraryViewModel'

const heading = { flex: '1 1 auto', margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 } as const
const label = { margin: '20px 0 6px' } as const
const small = { height: 24, fontSize: 'var(--t-small)' } as const

/**
 * The Library place (design 1n): the old prototype's screen (`main.html` at
 * `b375fac9`, `ui.isLibrary`) -- its header with the three tabs and New
 * agent…, the 300 px list of definitions with what it says under its rows,
 * and the page: the agent's, a symphony's with Play, or the server's with
 * the presets Apogee ships -- drawn from the place's view-model.
 */
export function LibraryScreen({ model }: { readonly model: LibraryViewModel }) {
  useObserved(model)
  const face = model.face()
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex', flexDirection: 'column' }}>
      <header className="ap-tools" style={{ flex: '0 0 52px', display: 'flex', alignItems: 'center', gap: 14, padding: '0 16px 0 24px', borderBottom: '1px solid var(--line-border)' }}>
        <h1 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>Library</h1>
        <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>The agents, symphonies and MCP servers runs draw on</span>
        <span style={{ flex: '1 1 auto' }} />
        <div role="group" aria-label="Show" className="seg">
          {face.tabs.map((tab) => (
            <button key={tab.tab} type="button" aria-pressed={tab.current} onClick={() => model.pick(tab.tab)} style={{ background: tab.current ? 'var(--surface-control)' : 'transparent', color: 'var(--text-primary)' }}>
              {tab.label}
            </button>
          ))}
        </div>
        {face.newAgent ? (
          <button type="button" className="btn-g" onClick={() => model.agents.newAgent()}>
            New agent…
          </button>
        ) : null}
      </header>
      <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
        <PickColumn label="Definitions">
          {face.rows.map((row) => (
            <PickRow key={row.name} name={row.name} meta={row.meta} current={row.current} pick={() => model.pickRow(row.name)} />
          ))}
          {face.note !== '' ? <p style={{ margin: '8px 12px 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>{face.note}</p> : null}
        </PickColumn>
        <div style={{ flex: '1 1 0', minWidth: 0, overflowY: 'auto', padding: '22px 28px 32px' }}>
          <div style={{ maxWidth: 820 }}>
            {face.emptyDetail !== '' ? <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>{face.emptyDetail}</p> : null}
            {face.agent.shown ? <AgentPage model={model} agent={face.agent} /> : null}
            {face.symphony.shown ? <SymphonyPage model={model} symphony={face.symphony} /> : null}
            {face.tabs[2]?.current ? <ServerPage model={model} server={face.mcp} /> : null}
          </div>
        </div>
      </div>
    </div>
  )
}

/** The agent's page: its name and Delete…, what it does, its model and tools, its files with Edit, and the command that runs it. */
function AgentPage({ model, agent }: { readonly model: LibraryViewModel; readonly agent: AgentPageFace }) {
  return (
    <>
      <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
        <h2 style={heading}>{agent.name}</h2>
        {agent.canDelete ? (
          <button type="button" className="btn-g" onClick={() => model.agents.askToDelete()} style={{ color: 'var(--status-error)' }}>
            Delete…
          </button>
        ) : null}
      </div>
      <p style={{ margin: '8px 0 0' }}>{agent.description}</p>
      <p style={{ margin: '6px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{agent.facts}</p>
      <h3 className="lbl" style={label}>
        FILES
      </h3>
      {agent.files.map((file) => (
        <div key={file} style={{ display: 'flex', alignItems: 'center', minHeight: 28 }}>
          <span className="mono" style={{ flex: '1 1 auto' }}>
            {file}
          </span>
          <button type="button" className="btn-g" onClick={() => model.agents.edit(file)} aria-label={`Edit ${file}`} style={small}>
            Edit
          </button>
        </div>
      ))}
      <p className="mono" style={{ margin: '14px 0 0', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
        {agent.runs}
      </p>
    </>
  )
}

/** A symphony's page: its stages, where it comes from, Play with its input and its image where it takes one, the play running, and the last play's answers. */
function SymphonyPage({ model, symphony }: { readonly model: LibraryViewModel; readonly symphony: SymphonyPageFace }) {
  const symphonies = model.symphonies
  return (
    <>
      <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
        <h2 style={heading}>{symphony.name}</h2>
        {symphony.canEdit ? (
          <button type="button" className="btn-g" onClick={() => symphonies.edit()}>
            Edit…
          </button>
        ) : null}
      </div>
      <p style={{ margin: '8px 0 0' }}>{symphony.description}</p>
      {symphony.stages.map((stage) => (
        <div key={stage} style={{ marginTop: 6, fontWeight: 500, fontSize: 'var(--t-small)' }}>
          {stage}
        </div>
      ))}
      {symphony.note !== '' ? <p style={{ margin: '12px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{symphony.note}</p> : null}
      <h3 className="lbl" style={label}>
        PLAY
      </h3>
      <p style={{ margin: '0 0 6px', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{symphony.inputDescription}</p>
      <input type="text" className="inp" value={symphony.input} onChange={(event) => symphonies.setInput(event.target.value)} aria-label="Its input" style={{ width: '100%' }} />
      {symphony.image.takes ? (
        <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 10, marginTop: 8 }}>
          {symphony.image.chosen !== '' ? (
            <>
              <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 'var(--t-small)' }}>
                <svg className="i" width="14" height="14" viewBox="0 0 16 16" aria-hidden="true">
                  <rect x="2.5" y="3.5" width="11" height="9" rx="1.5" />
                  <path d="M4.5 10.5l2.5-3 2 2 1.5-1.5 1.5 2.5" />
                </svg>
                {symphony.image.chosen}
              </span>
              <button type="button" className="btn-g" onClick={() => symphonies.removeImage()} style={small}>
                Remove
              </button>
              <button type="button" className="btn-g" onClick={() => symphonies.chooseImage()} style={small}>
                Choose another…
              </button>
            </>
          ) : (
            <>
              <button type="button" className="btn-g" onClick={() => symphonies.chooseImage()}>
                Choose an image…
              </button>
              <span style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>{symphony.image.types}</span>
            </>
          )}
        </div>
      ) : null}
      <div style={{ marginTop: 8 }}>
        <button type="button" className="btn-g" onClick={() => symphonies.play()} disabled={!symphony.canPlay}>
          Play
        </button>
      </div>
      {symphony.needsImage !== '' ? <p style={{ margin: '8px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{symphony.needsImage}</p> : null}
      {symphony.playing.shown ? (
        <div style={{ display: 'flex', alignItems: 'center', gap: 8, marginTop: 12, color: 'var(--text-secondary)' }}>
          <span className="pulse" aria-hidden="true" style={{ width: 6, height: 6, borderRadius: '50%', background: 'var(--status-accent)' }} />
          <span className="mono" style={{ fontSize: 'var(--t-meta)', overflowWrap: 'anywhere' }}>
            {symphony.playing.line}
          </span>
        </div>
      ) : null}
      {symphony.played.shown ? (
        <>
          {symphony.played.stages.map((stage, index) => (
            <div key={stage.label}>
              <div className="mono" style={{ marginTop: index === 0 ? 14 : 10, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                {stage.label}
              </div>
              <div style={{ fontSize: 'var(--t-small)' }}>{stage.answer}</div>
            </div>
          ))}
          <h3 className="lbl" style={{ margin: '16px 0 4px' }}>
            OUTPUT
          </h3>
          <div>{symphony.played.output}</div>
        </>
      ) : null}
    </>
  )
}

/** The server's page -- its command, its state and tools, Disable and Remove… -- then the presets Apogee ships, each in its own words. */
function ServerPage({ model, server }: { readonly model: LibraryViewModel; readonly server: MCPPageFace }) {
  return (
    <>
      {server.shown ? (
        <>
          <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
            <h2 style={heading}>{server.name}</h2>
            <button type="button" className="btn-g" onClick={() => model.mcp.toggle()}>
              {server.toggle}
            </button>
            <button type="button" className="btn-g" onClick={() => model.mcp.askToRemove()} style={{ color: 'var(--status-error)' }}>
              Remove…
            </button>
          </div>
          <div className="mono" style={{ marginTop: 8, fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
            {server.command}
          </div>
          <p style={{ margin: '6px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{server.facts}</p>
        </>
      ) : null}
      <h3 className="lbl" style={label}>
        PRESETS APOGEE SHIPS
      </h3>
      {server.presetsNote !== '' ? <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>{server.presetsNote}</p> : null}
      {server.presets.map((preset, index) => (
        <div key={preset.name}>
          <div style={{ marginTop: index === 0 ? 0 : 12, fontWeight: 500 }}>{preset.name}</div>
          <div style={{ fontSize: 'var(--t-small)', color: preset.refused ? 'var(--text-tertiary)' : 'var(--text-secondary)' }}>{preset.text}</div>
        </div>
      ))}
    </>
  )
}
