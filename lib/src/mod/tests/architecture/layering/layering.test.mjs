// ADR 0001's teeth, proven against planted violations (the house bar): each
// plant is one file added to or changed in the mod's real tree, and the check
// must fail it by name, and only it. Run by node (npm run test:node), never
// under `claude plugin test`: the check reads files, and a hooks module's
// environment has none.
import assert from 'node:assert/strict'
import { test } from 'node:test'

import { LAYERS, violations } from './check.mjs'
import { readMod } from './tree.mjs'

const mod = readMod()

/** The real tree with `planted` laid over it: a path to text, or to null to remove it. */
function plant(planted) {
  const files = new Map(mod.files)
  for (const [path, text] of Object.entries(planted)) {
    if (text === null) files.delete(path)
    else files.set(path, text)
  }

  return violations({ files, api: mod.api })
}

const ENGINE = "import type { EngineInterface } from 'claude-code'\n"

test('the mod holds ADR 0001', () => {
  assert.deepEqual(violations(mod), [])
})

test('every layer has a package, so the shape is real rather than aspirational', () => {
  const layers = new Set([...mod.files.keys()].filter(path => path.startsWith('src/')).map(path => path.split('/')[1]))
  assert.deepEqual([...layers].sort(), [...LAYERS].sort())
})

test('an empty graph is refused', () => {
  const files = new Map([...mod.files].filter(([path]) => !path.startsWith('src/')))
  assert.deepEqual(violations({ files, api: mod.api }), [
    'the import graph is empty: src/ holds no file, and an empty graph proves nothing',
  ])
})

test('a layer left without a package fails by name', () => {
  const files = new Map([...mod.files].filter(([path]) => !path.startsWith('src/business/')))
  files.set('src/presentation/notices/absence.ts', ENGINE + 'export function f($: EngineInterface): void { $.ui.toast("x") }\n')
  assert.deepEqual(violations({ files, api: mod.api }), [
    'business has no package: each layer is a folder src/business/<package>/, and the four layers exist from the first commit',
  ])
})

test('a package without its mirroring tests fails by name', () => {
  assert.deepEqual(plant({ 'src/data/documents/models.ts': 'export const one = 1\n' }), [
    'src/data/documents/ has no tests: they mirror it at tests/data/documents/',
  ])
})

test('a file outside src/<layer>/<package>/ fails as misplaced', () => {
  assert.deepEqual(plant({ 'src/helpers.ts': 'export const one = 1\n', 'src/domain/rules/rule.ts': 'export const two = 2\n' }), [
    'src/domain/rules/rule.ts is not at src/<layer>/<package>/ with a layer of infrastructure, data, business, presentation',
    'src/helpers.ts is not at src/<layer>/<package>/ with a layer of infrastructure, data, business, presentation',
  ])
})

test('an upward edge fails by name: Data importing Business', () => {
  assert.deepEqual(
    plant({ 'src/data/state/upward.ts': "import { findBinary } from '../../business/binary/binary'\nexport const f = findBinary\n" }),
    [
      'upward edge: src/data/state/upward.ts:1 (data) imports src/business/binary/binary.ts (business) -- dependencies point down',
    ],
  )
})

test('an upward edge fails by name: Infrastructure re-exporting Presentation', () => {
  assert.deepEqual(plant({ 'src/infrastructure/discovery/again.ts': "export { findAndSayAbsence } from '../../presentation/notices/absence'\n" }), [
    'upward edge: src/infrastructure/discovery/again.ts:1 (infrastructure) imports src/presentation/notices/absence.ts (presentation) -- dependencies point down',
  ])
})

test('an upward edge fails by name: a type-only import counts too', () => {
  assert.deepEqual(plant({ 'src/business/binary/types.ts': "import type { findAndSayAbsence } from '../../presentation/notices/absence'\nexport type T = typeof findAndSayAbsence\n" }), [
    'upward edge: src/business/binary/types.ts:1 (business) imports src/presentation/notices/absence.ts (presentation) -- dependencies point down',
  ])
})

