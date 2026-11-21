import { isBuiltin } from 'node:module'
import { posix } from 'node:path'
import ts from 'typescript'
import type { Module, ModuleMap } from './ModuleMap'
import type { SourceTree } from './SourceTree'

/** A map entry placed at `source/<layer>/<package>/`. */
interface Placed extends Module {
  readonly layer: Layer
  readonly package: string
  /** `source/<layer>/<package>`. */
  readonly path: string
  /** Its layer's place in `LayeringCheck.layers`, lowest first. */
  readonly rank: number
}

type Layer = (typeof LayeringCheck.layers)[number]

/**
 * The design's ADR 0001, mechanically: a TypeScript copy of the Mac app's
 * layering check (lib/src/darwin/tests/architecture/layering), its rules held
 * where a TypeScript tree has them.
 *
 * The map is `modules.json`. A module is one folder, `source/<layer>/<package>/`
 * -- where it sits is its declaration -- and its tests are under
 * `tests/<layer>/<package>/`. The entry point is one file, `source/main.tsx`,
 * that mounts `presentation/app`. The rules, each a violation naming what
 * broke it:
 *
 * - the map is not empty; every entry is one folder at `source/<layer>/<package>/`,
 *   and every folder under `source/<layer>/` is an entry;
 * - nothing comes from outside: a package is one `package.json` lists, a dev
 *   dependency or Node built-in only outside source/ (tests, scripts and
 *   config), and no relative import, Vite glob or `new URL(…, import.meta.url)`
 *   leaves `lib/design/prototype/`;
 * - the fixture roots (design 1d, a part of ADR 0001 §4's data rule held
 *   mechanically): `@recorded/…`, the Mac app's tests by Vite's alias, and
 *   `fixtures/`, the design's own, are reached only from a Presentation
 *   module's `Previews/` and from the tests, each recording by its full name
 *   in a `Fixtures/` folder (or under `fixtures/`) with `?raw`, and never by a
 *   glob;
 * - the tokens root (design 1e): `@tokens` (the typed tokens module the build
 *   generates from lib/design/spec/json/tokens.json), `@tokens/themes.css`
 *   (its CSS) and `@tokens/tokens.json` (the file, for the declaration's
 *   types), and nothing else under it, reached only from `presentation/styles`
 *   and the tests -- the one way to that file from outside the prototype;
 * - the assets root (design 1q): `@assets/<file>`, one file of
 *   lib/design/assets named in full (the logo the gallery draws, which the
 *   build inlines), reached only from `presentation/app`, which hands it to
 *   the view that draws it, and from the tests;
 * - a `.css` file is read as code is: each `@import` and `url()` is held to
 *   the same rules (design 1e), and nothing is loaded from the network;
 * - a module depends only on its own layer or a lower one; `styles` leans on
 *   no sibling; the shell imports no place; a file imports another module
 *   only when the map declares the dependency;
 * - React (`react`, `react-dom`, `react-*`, `@react-*`, any `.tsx`) stays in
 *   Presentation, and never in a `ViewModels/` file;
 * - every Presentation module but `styles` has `Views/` and `ViewModels/`,
 *   with its files inside them or in folders of their own;
 * - the entry point is one file, importing only `presentation/app`;
 * - every module has its mirroring test folder;
 * - every component a Presentation module's `Views/` or `Components/` exports
 *   is drawn (`<Name`) by a file in its `Previews/`;
 * - each layer's card, `source/<layer>/CLAUDE.md`, names exactly its modules.
 */
export class LayeringCheck {
  /** Lowest first: a module may depend on its own layer or any before it. */
  static readonly layers = ['infrastructure', 'data', 'business', 'presentation'] as const

  /** The Presentation module that is the layer's own floor. */
  static readonly stylesPackage = 'styles'

  /** The window around every place, which imports none of them. */
  static readonly shellPackage = 'shell'

  /** The composition root, the module the entry point mounts. */
  static readonly appPackage = 'app'

  static readonly entryPoint = 'source/main.tsx'

  /** Vite's alias for the Mac app's tests, whose `Fixtures/` folders hold the CLI's recordings (design 1d). */
  static readonly recordedAlias = '@recorded'

  /** The design's own fixture root: its hand-made files and new recordings (design 1d). */
  static readonly fixturesRoot = 'fixtures'

