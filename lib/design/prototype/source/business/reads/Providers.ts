import { JSONValue } from '../../data/protocol/JSONValue'
import type { ReadDocument } from '../../data/protocol/ReadDocument'

/** One provider as `providers scan` found it: the Mac app's `SettingsViewModel.Provider`. */
export interface Provider {
  readonly provider: string
  readonly label: string
  readonly tier: string
  /** What the scan says of its backend: `not registered` for one it would register, `backend: <names>` for one registered, empty for one it would not offer. */
  readonly backend: string
  /** The backend type it registers as: `claude-cli`, `ollama-cli`, …. */
  readonly type: string
}

export const Provider = {
  list(document: ReadDocument): Provider[] {
    return (document.entries ?? []).flatMap((entry): Provider[] => {
      const row = JSONValue.object(entry)
      const id = JSONValue.string(row?.provider)
      if (!row || id === undefined) return []
      const text = (key: string) => JSONValue.string(row[key]) ?? ''
      return [{ provider: id, label: text('label') || id, tier: text('tier'), backend: text('backend'), type: text('type') }]
    })
  },

  /** Whether the scan offers it for registering: detected, with no backend yet -- the CLI's own `not registered` (`providers_cmd.cpp`). */
  registers(provider: Provider): boolean {
    return provider.backend === 'not registered' && provider.type !== ''
  },

  /** Whether a backend of its type runs only with a model named: the Ollama CLI's (the CLI's provider table, `needs_model`). */
  needsModel(provider: Provider): boolean {
    return provider.type === 'ollama-cli'
  },

  /**
   * What Register runs, as a person types it (the Mac's `registration(for:model:)`
   * on a binary with the CLI's M33): `providers register <id>`, the TUI's
   * `r` itself; with a model named for a type that runs only with one,
   * `config add-backend <id> --type <type> --model <model>`, which the CLI names for it.
   */
  registration(provider: Provider, model = ''): string[] {
    const named = model.trim()
    return named === '' ? ['providers', 'register', provider.provider] : ['config', 'add-backend', provider.provider, '--type', provider.type, '--model', named]
  },
}

/** What `providers register <id>` said (`object: "provider.registration"`): the Mac's `ProviderRegistration`. */
export interface ProviderRegistration {
  readonly provider: string
  readonly registration: readonly string[]
  readonly rosters: readonly string[]
  /** Every line, as the text form prints them: the registration's, then the rosters'. */
  readonly lines: readonly string[]
}

export const ProviderRegistration = {
  /** The document, when it is a registration's; `undefined` for any other. */
  of(document: ReadDocument): ProviderRegistration | undefined {
    if (document.object !== 'provider.registration') return undefined
    const lines = (key: string) => (JSONValue.array(document.value[key]) ?? []).flatMap((line) => (typeof line === 'string' ? [line] : []))
    const registration = lines('registration')
    const rosters = lines('rosters')
    return { provider: JSONValue.string(document.value.provider) ?? '', registration, rosters, lines: [...registration, ...rosters] }
  },
}

/**
 * The providers with a key stored, as `auth list` prints them (its
 * `credentials_document`): names only -- the document carries no key, and
 * nothing here would keep one. The Mac's `keyed`.
 */
export const KeyedProviders = {
  /** The providers `auth add` takes, in its order (`secrets::slot_names`). */
  providers: ['anthropic', 'openai', 'google'] as readonly string[],
  of(document: ReadDocument): string[] {
    return (document.entries ?? []).flatMap((entry) => {
      const provider = JSONValue.string(JSONValue.object(entry)?.provider)
      return provider === undefined ? [] : [provider]
    })
  },
}