test('a layer importing the composition root is an upward edge', () => {
  assert.deepEqual(plant({ 'src/presentation/notices/root.ts': "import { register } from '../../../hooks/register'\nexport const r = register\n" }), [
    'upward edge: src/presentation/notices/root.ts:1 (presentation) imports hooks/register.tsx (the composition root) -- dependencies point down',
  ])
})

test('an undeclared edge fails by name: a package, a Node built-in, a path out of the mod', () => {
  assert.deepEqual(
    plant({
      'src/business/binary/outside.ts':
        "import left from 'left-pad'\nimport { readFileSync } from 'node:fs'\nimport { x } from '../../../../cli/source/x'\nexport const all = [left, readFileSync, x]\n",
    }),
    [
      "undeclared edge: src/business/binary/outside.ts:1 imports 'left-pad', which is neither a file of the mod nor 'claude-code'",
      "undeclared edge: src/business/binary/outside.ts:2 imports 'node:fs', which is neither a file of the mod nor 'claude-code'",
      "undeclared edge: src/business/binary/outside.ts:3 imports '../../../../cli/source/x', which is neither a file of the mod nor 'claude-code'",
    ],
  )
})

test('the test kit and the tests stay out of the plugin', () => {
  assert.deepEqual(
    plant({
      'src/data/state/kit.ts': "import { mock } from 'claude-code/testing'\nimport { HOME } from '../../../tests/support/machine'\nexport const all = [mock, HOME]\n",
    }),
    [
      "src/data/state/kit.ts:1 imports 'claude-code/testing': the test kit is the tests' alone",
      'src/data/state/kit.ts:2 imports tests/support/machine.ts: the plugin never imports its tests',
    ],
  )
})

test('import() fails by name: the engine does not load a module holding it', () => {
  assert.deepEqual(plant({ 'src/business/binary/late.ts': "export const later = () => import('../../data/state/binary')\n" }), [
    'src/business/binary/late.ts:1 holds import(), which the engine refuses to load',
  ])
})

test('$.process outside Infrastructure fails by name', () => {
  assert.deepEqual(
    plant({ 'src/business/binary/spawns.ts': ENGINE + "export async function spawn($: EngineInterface) {\n  return $.process.run(['ls'])\n}\n" }),
    ['src/business/binary/spawns.ts:3 uses $.process, which is Infrastructure\'s alone (ADR 0001)'],
  )
})

test('$.ui outside Presentation fails by name, and $.state outside Data', () => {
  assert.deepEqual(
    plant({
      'src/data/state/draws.ts': ENGINE + "export function say($: EngineInterface): void {\n  $.ui.toast('hi')\n}\n",
      'src/presentation/notices/keeps.ts':
        ENGINE + "export async function keep($: EngineInterface) {\n  return $.state.get({ plugin: 'apogee', key: 'binary' })\n}\n",
    }),
    [
      "src/data/state/draws.ts:3 uses $.ui, which is Presentation's alone (ADR 0001)",
      "src/presentation/notices/keeps.ts:3 uses $.state, which is Data's alone (ADR 0001)",
    ],
  )
})

test('the engine reached under another name, destructured or by index fails the same', () => {
  assert.deepEqual(
    plant({
      'src/business/binary/hidden.ts':
        ENGINE +
        'export async function hidden(engine: EngineInterface) {\n' +
        "  const { fs } = engine\n" +
        "  await fs.exists('/x')\n" +
        "  return engine['env'].get('HOME')\n" +
        '}\n',
    }),
    [
      "src/business/binary/hidden.ts:3 uses $.fs, which is Infrastructure's alone (ADR 0001)",
      "src/business/binary/hidden.ts:5 uses $.env, which is Infrastructure's alone (ADR 0001)",
    ],
  )
})

