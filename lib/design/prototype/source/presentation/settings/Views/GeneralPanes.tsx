import type { ReactNode } from 'react'
import { Segments } from '../../styles/Components/Segments'
import { useObserved } from '../../styles/Observation/useObserved'
import { Note, SettingRow } from '../Components/SettingRow'
import type { SettingsViewModel } from '../ViewModels/SettingsViewModel'

const small = { height: 26, fontSize: 'var(--t-small)' } as const

/** General: the model for new sessions, thinking and effort, saved or private, what opens, and the command-line tool -- the old pane's rows. */
export function GeneralPane({ model }: { readonly model: SettingsViewModel }) {
  useObserved(model)
  const general = model.face().general
  return (
    <>
      <SettingRow label="Model for new sessions">
        <select className="inp" value={general.model.value} onChange={(event) => void model.setDefaultModel(event.target.value)} aria-label="Model for new sessions">
          {general.model.options.map((option) => (
            <option key={option.value} value={option.value} disabled={option.value === ''}>
              {option.label}
            </option>
          ))}
        </select>
        <Note>You can switch in any session; a switch is noted in the conversation.</Note>
      </SettingRow>
      <SettingRow label="Thinking">
        <div style={{ display: 'flex', flexWrap: 'wrap', alignItems: 'center', gap: 12 }}>
          <Segments label="Thinking" options={general.thinking.map((choice) => [choice.value, choice.label] as const)} current={general.thinking.find((choice) => choice.current)?.value ?? ''} pick={(value) => model.setThinking(value as 'off' | 'on' | 'auto')} inline={false} />
          <label style={{ display: 'flex', alignItems: 'center', gap: 8, fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>
            Effort
            <select className="inp mono" value={general.effort.value} onChange={(event) => model.setEffort(event.target.value)}>
              {general.effort.options.map((option) => (
                <option key={option.value} value={option.value}>
                  {option.label}
                </option>
              ))}
            </select>
          </label>
          <div style={{ flex: '1 1 100%', fontSize: 'var(--t-small)', color: 'var(--text-tertiary)' }}>Auto asks the utility model once per question whether thinking will help.</div>
        </div>
      </SettingRow>
      <SettingRow label="New sessions are">
        <Segments label="New sessions are" options={general.newSessions.map((choice) => [choice.value, choice.label] as const)} current={general.newSessions.find((choice) => choice.current)?.value ?? ''} pick={(value) => model.setNewSessions(value as 'saved' | 'private')} />
        <Note>A private session is never recalled, and is deleted when you close it. ⇧⌘N always starts one.</Note>
      </SettingRow>
      <SettingRow label="When Apogee opens">
        <Segments label="When Apogee opens" options={general.onLaunch.map((choice) => [choice.value, choice.label] as const)} current={general.onLaunch.find((choice) => choice.current)?.value ?? ''} pick={(value) => model.setOnLaunch(value as 'new' | 'last')} />
      </SettingRow>
      <SettingRow label="Command-line tool" last>
        <div className="mono" style={{ fontSize: 'var(--t-ui)', lineHeight: 1.8, color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
          {general.cli.lines.map((line, index) => (
            <span key={line}>
              {index > 0 ? <br /> : null}
              {line}
            </span>
          ))}
        </div>
        {general.cli.passedOver !== '' ? <p style={{ margin: '6px 0 0', fontSize: 'var(--t-meta)', color: 'var(--status-warning)', overflowWrap: 'anywhere' }}>{general.cli.passedOver}</p> : null}
        <p style={{ margin: '6px 0 0', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}>The app and the command line share models, sessions, keys and settings.</p>
        <div style={{ display: 'flex', flexWrap: 'wrap', gap: 6, marginTop: 10 }}>
          <button type="button" className="btn-g" onClick={() => model.useAnother()} style={small}>
            Use another apogee…
          </button>
          {general.cli.useInstalled ? (
            <button type="button" className="btn-g" onClick={() => model.useInstalled()} style={small}>
              Use the installed one
            </button>
          ) : null}
        </div>
        {general.cli.next !== '' ? (
          <p className="mono" style={{ margin: '6px 0 0', fontSize: 'var(--t-meta)', color: 'var(--text-secondary)', overflowWrap: 'anywhere' }}>
            {general.cli.next}
          </p>
        ) : null}
      </SettingRow>
    </>
  )
}

/** Models & roles: the same roles as Models › Roles, drawn by the Models place through the slot the app root fills, and the door to the library. */
export function RolesPane({ model, roles }: { readonly model: SettingsViewModel; readonly roles?: (() => ReactNode) | undefined }) {
  return (
    <>
      <p style={{ margin: '16px 0 6px', fontSize: 'var(--t-body)', color: 'var(--text-secondary)' }}>The same roles as Models › Roles.</p>
      {roles?.()}
      <button type="button" className="btn-g" onClick={() => model.showLibrary()} style={{ marginTop: 16 }}>
        Open models library
      </button>
    </>
  )
}

/** Video & voice: the default video pipeline, the voice models, the voice for new speech, captions, where media goes and the runtimes -- the old pane's rows over the CLI. */
export function MediaPane({ model }: { readonly model: SettingsViewModel }) {
  useObserved(model)
  const media = model.face().media
  return (
    <>
      <SettingRow label="Video pipeline">
        <select className="inp" value={media.video.value} onChange={(event) => void model.setDefaultVideo(event.target.value)} aria-label="Video pipeline">
          {media.video.options.map((option) => (
            <option key={option.value} value={option.value}>
              {option.label}
            </option>
          ))}
        </select>
        <Note>
          Used when a clip or composition doesn’t name one. Saved as <span className="mono">models.default_video</span> in the config the command line reads too. It isn’t a role: nothing else uses it.
        </Note>
      </SettingRow>
      <SettingRow label="Voice model">
        <div style={{ overflowWrap: 'anywhere' }}>{media.voiceModel}</div>
        <Note>There’s no setting for this. With one voice model in the library it’s the one used; with several, each speech and composition names its own.</Note>
      </SettingRow>
      <SettingRow label="Voice for new speech">
        <select className="inp mono" value={media.voice} onChange={(event) => model.setVoice(event.target.value)} aria-label="Voice for new speech">
          {media.voices.map((option) => (
            <option key={option.value} value={option.value}>
              {option.label}
            </option>
          ))}
        </select>
        <Note>af_heart is Kokoro’s own recommendation, and what the command line uses when none is named.</Note>
      </SettingRow>
      <SettingRow label="Captions">
        <label style={{ display: 'inline-flex', alignItems: 'center', gap: 8, cursor: 'pointer' }}>
          <input type="checkbox" checked={media.captions} onChange={() => model.toggleCaptions()} style={{ accentColor: 'var(--status-accent)' }} />
          Caption new compositions
        </label>
        <Note>{media.captionsNote}</Note>
      </SettingRow>
      <SettingRow label="Save to">
        <div className="mono" style={{ fontSize: 'var(--t-ui)', lineHeight: 1.8, color: 'var(--text-secondary)' }}>
          {media.saveTo.map((line, index) => (
            <span key={line}>
              {index > 0 ? <br /> : null}
              {line}
            </span>
          ))}
        </div>
        <button type="button" className="btn-g" onClick={() => model.changeFolders()} style={{ ...small, marginTop: 8 }}>
          Change…
        </button>
        <Note>Every file is saved with its record beside it. Nothing is ever written over: a name that’s taken gets a number.</Note>
      </SettingRow>
      <SettingRow label="Runtimes" last>
        {media.runtimes.map((line) => (
          <div key={line.name} style={{ display: 'flex', alignItems: 'center', gap: 8, padding: '3px 0', fontSize: 'var(--t-small)' }}>
            <span style={{ flex: '0 0 auto', width: 6, height: 6, borderRadius: 'var(--radius-dot)', background: line.color }} />
            <span style={{ fontWeight: 500 }}>{line.name}</span>
            <span style={{ color: 'var(--text-tertiary)', overflowWrap: 'anywhere' }}>{line.status}</span>
          </div>
        ))}
        <button type="button" className="btn-g" onClick={() => model.pick('backends')} style={{ ...small, marginTop: 8 }}>
          Backends &amp; keys
        </button>
      </SettingRow>
    </>
  )
}

/** Memory: recall on or off, as the config says it, what it holds, as `chats recall` lists it, and Forget. */
export function MemoryPane({ model }: { readonly model: SettingsViewModel }) {
  useObserved(model)
  const memory = model.face().memory
  return (
    <>
      <SettingRow label="Recall earlier sessions">
        <label style={{ display: 'inline-flex', alignItems: 'center', gap: 8, cursor: 'pointer' }}>
          <input type="checkbox" checked={memory.recall} onChange={() => void model.toggleRecall()} style={{ accentColor: 'var(--status-accent)' }} />
          {memory.recallLabel}
        </label>
        <Note>Each finished session is summarized once into a private index on this Mac. A turn that draws on one says “Recalled”.</Note>
      </SettingRow>
      <SettingRow label="What’s remembered" last>
        <div className="mono" style={{ fontSize: 'var(--t-ui)', overflowWrap: 'anywhere' }}>
          {memory.holds}
        </div>
        <Note>Private sessions are never summarized. /private turns it off for one chat.</Note>
        <button type="button" className="btn-g" onClick={() => void model.askToForget()} disabled={!memory.canForget} style={{ marginTop: 10 }}>
          Forget all summaries…
        </button>
      </SettingRow>
    </>
  )
}

/** Keyboard: every key the window binds, as the shell's key map lists it. */
export function KeyboardPane({ model }: { readonly model: SettingsViewModel }) {
  useObserved(model)
  return (
    <div role="list" aria-label="Keys" style={{ display: 'grid', gridTemplateColumns: 'repeat(auto-fit, minmax(300px, 1fr))', gap: '0 32px', marginTop: 8 }}>
      {model.face().keyboard.map((line) => (
        <div key={`${line.label} ${line.keys}`} role="listitem" style={{ display: 'grid', gridTemplateColumns: 'minmax(0, 1fr) auto', gap: 12, alignItems: 'center', padding: '9px 0', borderBottom: '1px solid var(--line-border)' }}>
          <span>{line.label}</span>
          <span className="mono" style={{ padding: '1px 7px', border: '1px solid var(--line-strong)', borderRadius: 'var(--radius-chip)', fontSize: 'var(--t-ui)', color: 'var(--text-secondary)' }}>
            {line.keys}
          </span>
        </div>
      ))}
    </div>
  )
}
