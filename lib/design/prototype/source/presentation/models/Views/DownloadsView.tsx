import { useObserved } from '../../styles/Observation/useObserved'
import type { DownloadsViewModel } from '../ViewModels/DownloadsViewModel'
import type { ModelsViewModel } from '../ViewModels/ModelsViewModel'

/**
 * Jobs (design 1h): the old prototype's tab (`main.html` at `b375fac9`,
 * `mdl.isJobs`, 1031-1064) -- each job a card with its target, its status,
 * its buttons, its steps' bars and its log -- over the Mac app's
 * `DownloadsViewModel`. Its `DownloadsView`.
 */
export function DownloadsView({ model }: { readonly model: DownloadsViewModel }) {
  useObserved(model)
  const face = model.face()
  const act = (id: number, label: string) => {
    const download = model.downloads.find((candidate) => candidate.id === id)
    if (!download) return
    if (label === 'Stop') void model.stop(download)
    else if (label === 'Show the weights') model.showWeights?.(model.firstLanded(download))
    else if (label === 'Run again') model.again(download)
    else if (label === 'Dismiss') model.dismiss(download)
    else model.resume(download)
  }
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, overflowY: 'auto', padding: '20px 24px 28px' }}>
      <div style={{ maxWidth: 980, display: 'flex', flexDirection: 'column', gap: 14 }}>
        <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Downloads, conversions and checks run as steps. A step that stops runs again from its start; the steps before it are kept and never repeated.</p>
        {face.cards.length === 0 ? <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Nothing has run here yet.</p> : null}
        {face.cards.map((card) => (
          <div key={card.id} className="card" style={{ padding: '14px 16px' }}>
            <div style={{ display: 'flex', alignItems: 'center', flexWrap: 'wrap', gap: '8px 12px' }}>
              <span className="mono" style={{ fontSize: 'var(--t-ui)', fontWeight: 500 }}>
                {card.repo}
              </span>
              <span style={{ color: 'var(--text-tertiary)' }}>→</span>
              <span style={{ fontSize: 'var(--t-body)' }}>{card.target}</span>
              <span style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 'var(--t-small)', color: card.statusColor }}>
                {card.running ? <span className="pulse" style={{ width: 6, height: 6, borderRadius: 'var(--radius-dot)', background: 'var(--status-accent)' }} /> : null}
                {card.status}
              </span>
              <span style={{ flex: '1 1 auto' }} />
              {card.buttons.map((label) => (
                <button key={label} type="button" className={label === 'Stop' || label === 'Show the weights' || label === 'Dismiss' ? 'btn-g' : 'btn-p'} onClick={() => act(card.id, label)}>
                  {label}
                </button>
              ))}
              <button type="button" className="mono hv" onClick={() => model.toggleLog(card.id)} aria-expanded={card.logOpen} style={{ height: 28, padding: '0 8px', border: 0, borderRadius: 'var(--radius-control)', background: 'transparent', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
                log
              </button>
            </div>
            {card.steps.length > 0 ? (
              <div style={{ display: 'grid', gridTemplateColumns: 'repeat(4, minmax(0, 1fr))', gap: 12, marginTop: 12 }}>
                {card.steps.map((step) => (
                  <div key={step.n} style={{ minWidth: 0 }}>
                    <div style={{ display: 'flex', alignItems: 'center', gap: 6, fontSize: 'var(--t-small)' }}>
                      <span className="mono" style={{ fontSize: 'var(--t-label)', color: 'var(--text-tertiary)' }}>
                        {step.n}
                      </span>
                      <span style={{ color: step.labelColor }}>{step.label}</span>
                    </div>
                    <div style={{ height: 4, margin: '6px 0', borderRadius: 2, background: 'var(--line-border)', overflow: 'hidden' }}>
                      <div className={step.pulse ? 'pulse' : undefined} style={{ width: `${step.pct}%`, height: '100%', background: step.barColor }} />
                    </div>
                    <div className="mono" style={{ fontSize: 'var(--t-meta)', color: step.detailColor, overflowWrap: 'anywhere' }}>
                      {step.detail}
                    </div>
                  </div>
                ))}
              </div>
            ) : null}
            {card.outcome !== '' ? <p style={{ margin: '12px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>{card.outcome}</p> : null}
            {card.logOpen ? (
              <div className="mono" style={{ marginTop: 12, padding: '10px 12px', borderRadius: 'var(--radius-control)', background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.7, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' }}>
                {card.log}
              </div>
            ) : null}
          </div>
        ))}
      </div>
    </div>
  )
}

/**
 * The download sheet's content (design 1h): the old prototype's
 * `ui.downloadOpen` (`main.html` at `b375fac9`, 2319-2338) -- the
 * repository, the examples, GGUF or MLX and the level, the steps, the
 * summary. The frame, its title, its intro (the face's `intro`, as the
 * host's subtitle) and its buttons are the shell's host's.
 */
export function DownloadSheet({ model }: { readonly model: ModelsViewModel }) {
  useObserved(model)
  const sheet = model.face().downloadSheet
  return (
    <>
      <label style={{ display: 'flex', flexDirection: 'column', gap: 6, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
        Repository
        <input type="text" className="inp mono" value={sheet.repo} onChange={(event) => model.editDownload((form) => (form.repo = event.target.value))} placeholder="owner/name" aria-label="Hugging Face repository" style={{ height: 32, fontSize: 'var(--t-ui)' }} />
      </label>
      <div className="mono" style={{ fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>
        {sheet.facts}
      </div>
      <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 6 }}>
        <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Try</span>
        {sheet.examples.map((repo) => (
          <button key={repo} type="button" className="mono hv" onClick={() => model.editDownload((form) => (form.repo = repo))} style={{ height: 24, padding: '0 8px', border: '1px solid var(--line-border)', borderRadius: 5, background: 'transparent', fontSize: 'var(--t-meta)', color: 'var(--text-secondary)' }}>
            {repo}
          </button>
        ))}
      </div>
      <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: '12px 20px' }}>
        <div role="group" aria-label="Format" className="seg">
          {(
            [
              ['gguf', 'GGUF · llama.cpp'],
              ['mlx', 'MLX · Apple silicon'],
            ] as const
          ).map(([format, words]) => (
            <button key={format} type="button" aria-pressed={sheet.format === format} onClick={() => model.editDownload((form) => form.setFormat(format))} style={{ background: sheet.format === format ? 'var(--surface-control)' : 'transparent', color: sheet.format === format ? 'var(--text-primary)' : 'var(--text-secondary)', fontWeight: sheet.format === format ? 600 : 400 }}>
              {words}
            </button>
          ))}
        </div>
        {sheet.quants.length > 0 ? (
          <label style={{ display: 'flex', alignItems: 'center', gap: 8, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
            Quantize to
            <select className="inp mono" value={sheet.quant} onChange={(event) => model.editDownload((form) => (form.quant = event.target.value))}>
              {sheet.quants.map((quant) => (
                <option key={quant.value} value={quant.value}>
                  {quant.label}
                </option>
              ))}
            </select>
          </label>
        ) : null}
      </div>
      <ol style={{ margin: 0, padding: '12px 14px 12px 34px', border: '1px solid var(--line-border)', borderRadius: 'var(--radius-card)', background: 'var(--surface-window)', display: 'flex', flexDirection: 'column', gap: 4, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
        {sheet.steps.map((step) => (
          <li key={step}>{step}</li>
        ))}
      </ol>
      <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>{sheet.summary}</p>
    </>
  )
}