test('$.http fails in every layer: the mod is a client of the binary and opens no port', () => {
  assert.deepEqual(
    plant({ 'src/infrastructure/discovery/port.ts': ENGINE + "export async function port($: EngineInterface) {\n  return $.http.fetch('http://127.0.0.1:8080/v1/models')\n}\n" }),
    ['src/infrastructure/discovery/port.ts:3 uses $.http: the mod is a client of the binary and opens no port (the client invariant)'],
  )
})

test('the atom library outside Data fails by name', () => {
  assert.deepEqual(plant({ 'src/business/binary/atoms.ts': "import { atom, read } from 'claude-code'\nexport const all = [atom, read]\n" }), [
    "src/business/binary/atoms.ts:1 imports atom from 'claude-code': atoms are Data's alone (ADR 0001)",
    "src/business/binary/atoms.ts:1 imports read from 'claude-code': atoms are Data's alone (ADR 0001)",
  ])
})

test('the atom library taken whole, or re-exported, outside Data fails by name', () => {
  assert.deepEqual(
    plant({
      'src/business/binary/ns.ts': "import * as cc from 'claude-code'\nexport const make = () => cc.atom\n",
      'src/business/binary/re.ts': "export { update } from 'claude-code'\n",
      'src/business/binary/all.ts': "export * from 'claude-code'\nexport * as cc from 'claude-code'\n",
    }),
    [
      "src/business/binary/all.ts:1 takes all of 'claude-code', the atom library with it: atoms are Data's alone (ADR 0001)",
      "src/business/binary/all.ts:2 takes all of 'claude-code', the atom library with it: atoms are Data's alone (ADR 0001)",
      "src/business/binary/ns.ts:1 takes all of 'claude-code', the atom library with it: atoms are Data's alone (ADR 0001)",
      "src/business/binary/re.ts:1 re-exports update from 'claude-code': atoms are Data's alone (ADR 0001)",
    ],
  )
})

test('the composition root that reaches a noun itself fails by name', () => {
  const root = mod.files.get('hooks/register.tsx')
  const planted = root.replace('    await findAndSayAbsence($, discovery)\n', "    await findAndSayAbsence($, discovery)\n    $.ui.toast('started')\n")
  assert.notEqual(planted, root)
  const line = planted.split('\n').findIndex(text => text.includes("$.ui.toast('started')")) + 1
  assert.deepEqual(plant({ 'hooks/register.tsx': planted }), [
    `hooks/register.tsx:${line} uses $.ui: the composition root wires the layers and holds nothing itself (ADR 0001)`,
  ])
})

test('code beside the composition root fails by name: hooks/ holds register.tsx alone', () => {
  const root = mod.files.get('hooks/register.tsx')
  const planted = root
    .replace("import type { Register } from 'claude-code'\n", "import type { Register } from 'claude-code'\n\nimport { wire } from './wiring'\n")
    .replace('    await findAndSayAbsence($, discovery)\n', '    await findAndSayAbsence($, discovery)\n    await wire($)\n')
  assert.notEqual(planted, root)
  assert.deepEqual(
    plant({
      'hooks/register.tsx': planted,
      'hooks/wiring.ts':
        ENGINE +
        "import { readFileSync } from 'node:fs'\n" +
        "export async function wire($: EngineInterface) {\n  $.ui.toast('wired')\n  void readFileSync\n  return $.process.run(['ls'])\n}\n",
      'hooks/helper.mjs': "export const helper = $ => $.http.fetch('http://127.0.0.1:8080/health')\n",
    }),
    [
      'hooks/helper.mjs is code beside the composition root: hooks/register.tsx is the hooks module\'s one file, and code lives at src/<layer>/<package>/',
      'hooks/wiring.ts is code beside the composition root: hooks/register.tsx is the hooks module\'s one file, and code lives at src/<layer>/<package>/',
    ],
  )
})

