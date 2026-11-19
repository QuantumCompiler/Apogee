import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
import { LayeringCheck } from './LayeringCheck'
import { ModuleMap } from './ModuleMap'
import { DiskTree, PlantedTree } from './SourceTree'

/** The prototype's root, lib/design/prototype: this file is tests/architecture/layering/layering.test.ts. */
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..')

describe('the layering check, on this prototype', () => {
  test('modules.json and the sources hold the design ADR 0001', () => {
    const tree = new DiskTree(root)
    const violations = new LayeringCheck(ModuleMap.read(tree), tree).violations()
    expect(violations, violations.join('\n')).toEqual([])
  })

  // The Mac app's LayeringTests.swift:21. 1d writes it, once the lower layers have modules.
  test.todo('every layer has a module, so the shape is real rather than aspirational')

  test('a dependency planted in the real modules.json fails as its rule', () => {
    const tree = new DiskTree(root)
    const modules = JSON.parse(tree.contents('modules.json') ?? '{}') as { modules: Record<string, string[]> }
    modules.modules['presentation/styles'] = ['presentation/shell']
    const map = ModuleMap.parse(JSON.stringify(modules), tree.contents('package.json') ?? '{}')
    expect(new LayeringCheck(map, tree).violations()).toEqual([
      "presentation/styles depends on its sibling presentation/shell: styles is the Presentation layer's floor and leans on no other module",
    ])
  })

  test('the disk tree reads the prototype, never node_modules, build output or dot files', () => {
    const files = new DiskTree(root).files('')
    expect(files).toContain('modules.json')
    expect(files).toContain('source/main.tsx')
    expect(files.filter((file) => /(^|\/)(node_modules|dist|test-results|playwright-report)\//.test(file))).toEqual([])
    expect(files.filter((file) => file.split('/').some((part) => part.startsWith('.')))).toEqual([])
  })
})

/** The modules' own tests: a stub per module, importing a dev dependency as tests may. */
const stubTest = "import { expect, test } from 'vitest'\n\ntest('stub', () => expect(1).toBe(1))\n"

/**
 * A clean prototype in miniature -- the four layers, styles, the shell, one
 * place, the app, the entry point, their tests and cards, and a config file --
 * that each test plants one violation into.
 */
class Planted {
  modules: Record<string, string[]> = {
    'infrastructure/process': [],
    'data/protocol': [],
    'business/session': ['infrastructure/process', 'data/protocol'],
    'presentation/styles': [],
    'presentation/shell': ['presentation/styles'],
    'presentation/session': ['presentation/styles', 'business/session'],
    'presentation/app': ['presentation/styles', 'presentation/shell', 'presentation/session', 'business/session'],
  }
  dependencies: Record<string, string> = { react: '19.3.1', 'react-dom': '19.3.1' }
  devDependencies: Record<string, string> = { vite: '8.3.5', vitest: '5.1.0' }
  files: Record<string, string> = {
    'source/main.tsx': "import { start } from './presentation/app/Views/ApogeeApp'\n\nstart(document.getElementById('root'))\n",
    'source/infrastructure/process/ChildCommand.ts': 'export interface ChildCommand { readonly arguments: readonly string[] }\n',
    'source/data/protocol/MachineProtocol.ts': 'export interface MachineEvent { readonly event: string }\n',
    'source/business/session/ChatLaunch.ts':
      "import type { MachineEvent } from '../../data/protocol/MachineProtocol'\n" +
      "import type { ChildCommand } from '../../infrastructure/process/ChildCommand'\n\n" +
      'export interface ChatLaunch { readonly command: ChildCommand; readonly first?: MachineEvent }\n',
    'source/presentation/styles/Palette.ts': "export const surface = 'var(--surface-window)'\n",
    'source/presentation/shell/Views/ShellView.tsx':
      "import { surface } from '../../styles/Palette'\n" +
      "import type { ShellViewModel } from '../ViewModels/ShellViewModel'\n\n" +
      'export function ShellView({ model }: { model: ShellViewModel }) {\n' +
      '  return <nav style={{ background: surface }}>{model.title}</nav>\n}\n',
    'source/presentation/shell/ViewModels/ShellViewModel.ts': "export class ShellViewModel { readonly title = 'Apogee' }\n",
    'source/presentation/shell/Previews/ShellView.preview.tsx':
      "import { ShellView } from '../Views/ShellView'\n" +
      "import { ShellViewModel } from '../ViewModels/ShellViewModel'\n\n" +
      'export const Plain = () => <ShellView model={new ShellViewModel()} />\n',
    'source/presentation/session/Views/SessionWindow.tsx':
      "import type { SessionViewModel } from '../ViewModels/SessionViewModel'\n\n" +
      'export const SessionWindow = ({ model }: { model: SessionViewModel }) => <section>{model.title}</section>\n',
    'source/presentation/session/ViewModels/SessionViewModel.ts':
      "import type { ChatLaunch } from '../../../business/session/ChatLaunch'\n\n" +
      "export class SessionViewModel {\n  readonly title = 'Session'\n  constructor(readonly launch?: ChatLaunch) {}\n}\n",
    'source/presentation/session/Previews/SessionWindow.preview.tsx':
      "import { SessionWindow } from '../Views/SessionWindow'\n" +
      "import { SessionViewModel } from '../ViewModels/SessionViewModel'\n\n" +
      'export const Empty = () => <SessionWindow model={new SessionViewModel()} />\n',
    'source/presentation/app/Views/ApogeeApp.tsx':
      "import { StrictMode } from 'react'\n" +
      "import { createRoot } from 'react-dom/client'\n" +
      "import { ShellView } from '../../shell/Views/ShellView'\n" +
      "import { AppViewModel } from '../ViewModels/AppViewModel'\n\n" +
      'export function ApogeeApp({ model }: { model: AppViewModel }) {\n  return <ShellView model={model.shell} />\n}\n\n' +
      'export function start(element: HTMLElement | null) {\n' +
      '  if (element) createRoot(element).render(<StrictMode><ApogeeApp model={new AppViewModel()} /></StrictMode>)\n}\n',
    'source/presentation/app/ViewModels/AppViewModel.ts':
      "import { ShellViewModel } from '../../shell/ViewModels/ShellViewModel'\n\n" +
      'export class AppViewModel { readonly shell = new ShellViewModel() }\n',
    'source/presentation/app/Previews/ApogeeApp.preview.tsx':
      "import { ApogeeApp } from '../Views/ApogeeApp'\n" +
      "import { AppViewModel } from '../ViewModels/AppViewModel'\n\n" +
      'export const Window = () => <ApogeeApp model={new AppViewModel()} />\n',
    'source/infrastructure/CLAUDE.md': '# Infrastructure\n\n**Modules:** `process` -- the stand-in child.\n',
    'source/data/CLAUDE.md': '# Data\n\n**Modules:** `protocol`.\n',
    'source/business/CLAUDE.md': '# Business\n\n**Modules:** `session`.\n',
    'source/presentation/CLAUDE.md': '# Presentation\n\n**Modules:** `styles`, `shell`, `session`, `app`.\n',
    'tests/infrastructure/process/ChildCommand.test.ts': stubTest,
    'tests/data/protocol/MachineProtocol.test.ts': stubTest,
    'tests/business/session/ChatLaunch.test.ts': stubTest,
    'tests/presentation/styles/Palette.test.ts': stubTest,
    'tests/presentation/shell/ShellViewModel.test.ts': stubTest,
    'tests/presentation/session/SessionViewModel.test.ts': stubTest,
    'tests/presentation/app/AppViewModel.test.ts': stubTest,
    'vite.config.ts': "import { resolve } from 'node:path'\nimport { defineConfig } from 'vite'\n\nexport default defineConfig({ root: resolve('.') })\n",
  }

  violations(): string[] {
    const tree = new PlantedTree({
      ...this.files,
      'modules.json': JSON.stringify({ modules: this.modules }),
      'package.json': JSON.stringify({ dependencies: this.dependencies, devDependencies: this.devDependencies }),
    })
    return new LayeringCheck(ModuleMap.read(tree), tree).violations()
  }
}

describe('the layering check, on planted violations', () => {
  test('the miniature is clean before anything is planted', () => {
    expect(new Planted().violations()).toEqual([])
  })

  test('an empty map is refused, never passed', () => {
    const planted = new Planted()
    planted.modules = {}
    expect(planted.violations()).toEqual([
      'the module map is empty: modules.json declares no modules, and an empty map proves nothing',
    ])
  })

  test('a map with no entry at source/<layer>/<package>/ is refused, never passed', () => {
    const planted = new Planted()
    planted.modules = { 'util/helpers': [] }
    planted.files = {}
    expect(planted.violations()).toEqual([
      'util/helpers is at source/util/helpers/, not source/<layer>/<package>/ with a layer of infrastructure, data, business, presentation',
      'the module map has no modules at source/<layer>/<package>/',
    ])
  })

  test('each map entry is one folder, and every folder under source/<layer>/ is in the map', () => {
    const planted = new Planted()
    planted.modules['presentation/voice'] = ['presentation/styles']
    planted.files['source/presentation/stray/Views/Stray.tsx'] = 'export const Stray = () => null\n'
    planted.files['source/util/helpers.ts'] = 'export const helper = 1\n'
    planted.files['source/business/helpers.ts'] = 'export const helper = 2\n'
    expect(planted.violations()).toEqual([
      'presentation/voice is in modules.json, but source/presentation/voice/ is not a folder: each module is one folder',
      'source/util/ is not a layer: source/ holds the four layers and the entry point',
      "source/business/helpers.ts sits in no module: a layer's folder holds its card and its modules' folders",
      'source/presentation/stray/ is not in modules.json: every folder under source/<layer>/ is a module',
    ])
  })

  test('a module outside source/<layer>/<package>/ fails', () => {
    const planted = new Planted()
    planted.modules['util/helpers'] = []
    planted.files['source/util/helpers/Helpers.ts'] = 'export const helper = 1\n'
    expect(planted.violations()).toEqual([
      'util/helpers is at source/util/helpers/, not source/<layer>/<package>/ with a layer of infrastructure, data, business, presentation',
      'source/util/ is not a layer: source/ holds the four layers and the entry point',
    ])
  })

  test('nothing comes from outside: an unlisted package, a dev dependency or Node in source/', () => {
    const planted = new Planted()
    planted.files['source/business/session/ChatLaunch.ts'] += "import { z } from 'zod'\n"
    planted.files['source/data/protocol/MachineProtocol.ts'] += "import { test } from 'vitest'\n"
    planted.files['source/infrastructure/process/ChildCommand.ts'] += "import { readFileSync } from 'node:fs'\n"
    expect(planted.violations()).toEqual([
      'source/business/session/ChatLaunch.ts imports zod, which package.json does not list: nothing comes from outside the prototype but its declared packages',
      "source/data/protocol/MachineProtocol.ts imports vitest, a dev dependency: source/ uses only package.json's dependencies, and dev dependencies stay in tests, scripts and config",
      'source/infrastructure/process/ChildCommand.ts imports node:fs, a Node built-in: source/ runs in the browser, and Node stays in tests, scripts and config',
    ])
  })

  test('a script beside the app may use dev dependencies and Node, as tests and config may (design 1c)', () => {
    const planted = new Planted()
    planted.devDependencies.playwright = '1.64.1'
    planted.files['scripts/baseline.ts'] =
      "import { execFileSync } from 'node:child_process'\n" +
      "import { chromium } from 'playwright'\n\n" +
      "export const tools = [execFileSync, chromium]\n"
    planted.files['scripts/baseline.test.ts'] = "import { tools } from './baseline.ts'\n" + stubTest
    expect(planted.violations()).toEqual([])
    planted.files['scripts/baseline.ts'] += "import { z } from 'zod'\n"
    expect(planted.violations()).toEqual([
      'scripts/baseline.ts imports zod, which package.json does not list: nothing comes from outside the prototype but its declared packages',
    ])
  })

  test('an import leaving lib/design/prototype/ fails', () => {
    const planted = new Planted()
    planted.files['tests/business/session/ChatLaunch.test.ts'] = "import '../../../../spec/html/support.js'\n" + stubTest
    expect(planted.violations()).toEqual([
      'tests/business/session/ChatLaunch.test.ts imports ../../../../spec/html/support.js, which leaves lib/design/prototype/: nothing comes from outside the prototype but its declared packages',
    ])
  })

  test('a Vite glob leaving lib/design/prototype/ fails', () => {
    const planted = new Planted()
    planted.files['source/business/session/ChatLaunch.ts'] +=
      "export const outside = import.meta.glob('../../../../spec/html/*.js', { eager: true, query: '?raw' })\n"
    expect(planted.violations()).toEqual([
      'source/business/session/ChatLaunch.ts imports ../../../../spec/html/*.js, which leaves lib/design/prototype/: nothing comes from outside the prototype but its declared packages',
    ])
  })

  test('a new URL(…, import.meta.url) leaving lib/design/prototype/ fails', () => {
    const planted = new Planted()
    planted.files['source/data/protocol/MachineProtocol.ts'] +=
      "export const page = new URL('../../../../spec/html/main.html', import.meta.url)\n"
    expect(planted.violations()).toEqual([
      'source/data/protocol/MachineProtocol.ts imports ../../../../spec/html/main.html, which leaves lib/design/prototype/: nothing comes from outside the prototype but its declared packages',
    ])
  })

  test('an import of a module the map does not declare fails', () => {
    const planted = new Planted()
    planted.files['source/data/protocol/MachineProtocol.ts'] =
      "import type { ChildCommand } from '../../infrastructure/process/ChildCommand'\n\n" +
      'export interface MachineEvent { readonly event: string; readonly command?: ChildCommand }\n'
    expect(planted.violations()).toEqual([
      'source/data/protocol/MachineProtocol.ts imports infrastructure/process, which data/protocol does not declare as a dependency',
    ])
  })

  test('a map edge or an import naming no module fails', () => {
    const planted = new Planted()
    planted.modules['business/session'] = [...(planted.modules['business/session'] ?? []), 'data/missing']
    planted.files['source/business/session/ChatLaunch.ts'] += "import '../../main'\n"
    expect(planted.violations()).toEqual([
      'business/session depends on data/missing, which is not a module of this map',
      'source/business/session/ChatLaunch.ts imports source/main, which is not a module of this map',
    ])
  })

  test('an upward edge fails, naming both modules and their layers', () => {
    const planted = new Planted()
    planted.modules['data/protocol'] = ['business/session']
    expect(planted.violations()).toEqual([
      'upward edge: data/protocol (data) depends on business/session (business) -- dependencies point down',
    ])
  })

  test('styles leaning on a sibling fails, even declared', () => {
    const planted = new Planted()
    planted.modules['presentation/styles'] = ['presentation/shell']
    planted.files['source/presentation/styles/Palette.ts'] =
      "import { ShellView } from '../shell/Views/ShellView'\n\nexport const surface = 'var(--surface-window)'\nexport const shell = ShellView\n"
    expect(planted.violations()).toEqual([
      "presentation/styles depends on its sibling presentation/shell: styles is the Presentation layer's floor and leans on no other module",
    ])
  })

  test('the shell importing a place fails, even declared', () => {
    const planted = new Planted()
    planted.modules['presentation/shell'] = ['presentation/styles', 'presentation/session']
    planted.files['source/presentation/shell/Views/ShellView.tsx'] =
      "import { SessionWindow } from '../../session/Views/SessionWindow'\n" + planted.files['source/presentation/shell/Views/ShellView.tsx']
    expect(planted.violations()).toEqual([
      "presentation/shell depends on the place presentation/session: the shell never imports a place, and app hands it each place's screen",
    ])
  })

  test('React below Presentation fails: an import or a .tsx file', () => {
    const planted = new Planted()
    planted.files['source/business/session/ChatLaunch.ts'] += "import { useState } from 'react'\n"
    planted.files['source/data/protocol/Badge.tsx'] = 'export const Badge = () => null\n'
    expect(planted.violations()).toEqual([
      'source/data/protocol/Badge.tsx is a .tsx file: React stays in Presentation, and data/protocol is data',
      'source/business/session/ChatLaunch.ts imports react: React stays in Presentation, and business/session is business',
    ])
  })

  test('React imported in a ViewModels/ file fails', () => {
    const planted = new Planted()
    planted.files['source/presentation/session/ViewModels/SessionViewModel.ts'] += "import { useState } from 'react'\n"
    expect(planted.violations()).toEqual([
      'source/presentation/session/ViewModels/SessionViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
    ])
  })

  test('a .tsx view-model fails', () => {
    const planted = new Planted()
    const file = 'source/presentation/shell/ViewModels/ShellViewModel.ts'
    planted.files[`${file}x`] = planted.files[file] ?? ''
    delete planted.files[file]
    expect(planted.violations()).toEqual([
      'source/presentation/shell/ViewModels/ShellViewModel.tsx is a .tsx file: a view-model is plain TypeScript, so it tests without a browser',
    ])
  })

  test('a place missing Views/ or ViewModels/ fails', () => {
    const planted = new Planted()
    delete planted.files['source/presentation/shell/ViewModels/ShellViewModel.ts']
    delete planted.files['source/presentation/session/Views/SessionWindow.tsx']
    expect(planted.violations()).toEqual([
      'presentation/shell has no ViewModels/ with a file in it: a Presentation module is Views/ + ViewModels/',
      'presentation/session has no Views/ with a file in it: a Presentation module is Views/ + ViewModels/',
    ])
  })

  test("a file at a module's root fails", () => {
    const planted = new Planted()
    planted.files['source/presentation/session/Loose.ts'] = 'export const loose = 1\n'
    expect(planted.violations()).toEqual([
      'source/presentation/session/Loose.ts sits at the root of presentation/session: its files live in Views/, ViewModels/ or a folder of their own',
    ])
  })

  test('the entry point is one file, source/main.tsx, importing only presentation/app', () => {
    const planted = new Planted()
    planted.files['source/second.tsx'] = "import { start } from './presentation/app/Views/ApogeeApp'\n\nstart(null)\n"
    planted.files['source/main.tsx'] = "import './presentation/shell/Views/ShellView'\n" + planted.files['source/main.tsx']
    expect(planted.violations()).toEqual([
      'source/ holds 2 files: the entry point is one file, source/main.tsx, and the app itself is presentation/app',
      'source/main.tsx imports ./presentation/shell/Views/ShellView: the entry point imports only presentation/app',
    ])
    delete planted.files['source/second.tsx']
    delete planted.files['source/main.tsx']
    expect(planted.violations()).toEqual([
      'source/main.tsx is missing: the entry point is one file that mounts presentation/app',
    ])
  })

  test('a module without its mirroring test folder fails', () => {
    const planted = new Planted()
    delete planted.files['tests/data/protocol/MachineProtocol.test.ts']
    planted.files['tests/data/protocol/notes.md'] = 'not a test\n'
    expect(planted.violations()).toEqual([
      'data/protocol has no tests at tests/data/protocol/: every module has its mirroring test folder',
    ])
  })

  test('a view without its preview fails by name; a private helper or a constant is left out', () => {
    const planted = new Planted()
    planted.files['source/presentation/session/Views/SessionList.tsx'] =
      'export function SessionList() { return <Row /> }\n' +
      'function Row() { return null }\n' +
      'export const Width = 3\n' +
      'export type ListProps = { readonly count: number }\n'
    planted.files['source/presentation/styles/Components/Empty.tsx'] = 'export const Empty = () => null\n'
    planted.files['source/presentation/session/Previews/SessionList.preview.tsx'] = 'export const Near = () => <SessionListRow />\n'
    expect(planted.violations()).toEqual([
      'source/presentation/styles/Components/Empty.tsx exports Empty, which no file in source/presentation/styles/Previews/ draws: every view a module exports has a preview',
      'source/presentation/session/Views/SessionList.tsx exports SessionList, which no file in source/presentation/session/Previews/ draws: every view a module exports has a preview',
    ])
    planted.files['source/presentation/session/Previews/SessionList.preview.tsx'] = 'export const Two = () => <SessionList />\n'
    planted.files['source/presentation/styles/Previews/Empty.preview.tsx'] = 'export const Plain = () => <Empty/>\n'
    expect(planted.violations()).toEqual([])
  })

  test('a layer card naming a module that is not there fails, and so does a missing card or Modules line', () => {
    const planted = new Planted()
    planted.files['source/presentation/CLAUDE.md'] = '**Modules:** `styles`, `shell`, `session`, `app`, `gallery`.\n'
    planted.files['source/infrastructure/CLAUDE.md'] = '# Infrastructure\n\nModules: process.\n'
    delete planted.files['source/data/CLAUDE.md']
    expect(planted.violations()).toEqual([
      'source/infrastructure/CLAUDE.md has no **Modules:** line',
      'source/data/CLAUDE.md is missing: every layer carries its card',
      "source/presentation/CLAUDE.md names app, gallery, session, shell, styles but the layer's modules are app, session, shell, styles",
    ])
  })
})

describe('how the check reads a file', () => {
  test("imports are read with TypeScript's parser, in every form", () => {
    const source = [
      "import React from 'react'",
      "import type { A } from './a'",
      "import './side-effect.css'",
      "export { b } from '../b'",
      'export * from "../c"',
      "const d = await import('./d')",
      "const e = require('e')",
      "import f = require('f')",
      "type G = typeof import('g')",
      "// import h from 'h'",
      `const text = "import i from 'i'"`,
      "const template = `require('j')`",
      "import k from './k?raw'",
      'export const l = 1',
    ].join('\n')
    expect(LayeringCheck.importsIn(source, 'sample.ts')).toEqual([
      'react', './a', './side-effect.css', '../b', '../c', './d', 'e', 'f', 'g', './k?raw',
    ])
  })

  test('a Vite glob is read as its patterns, as one string or an array, its negations left out', () => {
    const source = [
      "const one = import.meta.glob('./one/*.ts')",
      "const many = import.meta.glob(['../two/*.ts', '!../two/skip.ts', `./three/*.json`], { eager: true })",
      "const other = loader.glob('./not-vite/*.ts')",
      "const env = import.meta.env.MODE",
    ].join('\n')
    expect(LayeringCheck.importsIn(source, 'sample.ts')).toEqual(['./one/*.ts', '../two/*.ts', './three/*.json'])
  })

  test('a new URL(…, import.meta.url) is read as its path; any other URL is not', () => {
    const source = [
      "const page = new URL('../spec/main.html', import.meta.url)",
      "const site = new URL('https://example.com/page')",
      "const based = new URL('./relative', document.baseURI)",
      "const env = new URL('./mode', import.meta.env)",
    ].join('\n')
    expect(LayeringCheck.importsIn(source, 'sample.ts')).toEqual(['../spec/main.html'])
  })

  test('the components a file exports are found as TypeScript declares them', () => {
    const source = [
      'export function Screen() { return null }',
      'export const Row = () => null',
      'export const Memo = memo(function Inner() { return null })',
      'export class Board extends Component {}',
      'export default function Page() { return null }',
      'function Local() { return null }',
      'const Other = () => null',
      'export { Other }',
      'export const Width = 3',
      'export const helper = () => 1',
      'export type Props = { readonly a: number }',
      'export interface Shape { readonly b: string }',
      'function Hidden() { return null }',
    ].join('\n')
    expect(LayeringCheck.exportedComponents(source, 'sample.tsx')).toEqual(['Screen', 'Row', 'Memo', 'Board', 'Page', 'Other'])
    // Kept apart: the sample above already has a default export. A renamed default is drawn by its own name.
    expect(LayeringCheck.exportedComponents('function Shown() { return null }\nexport { Shown as default }', 'sample.tsx')).toEqual([
      'Shown',
    ])
  })

  test('a package is named by its first segment, or its first two when scoped; React is react, react-dom, react-* and @react-*', () => {
    expect(LayeringCheck.packageName('react-dom/client')).toBe('react-dom')
    expect(LayeringCheck.packageName('@react-aria/button/dist')).toBe('@react-aria/button')
    expect(LayeringCheck.packageName('vite?raw')).toBe('vite')
    expect(['react', 'react-dom', 'react-router', '@react-spring/web', '@vitejs/plugin-react', 'preact'].map(LayeringCheck.isReact)).toEqual([
      true, true, true, true, false, false,
    ])
  })
})
