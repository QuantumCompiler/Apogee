import authListAnthropic from '../../../../fixtures/business/reads/auth-list-anthropic-handmade.json?raw'
import type { Rules } from '../../../infrastructure/process/StandInChild'
import { SettingsView } from '../Views/SettingsView'
import { useSettingsPreview } from './SettingsPreview'
import type { SettingsPreviewOptions } from './SettingsPreview'
import { fixLines, settingsPreviewRules } from './SettingsWorlds'

/** The window's own frame: the old prototype's at 1440 × 900, the Settings window over it. */
export const frame = { position: 'relative', width: 1440, height: 900, display: 'flex', background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Window(options: SettingsPreviewOptions) {
  const model = useSettingsPreview(options)
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <SettingsView model={model} close={() => undefined} roles={() => <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>(The Models place’s roles are drawn here by the app root.)</p>} />
    </div>
  )
}

/** A stored key for Anthropic, as `auth list` names it: the key-set state. */
export function keyed(): Rules {
  const rules = settingsPreviewRules('default')
  return { ...rules, reads: [...(rules.reads ?? []), { arguments: ['auth', 'list'], answer: { stdout: authListAnthropic } }] }
}

/** General, read: the model for new sessions, the app's own choices and the command-line tool. */
export const General = () => <Window />

/** Each other pane, as its preset opens it. */
export const Appearance = () => <Window pane="appearance" />
export const ModelsAndRoles = () => <Window pane="roles" />
export const VideoAndVoice = () => <Window pane="media" />
export const BackendsAndKeys = () => <Window pane="backends" />
export const Permissions = () => <Window pane="permissions" />
export const WebAccess = () => <Window pane="web" />
export const WebAccessUnset = () => <Window pane="web" world="nosearch" />
export const Memory = () => <Window pane="memory" />
export const Keyboard = () => <Window pane="keyboard" />
export const Xcode = () => <Window pane="xcode" world="xcode" />

/** check --fix running: working, Stop on, both buttons off. */
export const Fixing = () => <Window pane="backends" then={(settings) => settings.fix(['check', '--fix'])} />

/** check --fix failed: the red line, the failing row first in Health. */
export const FixFailed = () => <Window pane="backends" world="fixfailed" ticks={fixLines('fixfailed') + 2} then={(settings) => settings.fix(['check', '--fix'])} />

/** A key stored, its editor open on Replace key…. */
export const KeyEditing = () => <Window pane="backends" world={keyed()} then={(settings) => settings.editKey('anthropic')} />

/** Apogee not installed: nothing read, the Mac app's words. */
export const NotInstalled = () => <Window apogee="missing" />

/** A config the CLI refuses: its words, the red line. */
export const ConfigRefused = () => <Window apogee="config-typo" />
