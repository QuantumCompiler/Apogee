import { execFileSync } from 'node:child_process'
import { readFileSync } from 'node:fs'
import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { specPath } from '../../../scripts/baseline.ts'

/**
 * The old prototype at its frozen revision, for the parity tests (design 1e):
 * its `main.html` read from git at the revision 1c recorded in
 * `parity/baseline.json`, never from the working tree -- so the tests outlive
 * 1t's deletion of the old prototype's folder, and the layering check's
 * retired path holds them to git (a revision's path, `<revision>:…`), the
 * folder named once, by the baseline's `specPath`. Git is only read: `git
 * show`, with no optional locks on the shared index.
 */

/** The prototype's root: this file is tests/presentation/styles/Frozen.ts. */
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..')

/** The revision 1c's baseline froze. */
export const frozenRevision: string = (JSON.parse(readFileSync(resolve(root, 'parity/baseline.json'), 'utf8')) as { revision: string }).revision

let cached: string | undefined

/** The frozen `main.html`, as git holds it at `frozenRevision`. */
export function frozenMainHtml(): string {
  cached ??= execFileSync('git', ['--no-optional-locks', 'show', `${frozenRevision}:${specPath}/html/main.html`], {
    cwd: root,
    encoding: 'utf8',
    maxBuffer: 16 * 1024 * 1024,
  })
  return cached
}

/** What the parity tests call on the old prototype's component. */
export interface FrozenComponent {
  state: Record<string, unknown>
  themes(): Record<string, { name: string; file: string; colors: Record<string, string> }>
  colorGroups(): Array<{ title: string; rows: Array<[string, string, string, string | null]> }>
  lum(hex: string): number
  ratio(a: string, b: string): number
  effTheme(): string
  colorsOf(id: string): Record<string, string>
  withOverride(theme: string, key: string, value: string | null): Record<string, Record<string, string>>
  rootVars(): string
}

/**
 * A fresh instance of the old prototype's `Component`, built as its guide's
 * 30-tick check built it (at `b375fac9`): its script run with
 * a stand-in `DCLogic` whose `setState` merges.
 */
export function frozenComponent(props: Record<string, unknown> = { still: true }): FrozenComponent {
  const code = /<script[^>]*data-dc-script[^>]*>([\s\S]*?)<\/script>/.exec(frozenMainHtml())?.[1]
  if (code === undefined) throw new Error(`the frozen main.html at ${frozenRevision} has no data-dc-script`)
  function DCLogic(this: { props: unknown; state: object }, given: unknown) {
    this.props = given ?? {}
    this.state = {}
  }
  DCLogic.prototype.setState = function (this: { state: object }, patch: object | ((state: object) => object)) {
    Object.assign(this.state, typeof patch === 'function' ? patch(this.state) : patch)
  }
  const Component = new Function('DCLogic', `${code};return Component;`)(DCLogic) as new (given: unknown) => FrozenComponent
  return new Component(props)
}
