import { PickColumn, PickRow } from '../../styles/Components/PickList'
import { useObserved } from '../../styles/Observation/useObserved'
import type { RunsFace, SkillsFace, TargetsFace } from '../ViewModels/ReviewFaces'
import type { ReviewViewModel } from '../ViewModels/ReviewViewModel'

const meta = { fontSize: 'var(--t-meta)' } as const
const quiet = { margin: 0, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' } as const
const column = { flex: '1 1 0', minHeight: 0, overflowY: 'auto', padding: '22px 28px 32px' } as const

/**
 * The Review place (design 1n): the old prototype's screen (`main.html` at
 * `b375fac9`, `ui.isReview`) -- its header with the three tabs, and Add
 * target… and Sweep… on Targets and sweeps; then Reviews (the form, its
 * command, the dry run's document), Targets and sweeps (the targets with
 * their grants, the last sweep), or Skills (the lenses beside the one shown)
 * -- drawn from the place's view-model.
 */
export function ReviewScreen({ model }: { readonly model: ReviewViewModel }) {
  useObserved(model)
  const face = model.face()
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex', flexDirection: 'column' }}>
      <header className="ap-tools" style={{ flex: '0 0 52px', display: 'flex', alignItems: 'center', gap: 14, padding: '0 16px 0 24px', borderBottom: '1px solid var(--line-border)' }}>
        <h1 style={{ margin: 0, fontSize: 'var(--t-title)', fontWeight: 600 }}>Review</h1>
        <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Merge requests read by the review skills -- a dry run first</span>
        <span style={{ flex: '1 1 auto' }} />
        <div role="group" aria-label="Show" className="seg">
          {face.tabs.map((tab) => (
            <button key={tab.tab} type="button" aria-pressed={tab.current} onClick={() => model.pick(tab.tab)} style={{ background: tab.current ? 'var(--surface-selection)' : 'transparent', color: 'var(--text-primary)' }}>
              {tab.label}
            </button>
          ))}
        </div>
        {face.targetButtons.shown ? (
          <>
            <button type="button" className="btn-g" onClick={() => model.newTarget()} disabled={!face.targetButtons.enabled}>
              Add target…
            </button>
            <button type="button" className="btn-g" onClick={() => model.pressSweep()} disabled={!face.targetButtons.enabled}>
              Sweep…
            </button>
          </>
        ) : null}
      </header>
      {face.tabs[0]?.current ? <RunsTab model={model} runs={face.runs} /> : null}
      {face.tabs[1]?.current ? <TargetsTab model={model} targets={face.targets} /> : null}
      {face.tabs[2]?.current ? <SkillsTab model={model} skills={face.skills} /> : null}
    </div>
  )
}

