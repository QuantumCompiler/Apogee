import type { Rule, Rules } from '../../../infrastructure/process/StandInChild'

/**
 * The Suites place's stand-in answers (design 1p), every one a recording of
 * the Mac app's or a `*-handmade*` file in the CLI's shape: `config get
 * suites` and each suite's `config get suites.<name>` (`suites-config-get*.txt`),
 * `models status` naming the default (`suites-models-status.json`), each
 * suite's footprint as the CLI prices it (`suites-models-status-<name>.json`),
 * and `config set-default-suite <name>` (`config-set-default-suite-<name>-handmade.txt`,
 * `config_suites.cpp`'s `point_default_suite` line); and the members of
 * `coding`, the suite design 1g's execute preset lists beside them
 * (`config-get-suites-coding-handmade.txt`, from that preset's hand-made
 * footprint).
 *
 * Each file is named by its file name, looked up in `SuitesWorlds.ts`'
 * `recordings`, where the `?raw` imports are: this module imports none, so
 * the fixture check lists what every preset names without loading a fixture.
 */

/** Where a world's recordings come from: `SuitesWorlds.ts`' `recording`, or a test's lookup that lists the names. */
export type Lookup = (name: string) => string

/** The recorded suites, as `config get suites` lists them. */
export const recordedSuites = ['cloud', 'fast', 'pair', 'shared', 'vast'] as const

/** The Suites presets, in the old `data-props` order: what the fixture check walks. */
export const suitesPresets = ['suites'] as const

const read = (args: readonly string[], stdout: string): Rule => ({ arguments: args, answer: { stdout } })
const exact = (args: readonly string[], stdout: string): Rule => ({ arguments: args, exact: true, answer: { stdout } })

/** What the Suites place reads and runs on every preset. */
export function suitesRules(look: Lookup): Required<Pick<Rules, 'reads' | 'commands'>> {
  return {
    reads: [read(['models', 'status'], look('suites-models-status.json')), ...recordedSuites.map((name) => read(['models', 'status', '--suite', name], look(`suites-models-status-${name}.json`)))],
    commands: [
      exact(['config', 'get', 'suites'], look('suites-config-get.txt')),
      ...recordedSuites.map((name) => exact(['config', 'get', `suites.${name}`], look(`suites-config-get-${name}.txt`))),
      // The suite the execute preset's door lists beside the recorded five (1g's hand-made coding), its members as its hand-made footprint names them.
      exact(['config', 'get', 'suites.coding'], look('config-get-suites-coding-handmade.txt')),
      ...[...recordedSuites, 'off'].map((name) => exact(['config', 'set-default-suite', name], look(`config-set-default-suite-${name}-handmade.txt`))),
    ],
  }
}

/** The file names the place's answers look up, each once: what the fixture check holds to exist. */
export function suitesPlaceNames(): string[] {
  const named = new Set<string>()
  suitesRules((name) => (named.add(name), ''))
  return [...named]
}