  /** The design's tokens, generated from tokens.json at build time (design 1e). */
  static readonly tokensAlias = '@tokens'

  /** What the tokens root serves: the typed module, its CSS, and the file itself for the declaration's types. */
  static readonly tokensForms = ['@tokens', '@tokens/themes.css', '@tokens/tokens.json']

  /** The one module that reads the tokens; every other takes them from it. */
  static readonly tokensReader = 'source/presentation/styles/'

  /** The design's own assets beside the prototype, lib/design/assets: the logo the gallery draws (design 1q), inlined by the build. */
  static readonly assetsAlias = '@assets'

  /** The one module that reads the assets, handing each to the view that draws it (design 1q). */
  static readonly assetsReader = 'source/presentation/app/'

  /** The files whose imports are read with TypeScript's parser. */
  static readonly codeExtensions = ['.ts', '.tsx', '.mts', '.cts', '.js', '.jsx', '.mjs', '.cjs']

  /** The stylesheets, whose `@import` and `url()` are read (design 1e). */
  static readonly styleExtensions = ['.css']

  constructor(
    readonly map: ModuleMap,
    readonly tree: SourceTree,
  ) {}

  /** Every violation, in a stable order; empty when the prototype holds the law. */
  violations(): string[] {
    if (this.map.modules.length === 0) {
      return ['the module map is empty: modules.json declares no modules, and an empty map proves nothing']
    }
    const found: string[] = []
    const modules = this.folders(this.placed(found), found)
    if (modules.length === 0) {
      return [...found, 'the module map has no modules at source/<layer>/<package>/']
    }
    const byId = new Map(modules.map((module) => [module.id, module]))

    found.push(...this.outside())
    for (const module of modules) {
      found.push(...this.edges(module, byId))
      found.push(...this.imports(module, byId))
      if (module.layer === 'presentation' && module.package !== LayeringCheck.stylesPackage) {
        found.push(...this.mvvmShape(module))
      }
      if (module.layer === 'presentation') found.push(...this.previews(module))
    }
    found.push(...this.entry())
    found.push(...this.tests(modules))
    found.push(...this.cards(modules))
    return found
  }

  // MARK: - Placement and folders

  /** The map's entries at `<layer>/<package>`; one that is not is reported and left out. */
  private placed(found: string[]): Placed[] {
    const placed: Placed[] = []
    for (const module of this.map.modules) {
      const parts = module.id.split('/')
      const [layer, name] = parts
      const rank = LayeringCheck.layers.indexOf(layer as Layer)
      if (parts.length !== 2 || rank < 0 || !name) {
        found.push(
          `${module.id} is at source/${module.id}/, not source/<layer>/<package>/ with a layer of ${LayeringCheck.layers.join(', ')}`,
        )
        continue
      }
      placed.push({ ...module, layer: layer as Layer, package: name, path: `source/${module.id}`, rank })
    }
    return placed
  }

  /** Each entry is a folder, and each folder under a layer an entry; an entry with no folder is reported and left out. */
  private folders(placed: Placed[], found: string[]): Placed[] {
    const kept: Placed[] = []
    for (const module of placed) {
      if (this.tree.isDirectory(module.path)) kept.push(module)
      else found.push(`${module.id} is in modules.json, but ${module.path}/ is not a folder: each module is one folder`)
    }
    const declared = new Set(placed.map((module) => module.path))
    for (const name of this.tree.folders('source')) {
      if (!LayeringCheck.layers.includes(name as Layer)) {
        found.push(`source/${name}/ is not a layer: source/ holds the four layers and the entry point`)
      }
    }
    for (const layer of LayeringCheck.layers) {
      const directory = `source/${layer}`
      for (const name of this.tree.folders(directory)) {
        if (!declared.has(`${directory}/${name}`)) {
          found.push(`${directory}/${name}/ is not in modules.json: every folder under source/<layer>/ is a module`)
        }
      }
      for (const file of this.tree.files(directory)) {
        if (!file.slice(directory.length + 1).includes('/') && file !== `${directory}/CLAUDE.md`) {
          found.push(`${file} sits in no module: a layer's folder holds its card and its modules' folders`)
        }
      }
    }
    return kept
  }

  // MARK: - Nothing from outside

