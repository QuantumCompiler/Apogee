import type { ReactNode } from 'react'
import { useObserved } from '../../styles/Observation/useObserved'
import { PromoteForm } from '../ViewModels/TrainingForms'
import type { TrainingViewModel } from '../ViewModels/TrainingViewModel'

const label = { display: 'flex', flexDirection: 'column', gap: 6, color: 'var(--text-secondary)' } as const
const control = { width: '100%', height: 30 } as const
const command = { padding: '8px 10px', borderRadius: 6, background: 'var(--surface-code)', fontSize: 'var(--t-meta)', lineHeight: 1.6, color: 'var(--text-secondary)', whiteSpace: 'pre-wrap', overflowWrap: 'anywhere' } as const

function Check({ on, toggle, children }: { readonly on: boolean; readonly toggle: () => void; readonly children: ReactNode }) {
  return (
    <label style={{ display: 'flex', alignItems: 'flex-start', gap: 8, color: 'var(--text-primary)', cursor: 'pointer' }}>
      <input type="checkbox" checked={on} onChange={toggle} style={{ margin: '2px 0 0', accentColor: 'var(--status-accent)' }} />
      <span>{children}</span>
    </label>
  )
}

function Select({ name, value, options, pick }: { readonly name: string; readonly value: string; readonly options: ReadonlyArray<readonly [string, string]>; readonly pick: (value: string) => void }) {
  return (
    <label style={label}>
      {name}
      <select className="inp" value={value} onChange={(event) => pick(event.target.value)} style={control}>
        {options.map(([option, words]) => (
          <option key={option} value={option}>
            {words}
          </option>
        ))}
      </select>
    </label>
  )
}

const optional = (value: string) => (value === '' ? undefined : value)

/**
 * The promote sheet's content (4i's `PromoteSheet`, the old `gs.kind ===
 * 'promote'` sheet at `b375fac9`, 7875-7930): the run's gate, the adapter
 * mode beside the merge, each sending only the flags the CLI takes in it, the
 * command as built, and the CLI's refusal above it when it was reopened. The
 * shell's sheet host frames it, its title and buttons handed in by `app`.
 */
export function PromoteSheet({ model }: { readonly model: TrainingViewModel }) {
  useObserved(model)
  const sheet = model.face().promoteSheet
  const backendOptions: Array<readonly [string, string]> = [...(sheet.backend !== '' && !sheet.entries.includes(sheet.backend) ? [[sheet.backend, sheet.backend] as const] : []), ['', 'Choose an entry'], ...sheet.entries.map((entry) => [entry, entry] as const)]
  return (
    <>
      {sheet.error !== '' ? (
        <p role="alert" style={{ margin: 0, color: 'var(--status-error)', overflowWrap: 'anywhere', whiteSpace: 'pre-wrap' }}>
          {sheet.error}
        </p>
      ) : null}
      {sheet.verdict.text !== '' ? <p style={{ margin: 0, color: sheet.verdict.failed ? 'var(--status-error)' : 'var(--text-secondary)', overflowWrap: 'anywhere' }}>{sheet.verdict.text}</p> : null}
      <Check on={sheet.adapter} toggle={() => model.setPromote({ adapter: !sheet.adapter })}>
        The adapter alone (--adapter): no fuse -- registered on an existing entry&apos;s adapters, over the base it runs
      </Check>
      {sheet.adapter ? (
        <Select name="Onto" value={sheet.backend} options={backendOptions} pick={(backend) => model.setPromote({ backend })} />
      ) : (
        <label style={label}>
          As backend (new, or an existing llamacpp or mlx entry)
          <input type="text" className="inp" value={sheet.backend} onChange={(event) => model.setPromote({ backend: event.target.value })} aria-label="As backend" style={{ ...control, fontFamily: 'var(--font-mono)' }} />
        </label>
      )}
      {sheet.ledger !== '' ? <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>{sheet.ledger}</p> : null}
      {sheet.adapter ? (
        <>
          <label style={label}>
            Gate suite, where the base is quantized (--suite; else the run&apos;s recorded eval&apos;s)
            <input type="text" className="inp" value={sheet.suite} onChange={(event) => model.setPromote({ suite: event.target.value })} aria-label="Gate suite" style={{ ...control, fontFamily: 'var(--font-mono)' }} />
          </label>
          <Select
            name="Judge"
            value={sheet.judge}
            options={[
              ['', "The recorded eval's, or the config's"],
              ['claude', 'claude'],
            ]}
            pick={(judge) => model.setPromote({ judge: optional(judge) })}
          />
          <p style={{ margin: 0, color: 'var(--text-tertiary)', fontSize: 'var(--t-meta)' }}>Over a quantized base the gate scores the candidate there, on this suite; over any other it is the run&apos;s recorded eval.</p>
        </>
      ) : (
        <>
          <Select
            name="Target"
            value={sheet.target}
            options={[
              ['', 'What the backend runs (else GGUF)'],
              ['gguf', 'GGUF, for llama.cpp'],
              ['mlx', 'MLX, as fused'],
            ]}
            pick={(target) => model.setPromote({ target: optional(target) })}
          />
          <Select name="Quantize" value={sheet.quantize} options={[['', 'Keep F16'], ...PromoteForm.levels.map((level) => [level, level] as const)]} pick={(quantize) => model.setPromote({ quantize: optional(quantize) })} />
          <Check on={sheet.keepFused} toggle={() => model.setPromote({ keepFused: !sheet.keepFused })}>
            Keep the fused checkpoint (--keep-fused)
          </Check>
        </>
      )}
      <Select name="Trainer" value={sheet.trainer} options={[['', "The run's recorded one"], ...PromoteForm.trainers.map((trainer) => [trainer, trainer] as const)]} pick={(trainer) => model.setPromote({ trainer: optional(trainer) })} />
      <Check on={sheet.force} toggle={() => model.setPromote({ force: !sheet.force })}>
        Skip the gate (--force): a failing candidate reaches inference
      </Check>
      <div className="mono" style={command}>
        {sheet.command}
      </div>
    </>
  )
}

/** The pull sheet's content (4i's `PullSheet`): a Hugging Face dataset's reference, and `datasets pull` as it would run. */
export function PullSheet({ model }: { readonly model: TrainingViewModel }) {
  useObserved(model)
  const sheet = model.face().pullSheet
  return (
    <>
      <p style={{ margin: 0, color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>Downloads a Hugging Face dataset&apos;s data files -- Parquet, JSONL, JSON, CSV, Arrow or text -- for Prepare to make trainer-ready. A gated dataset needs your Hugging Face token.</p>
      <label style={label}>
        <input type="text" className="inp" value={sheet.ref} onChange={(event) => model.setPullRef(event.target.value)} placeholder="owner/name[@revision][:file]" aria-label="Dataset reference" style={{ ...control, fontFamily: 'var(--font-mono)' }} />
      </label>
      <div className="mono" style={command}>
        {sheet.command}
      </div>
    </>
  )
}