test('code in the contract fails by name: types/ holds declarations (.d.ts) alone', () => {
  assert.deepEqual(
    plant({
      'types/run.ts': ENGINE + "export async function ping($: EngineInterface) {\n  await $.http.fetch('http://127.0.0.1:8081/health')\n  return $.process.run(['/usr/bin/true'])\n}\n",
      'types/bridge.ts': "export { findAndSayAbsence } from '../src/presentation/notices/absence'\n",
      'types/late.js': "export const late = $ => $.process.run(['ls'])\n",
      'src/data/state/ping.ts': ENGINE + "import { ping } from '../../../types/run'\nexport async function pinged($: EngineInterface) {\n  return ping($)\n}\n",
    }),
    [
      'types/bridge.ts is code in the contract: types/ holds declarations (.d.ts) only, and code lives at src/<layer>/<package>/',
      'types/late.js is code in the contract: types/ holds declarations (.d.ts) only, and code lives at src/<layer>/<package>/',
      'types/run.ts is code in the contract: types/ holds declarations (.d.ts) only, and code lives at src/<layer>/<package>/',
    ],
  )
})

test('the contract imports nothing of the mod but itself: an upward edge laundered through it fails by name', () => {
  assert.deepEqual(
    plant({
      'types/bridge.d.ts': "export { findAndSayAbsence } from '../src/presentation/notices/absence'\n",
      'src/infrastructure/discovery/up.ts': "import { findAndSayAbsence } from '../../../types/bridge'\nexport const up = findAndSayAbsence\n",
    }),
    ['types/bridge.d.ts:1 imports src/presentation/notices/absence.ts: the contract imports nothing of the mod but itself'],
  )
})

test('the contract reaches no noun', () => {
  assert.deepEqual(
    plant({ 'types/reach.d.ts': ENGINE + 'export declare const reach: (engine: EngineInterface) => unknown\nexport const reached = (engine: EngineInterface) => engine.process\n' }),
    ['types/reach.d.ts:3 uses $.process: the contract holds declarations only (ADR 0001)'],
  )
})

test('JavaScript in a layer fails by name: the engine loads it, and the check cannot read it', () => {
  assert.deepEqual(
    plant({
      'src/business/binary/run.js':
        "import { findAndSayAbsence } from '../../presentation/notices/absence'\n" +
        "export async function run($) {\n  void findAndSayAbsence\n  return (await $.process.run(['/usr/bin/true'])).exitCode\n}\n",
      'src/business/binary/run.d.ts': ENGINE + 'export declare function run($: EngineInterface): Promise<number>\n',
      'src/data/state/sneak.js': "export const sneak = $ => $.http.fetch('http://127.0.0.1:8080/health')\n",
    }),
    [
      'src/business/binary/run.js is not TypeScript: the engine loads it, but the layering check reads only .ts, .tsx, .mts and .cts files, and a file it cannot read is a file it cannot hold',
      'src/data/state/sneak.js is not TypeScript: the engine loads it, but the layering check reads only .ts, .tsx, .mts and .cts files, and a file it cannot read is a file it cannot hold',
    ],
  )
})

test('downward and same-layer edges, the contract and the engine noun of the own layer pass', () => {
  assert.deepEqual(
    plant({
      'src/presentation/notices/fine.ts':
        ENGINE +
        "import type { ApogeeBinary } from '../../../types'\n" +
        "import { findBinary } from '../../business/binary/binary'\n" +
        "import { findAndSayAbsence } from './absence'\n" +
        "export function fine($: EngineInterface, b: ApogeeBinary): void {\n  $.ui.toast(b.kind)\n  void [findBinary, findAndSayAbsence]\n}\n",
      'src/infrastructure/discovery/fine.ts': ENGINE + "export async function fine($: EngineInterface) {\n  const { process } = $\n  return process.run(['true'])\n}\n",
      'src/business/binary/kinds.ts':
        "import type * as cc from 'claude-code'\nexport type { Register, update } from 'claude-code'\nexport { type atom } from 'claude-code'\nexport type R = cc.Register\n",
      'types/more.d.ts': "import type { ApogeeBinary } from './index'\nimport type { Register } from 'claude-code'\nexport type More = ApogeeBinary | Register\n",
    }),
    [],
  )
})