  /** Every code file and stylesheet in the prototype: its packages listed, dev ones and Node outside source/, and nothing past its root. */
  private outside(): string[] {
    const found: string[] = []
    for (const file of this.tree.files('')) {
      if (!LayeringCheck.isRead(file)) continue
      const inSource = file.startsWith('source/')
      for (const specifier of LayeringCheck.importsIn(this.tree.contents(file) ?? '', file)) {
        const bare = LayeringCheck.withoutQuery(specifier)
        if (LayeringCheck.isRemote(bare)) {
          found.push(`${file} loads ${specifier}: the built file opens from disk and loads nothing from the network`)
          continue
        }
        if (LayeringCheck.isRelative(bare)) {
          const resolved = LayeringCheck.resolve(file, bare)
          if (resolved === undefined) {
            found.push(
              `${file} imports ${specifier}, which leaves lib/design/prototype/: nothing comes from outside the prototype but its declared packages`,
            )
          } else if (LayeringCheck.inFixtures(resolved)) {
            found.push(...LayeringCheck.recording(file, specifier, LayeringCheck.fixturePathIsWhole(resolved)))
          }
          continue
        }
        if (bare === LayeringCheck.recordedAlias || bare.startsWith(`${LayeringCheck.recordedAlias}/`)) {
          found.push(...LayeringCheck.recording(file, specifier, LayeringCheck.recordedPathIsWhole(bare)))
          continue
        }
        if (bare === LayeringCheck.tokensAlias || bare.startsWith(`${LayeringCheck.tokensAlias}/`)) {
          found.push(...LayeringCheck.tokensRead(file, specifier))
          continue
        }
        if (bare === LayeringCheck.assetsAlias || bare.startsWith(`${LayeringCheck.assetsAlias}/`)) {
          found.push(...LayeringCheck.assetRead(file, specifier))
          continue
        }
        if (isBuiltin(bare)) {
          if (inSource) {
            found.push(`${file} imports ${specifier}, a Node built-in: source/ runs in the browser, and Node stays in tests, scripts and config`)
          }
          continue
        }
        const name = LayeringCheck.packageName(bare)
        if (this.map.dependencies.has(name)) continue
        if (this.map.devDependencies.has(name)) {
          if (inSource) {
            found.push(
              `${file} imports ${name}, a dev dependency: source/ uses only package.json's dependencies, and dev dependencies stay in tests, scripts and config`,
            )
          }
          continue
        }
        found.push(
          `${file} imports ${name}, which package.json does not list: nothing comes from outside the prototype but its declared packages`,
        )
      }
    }
    return found
  }

  /**
   * A recording reached from `file` by `specifier` (design 1d): never by a
   * glob; only from a Presentation module's `Previews/` or a test; and by its
   * full name (`whole`) with `?raw`.
   */
  private static recording(file: string, specifier: string, whole: boolean): string[] {
    const bare = LayeringCheck.withoutQuery(specifier)
    if (/[*{}]/.test(bare)) {
      return [`${file} globs ${specifier}: a recording is imported by name, so the build bundles only the recordings a preset uses`]
    }
    const found: string[] = []
    if (!LayeringCheck.mayImportRecordings(file)) {
      found.push(
        `${file} imports ${specifier}, a recording: only a Presentation module's Previews/ and the tests import recordings, and the stand-in is handed its rules`,
      )
    }
    if (!whole || specifier.slice(bare.length) !== '?raw') {
      found.push(
        `${file} imports ${specifier}: a recording is a file in a Fixtures/ folder of @recorded, or under fixtures/, named in full and imported with ?raw so its text stays byte for byte`,
      )
    }
    return found
  }

  /**
   * The tokens root reached from `file` by `specifier` (design 1e): one of its
   * three forms, from `presentation/styles` or a test.
   */
  private static tokensRead(file: string, specifier: string): string[] {
    if (!LayeringCheck.tokensForms.includes(LayeringCheck.withoutQuery(specifier))) {
      return [`${file} imports ${specifier}: the tokens root serves ${LayeringCheck.tokensForms.join(', ').replace(/, ([^,]*)$/, ' and $1')}, and nothing else`]
    }
    if (!file.startsWith(LayeringCheck.tokensReader) && !LayeringCheck.isTest(file)) {
      return [`${file} imports ${specifier}: only presentation/styles reads the tokens, and every other module takes them from it`]
    }
    return []
  }