/** Reviews: the merge request, its lenses and model, the command the dry run is, and the run's document. */
function RunsTab({ model, runs }: { readonly model: ReviewViewModel; readonly runs: RunsFace }) {
  const run = runs.run
  return (
    <div style={column}>
      <div style={{ maxWidth: 860, margin: '0 auto', display: 'flex', flexDirection: 'column', gap: 14 }}>
        <div style={{ display: 'flex', gap: 8 }}>
          <input type="text" className="inp" value={runs.form.ref} onChange={(event) => model.setForm({ ref: event.target.value })} aria-label="A merge request" placeholder="A merge request: a URL, owner/repo#12 or group/project!4" style={{ flex: '1 1 auto', minWidth: 0 }} />
          <button type="button" className="btn-p" onClick={() => model.pressReview()}>
            Review
          </button>
        </div>
        <div style={{ display: 'flex', gap: 10, alignItems: 'center', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
          <span>{runs.form.lenses}</span>
          <span>·</span>
          <span>{runs.form.model}</span>
        </div>
        {runs.missing !== '' ? <p style={quiet}>{runs.missing}</p> : null}
        {runs.form.command !== '' ? (
          <div className="mono" style={{ ...meta, color: 'var(--text-tertiary)' }}>
            {runs.form.command}
          </div>
        ) : null}
        {run.shown ? (
          <>
            <div>
              <h2 style={{ margin: '6px 0 0', fontSize: 'var(--t-h2)', fontWeight: 600 }}>{run.title}</h2>
              <div className="mono" style={{ marginTop: 4, ...meta, color: 'var(--text-secondary)' }}>
                {run.where}
              </div>
              <div className="mono" style={{ marginTop: 2, ...meta, color: 'var(--text-tertiary)' }}>
                {run.lenses}
              </div>
            </div>
            <div style={{ display: 'flex', alignItems: 'center', gap: 10 }}>
              <span style={{ flex: '1 1 auto', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{run.posted}</span>
              {run.canPost ? (
                <button type="button" className="btn" onClick={() => model.askToPost()}>
                  Post…
                </button>
              ) : null}
            </div>
            {run.findings.map((finding) => (
              <div key={`${finding.at} ${finding.claim}`} style={{ padding: '12px 14px', border: '1px solid var(--line-border)', borderRadius: 8, background: 'var(--surface-card)' }}>
                <div style={{ display: 'flex', alignItems: 'baseline', gap: 10 }}>
                  <span className="mono" style={{ ...meta, fontWeight: 600, color: finding.severityColor }}>
                    {finding.severity}
                  </span>
                  <span className="mono" style={{ ...meta, color: 'var(--text-secondary)' }}>
                    {finding.kind}
                  </span>
                  <span className="mono" style={{ fontSize: 'var(--t-code)' }}>
                    {finding.at}
                  </span>
                  <span style={{ flex: '1 1 auto' }} />
                  <span className="mono" style={{ ...meta, color: 'var(--text-tertiary)' }}>
                    {finding.by}
                  </span>
                </div>
                <p style={{ margin: '6px 0 0' }}>{finding.claim}</p>
                {finding.fix !== '' ? <p style={{ margin: '4px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{finding.fix}</p> : null}
              </div>
            ))}
            <div className="mono" style={{ ...meta, color: 'var(--text-tertiary)' }}>
              {run.counts}
            </div>
          </>
        ) : null}
      </div>
    </div>
  )
}

/** Targets and sweeps: each target with its grant and Remove…, then the last sweep the CLI kept. */
function TargetsTab({ model, targets }: { readonly model: ReviewViewModel; readonly targets: TargetsFace }) {
  const sweep = targets.sweep
  return (
    <div style={column}>
      <div style={{ maxWidth: 860, margin: '0 auto', display: 'flex', flexDirection: 'column', gap: 10 }}>
        {targets.note !== '' ? <p style={{ ...quiet, overflowWrap: 'anywhere' }}>{targets.note}</p> : null}
        {targets.rows.map((row) => (
          <div key={row.name} style={{ display: 'flex', alignItems: 'baseline', gap: 12, padding: '12px 14px', borderRadius: 8, background: 'var(--surface-card)' }}>
            <div style={{ flex: '1 1 auto' }}>
              <div style={{ fontWeight: 500 }}>{row.name}</div>
              <div className="mono" style={{ ...meta, color: 'var(--text-tertiary)' }}>
                {row.facts}
              </div>
            </div>
            <span style={{ ...meta, color: row.postColor }}>{row.post}</span>
            <button type="button" className="btn-g" onClick={() => model.pressRemove(row.name)} aria-label={`Remove ${row.name}…`}>
              Remove…
            </button>
          </div>
        ))}
        {sweep.shown ? (
          <>
            <h2 style={{ margin: '14px 0 0', fontSize: 'var(--t-h2)', fontWeight: 600 }}>The last sweep</h2>
            <div className="mono" style={{ ...meta, color: 'var(--text-secondary)' }}>
              {sweep.summary}
            </div>
            {sweep.targets.map((target) => (
              <div key={target.heading} style={{ padding: '12px 14px', borderRadius: 8, background: 'var(--surface-card)' }}>
                <div style={{ fontSize: 'var(--t-small)', fontWeight: 500 }}>{target.heading}</div>
                {target.error !== '' ? <div style={{ fontSize: 'var(--t-small)', color: 'var(--status-error)' }}>{target.error}</div> : null}
                {target.projects.map((project) => (
                  <div key={project.project}>
                    <div className="mono" style={{ marginTop: 6, fontSize: 'var(--t-code)' }}>
                      {project.project}
                    </div>
                    <div className="mono" style={{ ...meta, color: 'var(--text-tertiary)' }}>
                      {project.watermark}
                    </div>
                    {project.error !== '' ? <div style={{ fontSize: 'var(--t-small)', color: 'var(--status-error)' }}>{project.error}</div> : null}
                    {project.lines.map((line) => (
                      <div key={line} style={{ marginTop: 4, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
                        {line}
                      </div>
                    ))}
                  </div>
                ))}
              </div>
            ))}
            {sweep.kept !== '' ? (
              <div className="mono" style={{ ...meta, color: 'var(--text-tertiary)' }}>
                {sweep.kept}
              </div>
            ) : null}
          </>
        ) : null}
      </div>
    </div>
  )
}

/** Skills: every lens with what it applies to, beside the one shown whole, its prompt read only. */
function SkillsTab({ model, skills }: { readonly model: ReviewViewModel; readonly skills: SkillsFace }) {
  const skill = skills.skill
  return (
    <div style={{ flex: '1 1 0', minHeight: 0, display: 'flex' }}>
      <PickColumn label="Skills">
        {skills.note !== '' ? <p style={{ margin: '8px 12px 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>{skills.note}</p> : null}
        {skills.rows.map((row) => (
          <PickRow key={row.name} name={row.name} meta={row.meta} current={row.current} pick={() => void model.show(row.name)} mono />
        ))}
      </PickColumn>
      <div style={{ flex: '1 1 0', minWidth: 0, overflowY: 'auto', padding: '22px 28px 32px' }}>
        <div style={{ maxWidth: 820 }}>
          {skill.shown ? (
            <>
              <h2 style={{ margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>{skill.name}</h2>
              <p style={{ margin: '8px 0 0' }}>{skill.description}</p>
              <p style={{ margin: '6px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{skill.applies}</p>
              <div className="mono" style={{ marginTop: 14, padding: '10px 12px', borderRadius: 6, background: 'var(--surface-code)', ...meta, lineHeight: 1.6, whiteSpace: 'pre-wrap' }}>
                {skill.prompt}
              </div>
              <p className="mono" style={{ margin: '10px 0 0', ...meta, color: 'var(--text-tertiary)' }}>
                {skill.command}
              </p>
            </>
          ) : (
            <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>{skills.empty}</p>
          )}
        </div>
      </div>
    </div>
  )
}
