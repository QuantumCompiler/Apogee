import xcodeCheckRegistered from '@recorded/business/reads/Fixtures/xcode-check-registered.json?raw'
import xcodeConfigGet from '@recorded/business/reads/Fixtures/xcode-config-get.txt?raw'
import type { ReactNode } from 'react'
import type { Rules } from '../../../infrastructure/process/StandInChild'
import { chord, KeyMap } from '../../shell/ViewModels/KeyMap'
import { themes } from '../../styles/Appearance/Appearance'
import { Note, SectionHeading, SettingRow } from '../Components/SettingRow'
import type { SettingsViewModel } from '../ViewModels/SettingsViewModel'
import { PermissionsPane, WebPane } from '../Views/AccessPanes'
import { AppearancePane } from '../Views/AppearancePane'
import { BackendsPane, RegisterProviderSheet } from '../Views/BackendsPane'
import { GeneralPane, KeyboardPane, MediaPane, MemoryPane, RolesPane } from '../Views/GeneralPanes'
import { XcodePane } from '../Views/XcodePane'
import { useSettingsPreview } from './SettingsPreview'
import type { SettingsPreviewOptions } from './SettingsPreview'
import { settingsPreferences, settingsPreviewRules } from './SettingsWorlds'
import { keyed } from './SettingsView.preview'

const frame = { width: 820, padding: '8px 28px 32px', background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Pane({ draw, ...options }: SettingsPreviewOptions & { readonly draw: (model: SettingsViewModel) => ReactNode }) {
  const model = useSettingsPreview(options)
  return (
    <div className="ap" data-theme="dark" style={frame}>
      {draw(model)}
    </div>
  )
}

/** The Xcode world with its preset registered: `config get` names its command, and check's bridge row says so. */
function registered(): Rules {
  const rules = settingsPreviewRules('xcode')
  return {
    ...rules,
    reads: [...(rules.reads ?? []), { arguments: ['check'], answer: { stdout: xcodeCheckRegistered } }],
    commands: [...(rules.commands ?? []), { arguments: ['config', 'get', 'mcp_servers.xcode'], answer: { stdout: xcodeConfigGet } }],
  }
}

export const GeneralRead = () => <Pane draw={(model) => <GeneralPane model={model} />} />
/** Use the installed one pressed: the override gone, and its button with it. */
export const GeneralInstalled = () => <Pane draw={(model) => <GeneralPane model={model} />} then={(model) => model.useInstalled()} />

export const AppearanceStart = () => <Pane draw={(model) => <AppearancePane model={model.theme} />} />
/** Two colors edited -- tertiary text as dark as a card, too low; the accent Graphite Light's, low -- and a hex being typed. */
export const AppearanceEdited = () => (
  <Pane
    draw={(model) => <AppearancePane model={model.theme} />}
    then={(model) => {
      model.theme.pickColor('text.tertiary', themes().dark.colors['surface.card'])
      model.theme.pickColor('status.accent', themes().light.colors['status.accent'])
      model.theme.typeHex('status.error', 'ff88')
    }}
  />
)
/** Automatic, its two picks shown, on a light system. */
export const AppearanceAutomatic = () => (
  <Pane
    draw={(model) => <AppearancePane model={model.theme} />}
    then={(model) => {
      model.theme.pick('auto')
      model.theme.service.systemChanged('light')
    }}
  />
)
/** Atkinson Hyperlegible and JetBrains Mono, extra large. */
export const AppearanceLargeText = () => (
  <Pane
    draw={(model) => <AppearancePane model={model.theme} />}
    then={(model) => {
      model.theme.setUiFont('atkinson')
      model.theme.setCodeFont('jetbrains')
      model.theme.setTextSize('xl')
    }}
  />
)

export const Roles = () => <Pane draw={(model) => <RolesPane model={model} roles={() => <p style={{ margin: 0, color: 'var(--text-tertiary)' }}>(The Models place’s roles.)</p>} />} />

export const MediaRead = () => <Pane draw={(model) => <MediaPane model={model} />} />
/** A library with no video or voice model: None alone, and none in the library. */
export const MediaEmpty = () => {
  const rules = settingsPreviewRules('default')
  const world: Rules = { ...rules, reads: [...(rules.reads ?? []), { arguments: ['models', 'list'], answer: { stdout: '{"data":[],"folded":0,"object":"list"}\n' } }] }
  return <Pane draw={(model) => <MediaPane model={model} />} world={world} />
}

export const BackendsNoKey = () => <Pane draw={(model) => <BackendsPane model={model} />} />
export const BackendsKeySet = () => <Pane draw={(model) => <BackendsPane model={model} />} world={keyed()} />
export const BackendsAddingKey = () => <Pane draw={(model) => <BackendsPane model={model} />} then={(model) => model.editKey('openai')} />
/** codex registered: what it said beyond its first line, kept until dismissed. */
export const BackendsRegistered = () => <Pane draw={(model) => <BackendsPane model={model} />} then={(model) => {
  model.askToRegister('codex')
  return model.register()
}} />

export const RegisterCodex = () => <Pane draw={(model) => <RegisterProviderSheet model={model} />} then={(model) => model.askToRegister('codex')} />
export const RegisterOllama = () => (
  <Pane
    draw={(model) => <RegisterProviderSheet model={model} />}
    then={(model) => {
      model.askToRegister('ollama')
      model.typeRegisterModel('llama3.2')
    }}
  />
)

export const PermissionsRead = () => <Pane draw={(model) => <PermissionsPane model={model} />} />
/** What permission cards allowed: always, and for this session. */
export const PermissionsAllowed = () => (
  <Pane
    draw={(model) => <PermissionsPane model={model} />}
    preferences={{ ...settingsPreferences, always: [{ label: 'write_file in ~/code/ingest', from: 'From “Make fetch retries configurable”', tool: 'write_file' }], session: [{ label: 'fetch_url docs.python.org', from: 'From this session' }] }}
  />
)

export const WebSet = () => <Pane draw={(model) => <WebPane model={model} />} />
export const WebUnset = () => <Pane draw={(model) => <WebPane model={model} />} world="nosearch" />

export const MemoryRead = () => <Pane draw={(model) => <MemoryPane model={model} />} />

export const KeyboardList = () => <Pane draw={(model) => <KeyboardPane model={model} />} />
/** A key bound that the design's list does not name: its own line, last. */
export const KeyboardWithAnExtraKey = () => <Pane draw={(model) => <KeyboardPane model={model} />} keys={[...KeyMap.shell, { chord: chord('j', 'control', 'command'), label: 'Jump to the latest turn' }]} />

export const XcodeUnregistered = () => <Pane draw={(model) => <XcodePane model={model.xcode} />} world="xcode" pane="xcode" />
export const XcodeRegistered = () => <Pane draw={(model) => <XcodePane model={model.xcode} />} world={registered()} pane="xcode" />
export const XcodeJustRegistered = () => <Pane draw={(model) => <XcodePane model={model.xcode} />} world="xcode" pane="xcode" then={async (model) => {
  await model.xcode.read()
  await model.xcode.register()
}} />
/** A binary whose check says nothing of Xcode. */
export const XcodeUnchecked = () => <Pane draw={(model) => <XcodePane model={model.xcode} />} pane="xcode" />

/** The parts every pane is drawn from. */
export const Parts = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <SectionHeading>A SECTION</SectionHeading>
    <SettingRow label="A setting">
      <span>Its control</span>
      <Note>Its note.</Note>
    </SettingRow>
    <SettingRow label="The last one" last>
      <span>No rule under it</span>
    </SettingRow>
  </div>
)