  /**
   * The assets root reached from `file` by `specifier` (design 1q): one file
   * of lib/design/assets by its name, never the root, a folder or a glob,
   * from `presentation/app` or a test.
   */
  private static assetRead(file: string, specifier: string): string[] {
    const bare = LayeringCheck.withoutQuery(specifier)
    const name = bare.slice(LayeringCheck.assetsAlias.length + 1)
    if (bare === LayeringCheck.assetsAlias || name === '' || name === '.' || name === '..' || /[/*{}]/.test(name)) {
      return [`${file} imports ${specifier}: an asset is one file of lib/design/assets, named in full as ${LayeringCheck.assetsAlias}/<file>`]
    }
    if (!file.startsWith(LayeringCheck.assetsReader) && !LayeringCheck.isTest(file)) {
      return [`${file} imports ${specifier}: only presentation/app reads the design’s assets, and hands each to the view that draws it`]
    }
    return []
  }

  /** A Presentation module's `Previews/` file, or a test: what may import a recording (design 1d's default). */
  static mayImportRecordings(file: string): boolean {
    return /^source\/presentation\/[^/]+\/Previews\//.test(file) || LayeringCheck.isTest(file)
  }

  /**
   * A file under `tests/` or `e2e/`, or a `*.test.*` / `*.spec.*` beside a
   * script -- never under source/, where a test-named file is source like any
   * other (no runner reads it there) and would carry a recording into a layer.
   */
  static isTest(file: string): boolean {
    return (
      file.startsWith('tests/') ||
      file.startsWith('e2e/') ||
      (!file.startsWith('source/') && /\.(test|spec)\.[cm]?[jt]sx?$/.test(file))
    )
  }

  /** A path, from the prototype's root, under the design's own fixture root. */
  private static inFixtures(path: string): boolean {
    return path === LayeringCheck.fixturesRoot || path.startsWith(`${LayeringCheck.fixturesRoot}/`)
  }

  /** `fixtures/<…>/<file>`: a file under the root, not the root itself. */
  private static fixturePathIsWhole(path: string): boolean {
    return path.split('/').length >= 2 && !path.endsWith('/')
  }

  /** `@recorded/<…>/Fixtures/<…>/<file>`: no empty, `.` or `..` part, and a `Fixtures` folder above the file. */
  private static recordedPathIsWhole(bare: string): boolean {
    const parts = bare.split('/').slice(1)
    const fixtures = parts.indexOf('Fixtures')
    return parts.every((part) => part !== '' && part !== '.' && part !== '..') && fixtures >= 0 && fixtures < parts.length - 1
  }

  // MARK: - Edges

  /** The map's own edges: each to a module, none upward, none from styles to a sibling or from the shell to a place. */
  private edges(module: Placed, byId: Map<string, Placed>): string[] {
    const found: string[] = []
    for (const dependency of module.dependsOn) {
      const other = byId.get(dependency)
      if (!other) {
        found.push(`${module.id} depends on ${dependency}, which is not a module of this map`)
        continue
      }
      if (other.rank > module.rank) {
        found.push(
          `upward edge: ${module.id} (${module.layer}) depends on ${other.id} (${other.layer}) -- dependencies point down`,
        )
      }
      if (
        module.layer === 'presentation' &&
        module.package === LayeringCheck.stylesPackage &&
        other.layer === 'presentation' &&
        other.package !== LayeringCheck.stylesPackage
      ) {
        found.push(
          `${module.id} depends on its sibling ${other.id}: styles is the Presentation layer's floor and leans on no other module`,
        )
      }
      if (module.layer === 'presentation' && module.package === LayeringCheck.shellPackage && LayeringCheck.isPlace(other)) {
        found.push(
          `${module.id} depends on the place ${other.id}: the shell never imports a place, and app hands it each place's screen`,
        )
      }
    }
    return found
  }

  // MARK: - Imports

  /** A module's files: every import of another module declared, and React only where it may be. */
  private imports(module: Placed, byId: Map<string, Placed>): string[] {
    const found: string[] = []
    const declared = new Set(module.dependsOn)
    for (const file of this.tree.files(module.path)) {
      if (!LayeringCheck.isRead(file)) continue
      const inViewModels = file.slice(module.path.length + 1).startsWith('ViewModels/')
      const extension = posix.extname(file)
      if (extension === '.tsx' || extension === '.jsx') {
        if (module.layer !== 'presentation') {
          found.push(`${file} is a ${extension} file: React stays in Presentation, and ${module.id} is ${module.layer}`)
        } else if (inViewModels) {
          found.push(`${file} is a ${extension} file: a view-model is plain TypeScript, so it tests without a browser`)
        }
      }
      for (const specifier of LayeringCheck.importsIn(this.tree.contents(file) ?? '', file)) {
        const bare = LayeringCheck.withoutQuery(specifier)
        if (LayeringCheck.isRelative(bare)) {
          const resolved = LayeringCheck.resolve(file, bare)
          if (resolved === undefined || LayeringCheck.inFixtures(resolved)) continue // outside() names it
          const target = LayeringCheck.moduleAt(resolved, byId)
          if (!target) {
            found.push(`${file} imports ${resolved}, which is not a module of this map`)
          } else if (target.id !== module.id && !declared.has(target.id)) {
            found.push(`${file} imports ${target.id}, which ${module.id} does not declare as a dependency`)
          }
          continue
        }
        const name = LayeringCheck.packageName(bare)
        if (!LayeringCheck.isReact(name)) continue
        if (module.layer !== 'presentation') {
          found.push(`${file} imports ${name}: React stays in Presentation, and ${module.id} is ${module.layer}`)
        } else if (inViewModels) {
          found.push(`${file} imports ${name}: a view-model stays free of React, so it tests without a browser`)
        }
      }
    }
    return found
  }

  /** The module a resolved path lies in, if any. */
  private static moduleAt(path: string, byId: Map<string, Placed>): Placed | undefined {
    const parts = path.split('/')
    if (parts[0] !== 'source' || parts.length < 3) return undefined
    return byId.get(`${parts[1]}/${parts[2]}`)
  }

  // MARK: - Presentation's shape

  private mvvmShape(module: Placed): string[] {
    const found: string[] = []
    for (const required of ['Views', 'ViewModels']) {
      if (!this.tree.files(`${module.path}/${required}`).some(LayeringCheck.isCode)) {
        found.push(`${module.id} has no ${required}/ with a file in it: a Presentation module is Views/ + ViewModels/`)
      }
    }
    for (const file of this.tree.files(module.path)) {
      if (!file.slice(module.path.length + 1).includes('/')) {
        found.push(
          `${file} sits at the root of ${module.id}: its files live in Views/, ViewModels/ or a folder of their own`,
        )
      }
    }
    return found
  }

  // MARK: - Previews

  /** Every component `Views/` or `Components/` exports is drawn by some file in the module's `Previews/`. */
  private previews(module: Placed): string[] {
    const found: string[] = []
    const previews = this.tree.files(`${module.path}/Previews`).map((file) => this.tree.contents(file) ?? '')
    const drawn = this.tree
      .files(module.path)
      .filter((file) => /^(Views|Components)\//.test(file.slice(module.path.length + 1)) && LayeringCheck.isCode(file))
    for (const file of drawn) {
      for (const name of LayeringCheck.exportedComponents(this.tree.contents(file) ?? '', file)) {
        if (!previews.some((text) => LayeringCheck.draws(name, text))) {
          found.push(
            `${file} exports ${name}, which no file in ${module.path}/Previews/ draws: every view a module exports has a preview`,
          )
        }
      }
    }
    return found
  }

  // MARK: - The entry point

  /** One file directly in source/, `main.tsx`, importing only `presentation/app`. */
  private entry(): string[] {
    const found: string[] = []
    const files = this.tree.files('source').filter((file) => !file.slice('source/'.length).includes('/'))
    if (!files.includes(LayeringCheck.entryPoint)) {
      found.push(`${LayeringCheck.entryPoint} is missing: the entry point is one file that mounts presentation/app`)
    }
    if (files.length > 1) {
      found.push(
        `source/ holds ${files.length} files: the entry point is one file, ${LayeringCheck.entryPoint}, and the app itself is presentation/app`,
      )
    }
    const app = `source/presentation/${LayeringCheck.appPackage}`
    for (const specifier of LayeringCheck.importsIn(this.tree.contents(LayeringCheck.entryPoint) ?? '', LayeringCheck.entryPoint)) {
      const bare = LayeringCheck.withoutQuery(specifier)
      const resolved = LayeringCheck.isRelative(bare) ? LayeringCheck.resolve(LayeringCheck.entryPoint, bare) : undefined
      if (resolved !== app && !resolved?.startsWith(`${app}/`)) {
        found.push(`${LayeringCheck.entryPoint} imports ${specifier}: the entry point imports only presentation/app`)
      }
    }
    return found
  }

  // MARK: - Tests mirror the source

  private tests(modules: Placed[]): string[] {
    return modules
      .filter((module) => !this.tree.files(`tests/${module.id}`).some((file) => /\.test\.(ts|tsx)$/.test(file)))
      .map((module) => `${module.id} has no tests at tests/${module.id}/: every module has its mirroring test folder`)
  }

  // MARK: - Layer cards

  /** Each layer has `source/<layer>/CLAUDE.md`, whose `**Modules:**` line names its modules -- exactly them -- in backticks. */
  private cards(modules: Placed[]): string[] {
    const found: string[] = []
    for (const layer of LayeringCheck.layers) {
      const packages = new Set(modules.filter((module) => module.layer === layer).map((module) => module.package))
      const card = `source/${layer}/CLAUDE.md`
      const text = this.tree.contents(card)
      if (text === undefined) {
        found.push(`${card} is missing: every layer carries its card`)
        continue
      }
      const line = text.split('\n').find((candidate) => candidate.startsWith('**Modules:**'))
      if (line === undefined) {
        found.push(`${card} has no **Modules:** line`)
        continue
      }
      const named = new Set(line.split('`').filter((_, index) => index % 2 === 1))
      const sameSet = named.size === packages.size && [...named].every((name) => packages.has(name))
      if (!sameSet) {
        const list = (names: Set<string>) => [...names].sort().join(', ') || 'none'
        found.push(`${card} names ${list(named)} but the layer's modules are ${list(packages)}`)
      }
    }
    return found
  }

  // MARK: - Reading a file

  /**
   * Every module specifier a file names, in order, read with TypeScript's own
   * parser: `import`, `export … from`, `import()`, `require`, `import x =
   * require` and `import('…')` types, and the two other ways Vite pulls a file
   * into the build: each pattern of an `import.meta.glob` (its `!` negations
   * left out) and the path of a `new URL('…', import.meta.url)`. A computed
   * `import(`…${x}…`)`, which Vite turns into a glob, is read as its fixed
   * head and `*` (design 1d).
   */
  static importsIn(source: string, fileName: string): string[] {
    if (LayeringCheck.styleExtensions.includes(posix.extname(fileName))) return LayeringCheck.cssImportsIn(source)
    const file = ts.createSourceFile(fileName, source, ts.ScriptTarget.Latest, true, LayeringCheck.scriptKind(fileName))
    const found: string[] = []
    const isText = (node: ts.Node | undefined): node is ts.StringLiteral | ts.NoSubstitutionTemplateLiteral =>
      node !== undefined && (ts.isStringLiteral(node) || ts.isNoSubstitutionTemplateLiteral(node))
    const add = (node: ts.Node | undefined) => {
      if (isText(node)) found.push(node.text)
    }
    const isImportMeta = (node: ts.Node) =>
      ts.isMetaProperty(node) && node.keywordToken === ts.SyntaxKind.ImportKeyword && node.name.text === 'meta'
    const isImportMetaUrl = (node: ts.Node | undefined) =>
      node !== undefined && ts.isPropertyAccessExpression(node) && node.name.text === 'url' && isImportMeta(node.expression)
    const visit = (node: ts.Node): void => {
      if ((ts.isImportDeclaration(node) || ts.isExportDeclaration(node)) && node.moduleSpecifier) {
        add(node.moduleSpecifier)
      } else if (ts.isImportEqualsDeclaration(node) && ts.isExternalModuleReference(node.moduleReference)) {
        add(node.moduleReference.expression)
      } else if (
        ts.isCallExpression(node) &&
        (node.expression.kind === ts.SyntaxKind.ImportKeyword ||
          (ts.isIdentifier(node.expression) && node.expression.text === 'require'))
      ) {
        const first = node.arguments[0]
        // A computed `import(`…${x}…`)` is a glob to Vite: read as its fixed head and `*`.
        if (first && ts.isTemplateExpression(first) && node.expression.kind === ts.SyntaxKind.ImportKeyword) found.push(`${first.head.text}*`)
        else add(first)
      } else if (ts.isImportTypeNode(node) && ts.isLiteralTypeNode(node.argument)) {
        add(node.argument.literal)
      } else if (
        ts.isCallExpression(node) &&
        ts.isPropertyAccessExpression(node.expression) &&
        node.expression.name.text === 'glob' &&
        isImportMeta(node.expression.expression)
      ) {
        const first = node.arguments[0]
        const patterns = first && ts.isArrayLiteralExpression(first) ? first.elements : first ? [first] : []
        for (const pattern of patterns) {
          if (isText(pattern) && !pattern.text.startsWith('!')) found.push(pattern.text)
        }
      } else if (
        ts.isNewExpression(node) &&
        ts.isIdentifier(node.expression) &&
        node.expression.text === 'URL' &&
        node.arguments?.length === 2 &&
        isImportMetaUrl(node.arguments[1])
      ) {
        add(node.arguments[0])
      }
      ts.forEachChild(node, visit)
    }
    visit(file)
    return found
  }

  /**
   * Every path a stylesheet names, in file order (design 1e): each `@import`
   * (a string or `url()`), each other `url()`, and each `image-set()`
   * candidate's path (`-webkit-image-set()` too), a string or a bare path, as
   * Vite reads and inlines it; a `url()` among the candidates is read once,
   * and another function, such as a gradient, loads nothing. Its comments are
   * left out, and a `data:` URI or a bare `#fragment` too, since neither loads
   * a file; a path's own `#fragment` is not part of it. As in an import, a path
   * is relative only when it starts with `./`, `../` or `/`, and anything else
   * names a package.
   */
  static cssImportsIn(source: string): string[] {
    const text = source.replace(/\/\*[\s\S]*?\*\//g, '')
    const found: Array<[number, string]> = []
    const keep = (at: number, raw: string) => {
      const path = raw.trim()
      if (path === '' || path.startsWith('data:') || path.startsWith('#')) return
      found.push([at, LayeringCheck.isRemote(path) ? path : path.replace(/#.*$/, '')])
    }
    const pattern = /@import\s+(?:url\(\s*)?(['"]?)([^'")\s;]+)\1|url\(\s*(['"]?)([^'")]*?)\3\s*\)/g
    for (const match of text.matchAll(pattern)) keep(match.index, match[2] ?? match[4] ?? '')
    // Vite's own reading of an image-set(): its body up to the closing
    // parenthesis (a function inside it one level deep), then each candidate,
    // a function call, a quoted string or a bare path, before its descriptor.
    for (const set of text.matchAll(/image-set\(((?:[\w-]{1,256}\([^)]*\)|[^)])*)\)/g)) {
      const body = set.index + 'image-set('.length
      for (const candidate of (set[1] ?? '').matchAll(/(\s*)([\w-]+\([^)]*\)|"[^"]*"|'[^']*'|[^\s,]+)[^,]*,?/g)) {
        const head = candidate[2] ?? ''
        if (/^[\w-]+\(/.test(head)) continue
        keep(body + candidate.index + (candidate[1] ?? '').length, head.replace(/^(['"])(.*)\1$/, '$2'))
      }
    }
    return found.sort((a, b) => a[0] - b[0]).map(([, path]) => path)
  }

  /**
   * The components a file exports, in order: a capitalized function or class,
   * or a capitalized constant holding a function, a class or a call (`memo`,
   * `forwardRef`), exported where it is declared, by `export { … }` or as the
   * default. Types, interfaces and plain values are not components.
   */
  static exportedComponents(source: string, fileName: string): string[] {
    const file = ts.createSourceFile(fileName, source, ts.ScriptTarget.Latest, true, LayeringCheck.scriptKind(fileName))
    const isName = (name: string) => /^[A-Z]/.test(name)
    const exported = (node: ts.HasModifiers) =>
      ts.getModifiers(node)?.some((modifier) => modifier.kind === ts.SyntaxKind.ExportKeyword) ?? false
    const declared = (statement: ts.Statement): string[] => {
      if (ts.isFunctionDeclaration(statement) || ts.isClassDeclaration(statement)) {
        const name = statement.name?.text
        return name && isName(name) ? [name] : []
      }
      if (ts.isVariableStatement(statement)) {
        return statement.declarationList.declarations.flatMap((declaration) => {
          const initializer = declaration.initializer
          const isComponent =
            initializer !== undefined &&
            (ts.isArrowFunction(initializer) ||
              ts.isFunctionExpression(initializer) ||
              ts.isClassExpression(initializer) ||
              ts.isCallExpression(initializer))
          return ts.isIdentifier(declaration.name) && isName(declaration.name.text) && isComponent ? [declaration.name.text] : []
        })
      }
      return []
    }
    const locals = new Set(file.statements.flatMap(declared))
    const found: string[] = []
    for (const statement of file.statements) {
      if (ts.isFunctionDeclaration(statement) || ts.isClassDeclaration(statement) || ts.isVariableStatement(statement)) {
        if (exported(statement)) found.push(...declared(statement))
      } else if (
        ts.isExportDeclaration(statement) &&
        !statement.moduleSpecifier &&
        statement.exportClause &&
        ts.isNamedExports(statement.exportClause)
      ) {
        for (const element of statement.exportClause.elements) {
          const local = (element.propertyName ?? element.name).text
          // `export { View as default }` is drawn as `<View`, never `<default`.
          if (locals.has(local)) found.push(element.name.text === 'default' ? local : element.name.text)
        }
      } else if (ts.isExportAssignment(statement) && ts.isIdentifier(statement.expression) && locals.has(statement.expression.text)) {
        found.push(statement.expression.text)
      }
    }
    return found
  }

  /** Whether `text` draws the component `name`: `<Name` followed by anything but more of a name. */
  static draws(name: string, text: string): boolean {
    return new RegExp(`<${name.replace(/\$/g, '\\$')}(?![\\w$.])`).test(text)
  }

  /** `react-dom/client` -> `react-dom`; `@react-aria/button/x` -> `@react-aria/button`. */
  static packageName(specifier: string): string {
    const parts = LayeringCheck.withoutQuery(specifier).split('/')
    return (specifier.startsWith('@') ? parts.slice(0, 2) : parts.slice(0, 1)).join('/')
  }

  /** React's own packages and its ecosystem's: what draws. */
  static isReact(name: string): boolean {
    return name === 'react' || name === 'react-dom' || name.startsWith('react-') || name.startsWith('@react-')
  }

  /** A Presentation module that is one of the places: not styles, the shell or the app. */
  static isPlace(module: Placed): boolean {
    return (
      module.layer === 'presentation' &&
      ![LayeringCheck.stylesPackage, LayeringCheck.shellPackage, LayeringCheck.appPackage].includes(module.package)
    )
  }

  static isCode(file: string): boolean {
    return LayeringCheck.codeExtensions.includes(posix.extname(file))
  }

  /** A file whose imports the check reads: code, or a stylesheet (design 1e). */
  static isRead(file: string): boolean {
    return LayeringCheck.isCode(file) || LayeringCheck.styleExtensions.includes(posix.extname(file))
  }

  /** `https://…`, `http://…`, `//…`: a path the browser would fetch from the network. */
  private static isRemote(specifier: string): boolean {
    return /^([a-z][a-z0-9+.-]*:)?\/\//i.test(specifier)
  }

  private static isRelative(specifier: string): boolean {
    return specifier.startsWith('./') || specifier.startsWith('../') || specifier === '.' || specifier === '..' || specifier.startsWith('/')
  }

  /** Vite's `?raw`, `?url` … queries are not part of the path. */
  private static withoutQuery(specifier: string): string {
    const query = specifier.indexOf('?')
    return query < 0 ? specifier : specifier.slice(0, query)
  }

  /** A relative (or root-absolute) specifier as a path from the prototype's root; `undefined` when it leaves it. */
  private static resolve(file: string, specifier: string): string | undefined {
    const path = specifier.startsWith('/')
      ? posix.normalize(specifier.slice(1))
      : posix.normalize(posix.join(posix.dirname(file), specifier))
    return path === '..' || path.startsWith('../') ? undefined : path
  }

  private static scriptKind(fileName: string): ts.ScriptKind {
    switch (posix.extname(fileName)) {
      case '.tsx':
        return ts.ScriptKind.TSX
      case '.jsx':
        return ts.ScriptKind.JSX
      case '.js':
      case '.mjs':
      case '.cjs':
        return ts.ScriptKind.JS
      default:
        return ts.ScriptKind.TS
    }
  }
}
