import { useObserved } from '../../styles/Observation/useObserved'
import type { TasksViewModel } from '../ViewModels/TasksViewModel'

const label = { display: 'flex', flexDirection: 'column', gap: 6, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' } as const
const code = { padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', overflowWrap: 'anywhere' } as const

/**
 * The New task sheet's body (the old `ui.newTaskOpen`): the recipe, the goal
 * or the recipe's description, input and suggested grants, Done when, the
 * folder, the suite and the rounds, the two grants, the line on asking, the
 * command it runs and a recipe's "The same run as typing". Its frame, title,
 * Cancel and Start task are the shell's sheet host's, handed by the
 * composition.
 */
export function NewTaskSheet({ model }: { readonly model: TasksViewModel }) {
  useObserved(model)
  const sheet = model.sheetFace()
  return (
    <>
      <label style={label}>
        Recipe
        <select className="inp" value={sheet.recipe} onChange={(event) => void model.pickRecipe(event.target.value)}>
          {sheet.recipeOptions.map((option) => (
            <option key={option.value} value={option.value}>
              {option.label}
            </option>
          ))}
        </select>
      </label>
      {sheet.recipeFailure !== '' ? <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--status-error)', overflowWrap: 'anywhere' }}>{sheet.recipeFailure}</p> : null}
      {sheet.hasRecipe ? null : (
        <label style={label}>
          Goal
          <textarea
            rows={3}
            value={sheet.goal}
            onChange={(event) => model.setForm({ goal: event.target.value })}
            placeholder="e.g. Make tests/test_parsers.py pass on Python 3.12"
            style={{ boxSizing: 'border-box', width: '100%', resize: 'vertical', padding: '8px 10px', border: '1px solid var(--line-strong)', borderRadius: 6, background: 'var(--surface-window)', fontSize: 'var(--t-body)', lineHeight: 1.5, color: 'var(--text-primary)' }}
          />
        </label>
      )}
      {sheet.hasRecipe ? (
        <>
          <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{sheet.recipeDesc}</p>
          {sheet.recipeTakesInput ? (
            <label style={label}>
              Input
              <input type="text" className="inp" value={sheet.input} onChange={(event) => model.setForm({ input: event.target.value })} style={{ width: '100%' }} />
              <span style={{ color: 'var(--text-tertiary)' }}>{sheet.recipeInput}</span>
            </label>
          ) : null}
          {sheet.grants.length > 0 ? (
            <div>
              <div style={{ marginBottom: 6, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>Suggests</div>
              <div style={{ display: 'flex', flexWrap: 'wrap', gap: '6px 18px' }}>
                {sheet.grants.map((grant) => (
                  <label key={grant.name} style={{ display: 'inline-flex', alignItems: 'center', gap: 6, fontSize: 'var(--t-small)' }}>
                    <input
                      type="checkbox"
                      checked={grant.on}
                      onChange={() => model.setForm({ grants: grant.on ? sheet.grants.filter((each) => each.on && each.name !== grant.name).map((each) => each.name) : [...sheet.grants.filter((each) => each.on).map((each) => each.name), grant.name] })}
                    />
                    <span className="mono">Allow {grant.name}</span>
                  </label>
                ))}
              </div>
              <p style={{ margin: '6px 0 0', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Given only if ticked: a grant the recipe suggests is never assumed.</p>
            </div>
          ) : null}
        </>
      ) : null}
      <div>
        <div style={{ marginBottom: 6, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>Done when</div>
        <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(220px, 1fr))', gap: 10 }}>
          <label style={{ ...label, color: 'var(--text-tertiary)' }}>
            the answer says
            <input type="text" className="inp" value={sheet.require} onChange={(event) => model.setForm({ require: event.target.value })} placeholder="e.g. 0 failed" style={{ width: '100%' }} />
          </label>
          <label style={{ ...label, color: 'var(--text-tertiary)' }}>
            this file exists and isn’t empty
            <input type="text" className="inp mono" value={sheet.requireFile} onChange={(event) => model.setForm({ requireFile: event.target.value })} placeholder="e.g. REPORT.md" style={{ width: '100%' }} />
          </label>
        </div>
      </div>
      <div style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(150px, 1fr))', gap: 12 }}>
        <label style={label}>
          Folder
          <input type="text" className="inp mono" value={sheet.folder} readOnly aria-label="Folder" />
        </label>
        <label style={label}>
          Suite
          <select className="inp" value={sheet.suite} onChange={(event) => model.setForm({ suite: event.target.value })}>
            {sheet.suiteOptions.map((suite) => (
              <option key={suite} value={suite}>
                {suite}
              </option>
            ))}
          </select>
        </label>
        <label style={label}>
          Rounds, at most
          <select className="inp mono" value={sheet.rounds} onChange={(event) => model.setForm({ rounds: event.target.value })}>
            {sheet.roundsOptions.map((rounds) => (
              <option key={rounds} value={rounds}>
                {rounds}
              </option>
            ))}
          </select>
        </label>
      </div>
      {sheet.hasRecipe ? null : (
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: '14px 28px' }}>
          {(
            [
              ['Writing files', sheet.writes, 'writes'],
              ['Running commands', sheet.cmds, 'cmds'],
            ] as const
          ).map(([title, choices, key]) => (
            <div key={key}>
              <div style={{ marginBottom: 6, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{title}</div>
              <div role="group" aria-label={title} className="seg" style={{ display: 'inline-flex' }}>
                {choices.map((choice) => (
                  <button
                    key={choice.value}
                    type="button"
                    aria-pressed={choice.current}
                    onClick={() => model.setForm({ [key]: choice.value })}
                    style={{ background: choice.current ? 'var(--surface-control)' : 'transparent', color: choice.current ? 'var(--text-primary)' : 'var(--text-secondary)', fontWeight: choice.current ? 600 : 400 }}
                  >
                    {choice.label}
                  </button>
                ))}
              </div>
            </div>
          ))}
        </div>
      )}
      <p style={{ margin: 0, fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>A task never stops to ask. What isn’t granted here is refused, the task is told, and every use of what is granted is recorded.</p>
      <div className="mono" style={code}>
        {sheet.command}
      </div>
      {sheet.hasRecipe ? (
        <div>
          <div style={{ marginBottom: 6, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>The same run as typing</div>
          <div className="mono" style={{ ...code, whiteSpace: 'pre-wrap' }}>
            {sheet.expansion}
          </div>
        </div>
      ) : null}
    </>
  )
}
