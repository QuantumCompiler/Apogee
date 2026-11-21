import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import { describe, expect, test } from 'vitest'
import { LayeringCheck } from './LayeringCheck'
import { ModuleMap } from './ModuleMap'
import { DiskTree, PlantedTree } from './SourceTree'
import type { SourceTree } from './SourceTree'

/** The prototype's root, lib/design/prototype: this file is tests/architecture/layering/layering.test.ts. */
const root = resolve(dirname(fileURLToPath(import.meta.url)), '../../..')

describe('the layering check, on this prototype', () => {
  test('modules.json and the sources hold the design ADR 0001', () => {
    const tree = new DiskTree(root)
    const violations = new LayeringCheck(ModuleMap.read(tree), tree).violations()
    expect(violations, violations.join('\n')).toEqual([])
  })

  // The Mac app's LayeringTests.swift:21, written by 1d once the lower layers had modules.
  test('every layer has a module, so the shape is real rather than aspirational', () => {
    const map = ModuleMap.read(new DiskTree(root))
    const layers = new Set(map.modules.map((module) => module.id.split('/')[0]))
    for (const layer of LayeringCheck.layers) expect(layers.has(layer), `no module in ${layer}`).toBe(true)
  })

  test('a dependency planted in the real modules.json fails as its rule', () => {
    const tree = new DiskTree(root)
    const modules = JSON.parse(tree.contents('modules.json') ?? '{}') as { modules: Record<string, string[]> }
    modules.modules['presentation/styles'] = ['presentation/shell']
    const map = ModuleMap.parse(JSON.stringify(modules), tree.contents('package.json') ?? '{}')
    expect(new LayeringCheck(map, tree).violations()).toEqual([
      "presentation/styles depends on its sibling presentation/shell: styles is the Presentation layer's floor and leans on no other module",
    ])
  })

  test("violations planted in the real modules fail, each naming its file: React in monitor's view-model, the shell importing the monitor, styles importing the shell (design 1e)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/monitor/ViewModels/MonitorViewModel.ts': `${disk.contents('source/presentation/monitor/ViewModels/MonitorViewModel.ts')}import { useState } from 'react'\n`,
      'source/presentation/shell/Views/ShellView.tsx': `import { MonitorView } from '../../monitor/Views/MonitorView'\n${disk.contents('source/presentation/shell/Views/ShellView.tsx')}`,
      'source/presentation/styles/Styles.ts': `${disk.contents('source/presentation/styles/Styles.ts')}export { ShellViewModel } from '../shell/ViewModels/ShellViewModel'\n`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/presentation/styles/Styles.ts imports presentation/shell, which presentation/styles does not declare as a dependency',
      'source/presentation/shell/Views/ShellView.tsx imports presentation/monitor, which presentation/shell does not declare as a dependency',
      'source/presentation/monitor/ViewModels/MonitorViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
    ])
  })

  test("violations planted in the session fail, each naming its file: React in its view-model, and session importing sessions (design 1u)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/session/ViewModels/SessionViewModel.ts': `import React from 'react'\n${disk.contents('source/presentation/session/ViewModels/SessionViewModel.ts')}`,
      'source/presentation/session/Views/SessionWindow.tsx': `import { SessionsScreen } from '../../sessions/Views/SessionsScreen'\n${disk.contents('source/presentation/session/Views/SessionWindow.tsx')}`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/presentation/session/ViewModels/SessionViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/session/Views/SessionWindow.tsx imports presentation/sessions, which presentation/session does not declare as a dependency',
    ])
  })

  test("violations planted in the Sessions place and the console fail, each naming its file: React in the library's view-model, and the console importing sessions (design 1g)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/sessions/ViewModels/SessionLibraryViewModel.ts': `import { useState } from 'react'\n${disk.contents('source/presentation/sessions/ViewModels/SessionLibraryViewModel.ts')}`,
      'source/presentation/console/Views/ConsoleView.tsx': `import { SessionList } from '../../sessions/Views/SessionList'\n${disk.contents('source/presentation/console/Views/ConsoleView.tsx')}`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/presentation/sessions/ViewModels/SessionLibraryViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/console/Views/ConsoleView.tsx imports presentation/sessions, which presentation/console does not declare as a dependency',
    ])
  })

  test("violations planted in the Models place fail, each naming its file: React in a models view-model, the place importing another place's module, and another place importing models (design 1h)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/models/ViewModels/StoreViewModel.ts': `import { useState } from 'react'\n${disk.contents('source/presentation/models/ViewModels/StoreViewModel.ts')}`,
      'source/presentation/models/Views/ModelsScreen.tsx': `import { SessionList } from '../../sessions/Views/SessionList'\n${disk.contents('source/presentation/models/Views/ModelsScreen.tsx')}`,
      'source/presentation/monitor/ViewModels/MonitorViewModel.ts': `import { ModelsViewModel } from '../../models/ViewModels/ModelsViewModel'\n${disk.contents('source/presentation/monitor/ViewModels/MonitorViewModel.ts')}`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/presentation/models/ViewModels/StoreViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/models/Views/ModelsScreen.tsx imports presentation/sessions, which presentation/models does not declare as a dependency',
      'source/presentation/monitor/ViewModels/MonitorViewModel.ts imports presentation/models, which presentation/monitor does not declare as a dependency',
    ])
  })

  test("violations planted in Settings fail, each naming its file: React in a settings view-model, and settings importing models -- Models & roles draws its roles through the app root's slot (design 1i)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/settings/ViewModels/ThemeSettings.ts': `import { useState } from 'react'\n${disk.contents('source/presentation/settings/ViewModels/ThemeSettings.ts')}`,
      'source/presentation/settings/Views/GeneralPanes.tsx': `import { RoleRows } from '../../models/Views/ModelsScreen'\n${disk.contents('source/presentation/settings/Views/GeneralPanes.tsx')}`,
      'source/presentation/settings/ViewModels/SettingsViewModel.ts': `import { ModelsViewModel } from '../../models/ViewModels/ModelsViewModel'\n${disk.contents('source/presentation/settings/ViewModels/SettingsViewModel.ts')}`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/presentation/settings/ViewModels/SettingsViewModel.ts imports presentation/models, which presentation/settings does not declare as a dependency',
      'source/presentation/settings/ViewModels/ThemeSettings.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/settings/Views/GeneralPanes.tsx imports presentation/models, which presentation/settings does not declare as a dependency',
    ])
  })

  test("violations planted in Training fail, each naming its file: React in TrainingViewModel.ts, the place importing models, and the shell importing training (design 1j)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/training/ViewModels/TrainingViewModel.ts': `import React from 'react'\n${disk.contents('source/presentation/training/ViewModels/TrainingViewModel.ts')}`,
      'source/presentation/training/Views/RunsView.tsx': `import { ModelGlyph } from '../../models/Views/ModelsScreen'\n${disk.contents('source/presentation/training/Views/RunsView.tsx')}`,
      'source/presentation/shell/Views/Sidebar.tsx': `import { TrainingScreen } from '../../training/Views/TrainingScreen'\n${disk.contents('source/presentation/shell/Views/Sidebar.tsx')}`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/presentation/shell/Views/Sidebar.tsx imports presentation/training, which presentation/shell does not declare as a dependency',
      'source/presentation/training/ViewModels/TrainingViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/training/Views/RunsView.tsx imports presentation/models, which presentation/training does not declare as a dependency',
    ])
  })

  test("violations planted in Video fail, each naming its file: React in VideoViewModel.ts, video importing compose, and business/media importing a view-model (design 1k)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/video/ViewModels/VideoViewModel.ts': `import React from 'react'\n${disk.contents('source/presentation/video/ViewModels/VideoViewModel.ts')}`,
      'source/presentation/video/Views/VideoScreen.tsx': `import { ComposeScreen } from '../../compose/Views/ComposeScreen'\n${disk.contents('source/presentation/video/Views/VideoScreen.tsx')}`,
      'source/business/media/ClipForm.ts': `import { VideoForm } from '../../presentation/video/ViewModels/VideoForm'\n${disk.contents('source/business/media/ClipForm.ts')}`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/business/media/ClipForm.ts imports presentation/video, which business/media does not declare as a dependency',
      'source/presentation/video/ViewModels/VideoViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/video/Views/VideoScreen.tsx imports presentation/compose, which presentation/video does not declare as a dependency',
    ])
  })

  test("violations planted in Compose fail, each naming its file: React in ComposeViewModel.ts and compose importing video, which only the app root's slot joins (design 1l)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/compose/ViewModels/ComposeViewModel.ts': `import React from 'react'\n${disk.contents('source/presentation/compose/ViewModels/ComposeViewModel.ts')}`,
      'source/presentation/compose/Views/ComposeView.tsx': `import { VideoScreen } from '../../video/Views/VideoScreen'\n${disk.contents('source/presentation/compose/Views/ComposeView.tsx')}`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/presentation/compose/ViewModels/ComposeViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/compose/Views/ComposeView.tsx imports presentation/video, which presentation/compose does not declare as a dependency',
    ])
  })

  test("violations planted in Voice fail, each naming its file: React in VoiceViewModel.ts, voice importing compose and settings -- the chosen voice is styles' MediaPreferences (design 1m)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/voice/ViewModels/VoiceViewModel.ts': `import React from 'react'\n${disk.contents('source/presentation/voice/ViewModels/VoiceViewModel.ts')}`,
      'source/presentation/voice/Views/SpeakCard.tsx': `import { ComposeView } from '../../compose/Views/ComposeView'\n${disk.contents('source/presentation/voice/Views/SpeakCard.tsx')}`,
      'source/presentation/voice/ViewModels/VoiceFaces.ts': `import type { SettingsPreferences } from '../../settings/ViewModels/SettingsViewModel'\n${disk.contents('source/presentation/voice/ViewModels/VoiceFaces.ts')}`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/presentation/voice/ViewModels/VoiceFaces.ts imports presentation/settings, which presentation/voice does not declare as a dependency',
      'source/presentation/voice/ViewModels/VoiceViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/voice/Views/SpeakCard.tsx imports presentation/compose, which presentation/voice does not declare as a dependency',
    ])
  })

  test("violations planted in Library and Review fail, each naming its file: React in library's and review's view-models, library importing review, and review importing settings -- the two places share only styles' pick list (design 1n)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/library/ViewModels/AgentsViewModel.ts': `import React from 'react'\n${disk.contents('source/presentation/library/ViewModels/AgentsViewModel.ts')}`,
      'source/presentation/review/ViewModels/ReviewViewModel.ts': `import { useState } from 'react'\n${disk.contents('source/presentation/review/ViewModels/ReviewViewModel.ts')}`,
      'source/presentation/library/Views/LibraryScreen.tsx': `import { ReviewScreen } from '../../review/Views/ReviewScreen'\n${disk.contents('source/presentation/library/Views/LibraryScreen.tsx')}`,
      'source/presentation/review/ViewModels/ReviewFaces.ts': `import type { SettingsPreferences } from '../../settings/ViewModels/SettingsViewModel'\n${disk.contents('source/presentation/review/ViewModels/ReviewFaces.ts')}`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/presentation/library/ViewModels/AgentsViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/library/Views/LibraryScreen.tsx imports presentation/review, which presentation/library does not declare as a dependency',
      'source/presentation/review/ViewModels/ReviewFaces.ts imports presentation/settings, which presentation/review does not declare as a dependency',
      'source/presentation/review/ViewModels/ReviewViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
    ])
  })

  test("violations planted in Knowledge and the Code graph fail, each naming its file: React in graph's and knowledge's view-models, graph importing knowledge, and graph importing session -- Ask about this is a callback the app root fills (design 1o)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/graph/ViewModels/GraphViewModel.ts': `import { useState } from 'react'\n${disk.contents('source/presentation/graph/ViewModels/GraphViewModel.ts')}`,
      'source/presentation/knowledge/ViewModels/RecordsViewModel.ts': `import React from 'react'\n${disk.contents('source/presentation/knowledge/ViewModels/RecordsViewModel.ts')}`,
      'source/presentation/graph/Views/GraphScreen.tsx': `import { KnowledgeScreen } from '../../knowledge/Views/KnowledgeScreen'\n${disk.contents('source/presentation/graph/Views/GraphScreen.tsx')}`,
      'source/presentation/graph/ViewModels/GraphFaces.ts': `import type { SessionLeave } from '../../session/ViewModels/SessionViewModel'\n${disk.contents('source/presentation/graph/ViewModels/GraphFaces.ts')}`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/presentation/graph/ViewModels/GraphFaces.ts imports presentation/session, which presentation/graph does not declare as a dependency',
      'source/presentation/graph/ViewModels/GraphViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/graph/Views/GraphScreen.tsx imports presentation/knowledge, which presentation/graph does not declare as a dependency',
      'source/presentation/knowledge/ViewModels/RecordsViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
    ])
  })

  test("violations planted in Suites and Tasks fail, each naming its file: React in their view-models, each importing session, sessions and models -- a chat under a suite, the session's switch and the default are the app root's doors (design 1p)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/suites/ViewModels/SuitesViewModel.ts': `import React from 'react'\n${disk.contents('source/presentation/suites/ViewModels/SuitesViewModel.ts')}`,
      'source/presentation/tasks/ViewModels/TasksViewModel.ts': `import { useState } from 'react'\n${disk.contents('source/presentation/tasks/ViewModels/TasksViewModel.ts')}`,
      'source/presentation/suites/ViewModels/SuitesFaces.ts': `import type { SessionLeave } from '../../session/ViewModels/SessionViewModel'\n${disk.contents('source/presentation/suites/ViewModels/SuitesFaces.ts')}`,
      'source/presentation/suites/Views/SuitesScreen.tsx': `import { SessionList } from '../../sessions/Views/SessionList'\n${disk.contents('source/presentation/suites/Views/SuitesScreen.tsx')}`,
      'source/presentation/suites/Previews/SuitesPreview.ts': `import { ModelsViewModel } from '../../models/ViewModels/ModelsViewModel'\n${disk.contents('source/presentation/suites/Previews/SuitesPreview.ts')}`,
      'source/presentation/tasks/ViewModels/TasksFaces.ts': `import type { SessionLeave } from '../../session/ViewModels/SessionViewModel'\n${disk.contents('source/presentation/tasks/ViewModels/TasksFaces.ts')}`,
      'source/presentation/tasks/Views/TasksScreen.tsx': `import { SessionsScreen } from '../../sessions/Views/SessionsScreen'\n${disk.contents('source/presentation/tasks/Views/TasksScreen.tsx')}`,
      'source/presentation/tasks/ViewModels/TaskWords.ts': `import type { ModelsTab } from '../../models/ViewModels/ModelsViewModel'\n${disk.contents('source/presentation/tasks/ViewModels/TaskWords.ts')}`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/presentation/suites/Previews/SuitesPreview.ts imports presentation/models, which presentation/suites does not declare as a dependency',
      'source/presentation/suites/ViewModels/SuitesFaces.ts imports presentation/session, which presentation/suites does not declare as a dependency',
      'source/presentation/suites/ViewModels/SuitesViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/suites/Views/SuitesScreen.tsx imports presentation/sessions, which presentation/suites does not declare as a dependency',
      'source/presentation/tasks/ViewModels/TaskWords.ts imports presentation/models, which presentation/tasks does not declare as a dependency',
      'source/presentation/tasks/ViewModels/TasksFaces.ts imports presentation/session, which presentation/tasks does not declare as a dependency',
      'source/presentation/tasks/ViewModels/TasksViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/tasks/Views/TasksScreen.tsx imports presentation/sessions, which presentation/tasks does not declare as a dependency',
    ])
  })

  test("violations planted in the gallery fail, each naming its file: the gallery importing the app root, the shell and a place, and React in its view-model -- app hands it what it shows (design 1q)", () => {
    const disk = new DiskTree(root)
    const planted: Record<string, string> = {
      'source/presentation/gallery/ViewModels/GalleryViewModel.ts': `import { useState } from 'react'\n${disk.contents('source/presentation/gallery/ViewModels/GalleryViewModel.ts')}`,
      'source/presentation/gallery/Views/GalleryView.tsx': `import { AppViewModel } from '../../app/ViewModels/AppViewModel'\n${disk.contents('source/presentation/gallery/Views/GalleryView.tsx')}`,
      'source/presentation/gallery/Views/PropSwitcher.tsx': `import { ShellView } from '../../shell/Views/ShellView'\n${disk.contents('source/presentation/gallery/Views/PropSwitcher.tsx')}`,
      'source/presentation/gallery/Views/ComponentsPage.tsx': `import { SuitesScreen } from '../../suites/Views/SuitesScreen'\n${disk.contents('source/presentation/gallery/Views/ComponentsPage.tsx')}`,
    }
    const tree: SourceTree = {
      files: (directory) => disk.files(directory),
      folders: (directory) => disk.folders(directory),
      isDirectory: (path) => disk.isDirectory(path),
      contents: (path) => planted[path] ?? disk.contents(path),
    }
    expect(new LayeringCheck(ModuleMap.read(tree), tree).violations()).toEqual([
      'source/presentation/gallery/ViewModels/GalleryViewModel.ts imports react: a view-model stays free of React, so it tests without a browser',
      'source/presentation/gallery/Views/ComponentsPage.tsx imports presentation/suites, which presentation/gallery does not declare as a dependency',
      'source/presentation/gallery/Views/GalleryView.tsx imports presentation/app, which presentation/gallery does not declare as a dependency',
      'source/presentation/gallery/Views/PropSwitcher.tsx imports presentation/shell, which presentation/gallery does not declare as a dependency',
    ])
  })

  test('the gallery depends on styles alone, and app on the gallery (design 1q)', () => {
    const map = ModuleMap.read(new DiskTree(root))
    expect(map.modules.find((module) => module.id === 'presentation/gallery')?.dependsOn).toEqual(['presentation/styles'])
    expect(map.modules.find((module) => module.id === 'presentation/app')?.dependsOn).toContain('presentation/gallery')
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

  test('a recording imported by name with ?raw, from a place\'s Previews/ or a test, passes (design 1d)', () => {
    const planted = new Planted()
    planted.files['source/presentation/session/Previews/Recorded.ts'] =
      "import agentsList from '@recorded/business/reads/Fixtures/agents-list.json?raw'\n" +
      "import handmade from '../../../../fixtures/business/reads/agents-list-handmade.json?raw'\n\n" +
      'export const rules = [agentsList, handmade]\n'
    planted.files['tests/business/session/Recorded.test.ts'] =
      "import stream from '@recorded/data/protocol/Fixtures/mock-actor.jsonl?raw'\n" +
      "import handmade from '../../../fixtures/data/protocol/turn-handmade.jsonl?raw'\n" + stubTest
    planted.files['scripts/recorded.test.ts'] = "import err from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'\n" + stubTest
    expect(planted.violations()).toEqual([])
  })

  test('a recording imported in Business fails, naming the file: the stand-in is handed its rules', () => {
    const planted = new Planted()
    planted.files['source/business/session/ChatLaunch.ts'] += "import stream from '@recorded/data/protocol/Fixtures/mock-actor.jsonl?raw'\n"
    planted.files['source/business/session/Recorded.spec.ts'] = "export { default } from '@recorded/business/reads/Fixtures/agents-list.json?raw'\n"
    planted.files['source/infrastructure/process/Previews/Recorded.ts'] =
      "export { default } from '../../../../fixtures/business/reads/agents-list-handmade.json?raw'\n"
    planted.files['source/presentation/shell/Views/Recorded.ts'] = "import err from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'\n"
    expect(planted.violations()).toEqual([
      "source/business/session/ChatLaunch.ts imports @recorded/data/protocol/Fixtures/mock-actor.jsonl?raw, a recording: only a Presentation module's Previews/ and the tests import recordings, and the stand-in is handed its rules",
      "source/business/session/Recorded.spec.ts imports @recorded/business/reads/Fixtures/agents-list.json?raw, a recording: only a Presentation module's Previews/ and the tests import recordings, and the stand-in is handed its rules",
      "source/infrastructure/process/Previews/Recorded.ts imports ../../../../fixtures/business/reads/agents-list-handmade.json?raw, a recording: only a Presentation module's Previews/ and the tests import recordings, and the stand-in is handed its rules",
      "source/presentation/shell/Views/Recorded.ts imports @recorded/business/reads/Fixtures/agents-list-bad-config.err?raw, a recording: only a Presentation module's Previews/ and the tests import recordings, and the stand-in is handed its rules",
    ])
  })

  test('a glob over @recorded fails, naming the file: a recording is imported by name', () => {
    const planted = new Planted()
    planted.files['source/presentation/session/Previews/Every.ts'] =
      "export const every = import.meta.glob('@recorded/**/Fixtures/*.json', { query: '?raw', eager: true })\n" +
      'export const one = (name: string) => import(`@recorded/business/reads/Fixtures/${name}.json?raw`)\n' +
      "export const own = import.meta.glob('../../../../fixtures/**/*.json', { query: '?raw' })\n"
    planted.files['tests/data/protocol/Every.test.ts'] = "const all = import.meta.glob('@recorded/**/*.jsonl')\n" + stubTest
    expect(planted.violations()).toEqual([
      'source/presentation/session/Previews/Every.ts globs @recorded/**/Fixtures/*.json: a recording is imported by name, so the build bundles only the recordings a preset uses',
      'source/presentation/session/Previews/Every.ts globs @recorded/business/reads/Fixtures/*: a recording is imported by name, so the build bundles only the recordings a preset uses',
      'source/presentation/session/Previews/Every.ts globs ../../../../fixtures/**/*.json: a recording is imported by name, so the build bundles only the recordings a preset uses',
      'tests/data/protocol/Every.test.ts globs @recorded/**/*.jsonl: a recording is imported by name, so the build bundles only the recordings a preset uses',
    ])
  })

  test('a recording is a file in a Fixtures/ folder, named in full, imported with ?raw', () => {
    const planted = new Planted()
    planted.files['source/presentation/session/Previews/Wrong.ts'] =
      "import parsed from '@recorded/business/reads/Fixtures/agents-list.json'\n" +
      "import swift from '@recorded/presentation/monitor/MonitorViewModelTests.swift?raw'\n" +
      "import out from '@recorded/business/reads/Fixtures/../../../presentation/shell/Fixtures/x.json?raw'\n" +
      "import url from '@recorded/business/reads/Fixtures/agents-list.json?url'\n" +
      "import folder from '@recorded/business/reads/Fixtures?raw'\n" +
      "import own from '../../../../fixtures/business/reads/agents-list-handmade.json'\n"
    const sentence = (specifier: string) =>
      `source/presentation/session/Previews/Wrong.ts imports ${specifier}: a recording is a file in a Fixtures/ folder of @recorded, or under fixtures/, named in full and imported with ?raw so its text stays byte for byte`
    expect(planted.violations()).toEqual([
      sentence('@recorded/business/reads/Fixtures/agents-list.json'),
      sentence('@recorded/presentation/monitor/MonitorViewModelTests.swift?raw'),
      sentence('@recorded/business/reads/Fixtures/../../../presentation/shell/Fixtures/x.json?raw'),
      sentence('@recorded/business/reads/Fixtures/agents-list.json?url'),
      sentence('@recorded/business/reads/Fixtures?raw'),
      sentence('../../../../fixtures/business/reads/agents-list-handmade.json'),
    ])
  })

  test('the tokens root, from styles: @tokens, @tokens/themes.css and, for its types, @tokens/tokens.json pass (design 1e)', () => {
    const planted = new Planted()
    planted.files['source/presentation/styles/Tokens/TokensModule.d.ts'] =
      "declare module '@tokens' {\n  export const tokens: typeof import('@tokens/tokens.json')\n}\n"
    planted.files['source/presentation/styles/Palette.ts'] += "import { tokens } from '@tokens'\nimport '@tokens/themes.css'\nexport const all = tokens\n"
    planted.files['tests/presentation/styles/Palette.test.ts'] = "import { tokens } from '@tokens'\n" + stubTest
    expect(planted.violations()).toEqual([])
  })

  test('the tokens root from anywhere but styles fails, and so does any other file under it or tokens.json by its path', () => {
    const planted = new Planted()
    planted.files['source/presentation/session/Views/SessionWindow.tsx'] += "import { tokens } from '@tokens'\n"
    planted.files['source/business/session/ChatLaunch.ts'] += "import '@tokens/themes.css'\n"
    planted.files['source/presentation/styles/Palette.ts'] +=
      "import other from '@tokens/other.json'\nimport raw from '../../../../spec/json/tokens.json'\n"
    expect(planted.violations()).toEqual([
      "source/business/session/ChatLaunch.ts imports @tokens/themes.css: only presentation/styles reads the tokens, and every other module takes them from it",
      'source/presentation/session/Views/SessionWindow.tsx imports @tokens: only presentation/styles reads the tokens, and every other module takes them from it',
      'source/presentation/styles/Palette.ts imports @tokens/other.json: the tokens root serves @tokens, @tokens/themes.css and @tokens/tokens.json, and nothing else',
      'source/presentation/styles/Palette.ts imports ../../../../spec/json/tokens.json, which leaves lib/design/prototype/: nothing comes from outside the prototype but its declared packages',
    ])
  })

  test("the assets root, from app: one file of lib/design/assets by its name, @assets/<file>, passes, and a test may read it too (design 1q's logo)", () => {
    const planted = new Planted()
    planted.files['source/presentation/app/Models/Logo.ts'] = "import logo from '@assets/Logo-320.png'\n\nexport const artwork = logo\n"
    planted.files['tests/presentation/app/AppViewModel.test.ts'] = "import logo from '@assets/Logo-320.png'\n" + stubTest
    expect(planted.violations()).toEqual([])
  })

  test('the assets root from anywhere but app fails, and so does the root alone, a folder under it, a glob or the file by its path (design 1q)', () => {
    const planted = new Planted()
    planted.files['source/presentation/styles/Palette.ts'] += "import logo from '@assets/Logo-320.png'\n"
    planted.files['source/presentation/session/Views/SessionWindow.tsx'] += "import logo from '@assets/Logo-320.png'\n"
    planted.files['source/presentation/app/Views/ApogeeApp.tsx'] +=
      "import all from '@assets'\nimport icon from '@assets/icons/Logo-16.png'\nconst many = import.meta.glob('@assets/*.png')\nimport raw from '../../../../../assets/Logo-320.png'\n"
    expect(planted.violations()).toEqual([
      'source/presentation/app/Views/ApogeeApp.tsx imports @assets: an asset is one file of lib/design/assets, named in full as @assets/<file>',
      'source/presentation/app/Views/ApogeeApp.tsx imports @assets/icons/Logo-16.png: an asset is one file of lib/design/assets, named in full as @assets/<file>',
      'source/presentation/app/Views/ApogeeApp.tsx imports @assets/*.png: an asset is one file of lib/design/assets, named in full as @assets/<file>',
      'source/presentation/app/Views/ApogeeApp.tsx imports ../../../../../assets/Logo-320.png, which leaves lib/design/prototype/: nothing comes from outside the prototype but its declared packages',
      'source/presentation/session/Views/SessionWindow.tsx imports @assets/Logo-320.png: only presentation/app reads the design’s assets, and hands each to the view that draws it',
      'source/presentation/styles/Palette.ts imports @assets/Logo-320.png: only presentation/app reads the design’s assets, and hands each to the view that draws it',
    ])
  })

  test('a .css file is read too: an @import or url() inside the prototype, a listed package or a data: URI passes (design 1e)', () => {
    const planted = new Planted()
    planted.dependencies['@fontsource/ibm-plex-sans'] = '5.3.0'
    planted.files['source/presentation/styles/Helmet.css'] =
      "@import './Fonts.css';\n/* @import '../../../../spec/html/commented.css'; */\n.ap .grain { background: url(./grain.png) }\n.ap .dot { background: url('data:image/png;base64,AAAA') }\n"
    planted.files['source/presentation/styles/Fonts.css'] =
      "@font-face { font-family: 'IBM Plex Sans'; src: url(\"@fontsource/ibm-plex-sans/files/ibm-plex-sans-latin-400-normal.woff2\") format('woff2') }\n"
    planted.files['source/presentation/styles/grain.png'] = ''
    expect(planted.violations()).toEqual([])
  })

  test('a .css @import or url() leaving lib/design/prototype/ fails with rule 4\'s sentence, as an import does; so do an unlisted package and the network', () => {
    const planted = new Planted()
    planted.files['source/presentation/styles/Helmet.css'] =
      "@import url('../../../../spec/html/old.css');\n" +
      '.ap .logo { background: url(../../../../assets/Logo.png) }\n' +
      "@import 'normalize.css';\n" +
      ".ap .x { background: url('vitest/x.png') }\n" +
      "@import url('https://fonts.googleapis.com/css2?family=IBM+Plex+Sans');\n"
    planted.files['source/presentation/shell/Views/Shell.css'] = "@import '../../session/Views/Session.css';\n"
    planted.files['source/presentation/session/Views/Session.css'] = '.ap .s { color: var(--text-primary) }\n'
    expect(planted.violations()).toEqual([
      'source/presentation/styles/Helmet.css imports ../../../../spec/html/old.css, which leaves lib/design/prototype/: nothing comes from outside the prototype but its declared packages',
      'source/presentation/styles/Helmet.css imports ../../../../assets/Logo.png, which leaves lib/design/prototype/: nothing comes from outside the prototype but its declared packages',
      'source/presentation/styles/Helmet.css imports normalize.css, which package.json does not list: nothing comes from outside the prototype but its declared packages',
      "source/presentation/styles/Helmet.css imports vitest, a dev dependency: source/ uses only package.json's dependencies, and dev dependencies stay in tests, scripts and config",
      'source/presentation/styles/Helmet.css loads https://fonts.googleapis.com/css2?family=IBM+Plex+Sans: the built file opens from disk and loads nothing from the network',
      'source/presentation/shell/Views/Shell.css imports presentation/session, which presentation/shell does not declare as a dependency',
    ])
  })

  test("a .css image-set() leaving lib/design/prototype/ fails with rule 4's sentence, and a remote one with the network's", () => {
    const planted = new Planted()
    planted.files['source/presentation/styles/Helmet.css'] =
      '.ap .logo { background-image: image-set("../../../../spec/html/x.png" 1x) }\n' +
      ".ap .remote { background-image: -webkit-image-set('https://example.com/y.png' 1x) }\n"
    expect(planted.violations()).toEqual([
      'source/presentation/styles/Helmet.css imports ../../../../spec/html/x.png, which leaves lib/design/prototype/: nothing comes from outside the prototype but its declared packages',
      'source/presentation/styles/Helmet.css loads https://example.com/y.png: the built file opens from disk and loads nothing from the network',
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

  test('React imported in Data fails, naming the file (design 1d)', () => {
    const planted = new Planted()
    planted.files['source/data/protocol/MachineProtocol.ts'] += "import { createElement } from 'react'\n"
    expect(planted.violations()).toEqual([
      'source/data/protocol/MachineProtocol.ts imports react: React stays in Presentation, and data/protocol is data',
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
  test("imports are read with TypeScript's parser, in every form; a computed import() as the glob Vite makes of it", () => {
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
      'const m = import(`./m/${name}.ts`)',
      'const n = import(`./n.ts`)',
    ].join('\n')
    expect(LayeringCheck.importsIn(source, 'sample.ts')).toEqual([
      'react', './a', './side-effect.css', '../b', '../c', './d', 'e', 'f', 'g', './k?raw', './m/*', './n.ts',
    ])
  })

  test('a .css file is read as its @import and url() paths, its comments and data: URIs left out (design 1e)', () => {
    const source = [
      "@import './a.css';",
      '@import "b.css" screen;',
      "@import url('./c.css');",
      '@import url(d.css);',
      "/* @import './commented.css'; url(./commented.png) */",
      ".x { background: url(./e.png), url( \"./f.svg#g\" ) }",
      "@font-face { src: url('@fontsource/h/files/h.woff2') format('woff2') }",
      ".y { background: url('data:image/png;base64,AAAA') }",
      ".z { background: url(#gradient) }",
    ].join('\n')
    expect(LayeringCheck.importsIn(source, 'sample.css')).toEqual([
      './a.css', 'b.css', './c.css', 'd.css', './e.png', './f.svg', '@fontsource/h/files/h.woff2',
    ])
  })

  test("a .css image-set() is read as each candidate's path, as Vite reads it: a url() once, a gradient or data: URI not at all", () => {
    const source = [
      ".x { background: image-set(\"./a.png\" 1x, '../b.png#frag' 2x) }",
      '.y { background: -webkit-image-set(c.png 1x) }',
      '.z { background: image-set(url(./d.png) 1x, linear-gradient(red, blue) 2x, "data:image/png;base64,AAAA" 3x) }',
      "/* .w { background: image-set('./commented.png' 1x) } */",
    ].join('\n')
    expect(LayeringCheck.importsIn(source, 'sample.css')).toEqual(['./a.png', '../b.png', 'c.png', './d.png'])
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
