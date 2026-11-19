import { useState } from 'react'
import type { KeyboardEvent } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import type { ConsoleLineKind, ConsoleViewModel } from '../ViewModels/ConsoleViewModel'

/** Each line's color, as the old prototype's `conColor` draws it. */
const colors: Readonly<Record<ConsoleLineKind, string>> = {
  command: 'var(--text-primary)',
  out: 'var(--text-primary)',
  err: 'var(--text-secondary)',
  note: 'var(--text-tertiary)',
  problem: 'var(--status-error)',
}

/**
 * The console, over the window (⇧⌘K, design 1g): the old prototype's panel
 * (`ui.consoleOpen`, `main.html:2127-2138` at `b375fac9`) -- its title and
 * Clear, the lines as the command wrote them, the running bar with Stop, the
 * words Tab offers, and the line typed with Run. The Mac app's `ConsoleView`,
 * its names: Return runs the line, Tab asks the binary what comes next, the
 * arrows recall the lines run, Escape lets the offered words go. Everything
 * drawn is the view-model's `face()`.
 */
export function ConsoleView({ model, close }: { readonly model: ConsoleViewModel; readonly close: () => void }) {
  useObserved(model)
  const face = model.face()
  const [back, setBack] = useState(0)
  const run = () => {
    setBack(0)
    void model.run()
  }
  const onKey = (event: KeyboardEvent<HTMLInputElement>) => {
    if (event.key === 'Enter') {
      event.preventDefault()
      run()
    } else if (event.key === 'Tab') {
      event.preventDefault()
      void model.complete()
    } else if (event.key === 'ArrowUp') {
      const line = model.recall(back + 1)
      if (line === undefined) return
      event.preventDefault()
      setBack(back + 1)
      model.setInput(line)
    } else if (event.key === 'ArrowDown' && back > 0) {
      event.preventDefault()
      setBack(back - 1)
      model.setInput(back - 1 === 0 ? '' : (model.recall(back - 1) ?? ''))
    } else if (event.key === 'Escape' && face.suggestions.length > 0) {
      event.preventDefault()
      model.dismissSuggestions()
    }
  }
  return (
    <div
      role="dialog"
      aria-label="Console"
      className="menu"
      style={{ position: 'absolute', left: 'calc(var(--layout-sidebar) + 12px)', right: 16, bottom: 16, zIndex: 50, maxHeight: 'calc(100% - 32px)', display: 'flex', flexDirection: 'column', fontSize: 'var(--t-small)' }}
    >
      <div style={{ display: 'flex', alignItems: 'center', gap: 10, padding: '12px 12px 10px 18px', borderBottom: '1px solid var(--line-border)' }}>
        <h2 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>Console</h2>
        <span className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
          apogee, as typed · ⇧⌘K
        </span>
        <span style={{ flex: '1 1 auto' }} />
        <button type="button" className="btn-g" onClick={() => model.clear()} style={{ height: 26, fontSize: 'var(--t-small)' }}>
          Clear
        </button>
        <button type="button" className="ib" onClick={close} aria-label="Close the console">
          <svg className="i" width="13" height="13" viewBox="0 0 16 16" aria-hidden="true">
            <path d="M4.5 4.5l7 7M11.5 4.5l-7 7" />
          </svg>
        </button>
      </div>
      <div role="log" aria-label="Console output" className="mono" style={{ flex: '1 1 auto', minHeight: 240, overflow: 'auto', padding: '12px 18px', fontSize: 'var(--t-meta)', lineHeight: 1.6 }}>
        {face.empty ? (
          <p style={{ margin: 0, fontFamily: 'var(--font-ui)', color: 'var(--text-tertiary)' }}>
            Any apogee command runs here, as you’d type it at a prompt. Tab asks apogee what can come next; serve, reset and uninstall stay at the prompt.
          </p>
        ) : null}
        {face.lines.map((line) => (
          <div key={line.id} style={{ whiteSpace: 'pre', minHeight: '1.6em', color: colors[line.kind], fontWeight: line.kind === 'command' ? 600 : 400, marginTop: line.kind === 'command' ? 8 : 0 }}>
            {line.text}
          </div>
        ))}
      </div>
      {face.running.shown ? (
        <div style={{ display: 'flex', alignItems: 'center', gap: 10, padding: '8px 18px', borderTop: '1px solid var(--line-border)' }}>
          <span className="mono" style={{ flex: '1 1 auto', minWidth: 0, overflow: 'hidden', textOverflow: 'ellipsis', whiteSpace: 'nowrap', color: 'var(--text-secondary)', fontSize: 'var(--t-meta)' }}>
            {face.running.busy}
          </span>
          <button type="button" className="btn-g" onClick={() => void model.stop()} style={{ height: 26, fontSize: 'var(--t-small)' }}>
            Stop
          </button>
        </div>
      ) : null}
      {face.suggestions.length > 0 ? (
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, padding: '8px 18px 0', borderTop: '1px solid var(--line-border)' }}>
          {face.suggestions.map((word) => (
            <button key={word} type="button" className="btn-g mono" onClick={() => model.accept(word)} style={{ height: 24, fontSize: 'var(--t-meta)' }}>
              {word}
            </button>
          ))}
        </div>
      ) : null}
      <div style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '10px 12px 12px 18px' }}>
        <span className="mono" style={{ color: 'var(--text-tertiary)', fontSize: 'var(--t-meta)' }}>
          apogee
        </span>
        <input
          type="text"
          className="inp mono"
          value={face.input}
          onChange={(event) => model.setInput(event.target.value)}
          onKeyDown={onKey}
          aria-label="Command"
          placeholder="models list"
          style={{ flex: '1 1 auto', minWidth: 0 }}
        />
        <button type="button" className="btn-g" onClick={run} style={{ height: 28 }}>
          Run
        </button>
      </div>
    </div>
  )
}
