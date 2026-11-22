// ADR 0001 (lib/documentation/adrs/mod/layer-enforcement.md), mechanically: the
// mod's import graph held to its four layers, the engine's `$` nouns held to
// theirs. The CLI's harness.layer_context (lib/src/cli/cmake/modules.cmake),
// the macOS app's tests/architecture/layering and the design prototype's
// TypeScript copy of it are the references; this one reads a TypeScript tree.
//
// The layers are folders: a file is at src/<layer>/<package>/, and where it
// sits is its declaration. The composition root is hooks/register.tsx, above
// every layer; types/ is the contract, the floor every layer takes its types
// from. The rules, each a violation naming what broke it:
//
// - the graph is not empty: src/ holds a file;
// - every file of src/ is at src/<layer>/<package>/, every layer has a
//   package, and every package its tests at tests/<layer>/<package>/;
// - every file of the plugin has a place the law holds: hooks/ holds the
//   composition root alone, types/ holds declarations (.d.ts) alone, and a
//   layer's file is TypeScript, since the engine loads JavaScript too and a
//   file the check cannot read is a file it cannot hold;
// - an import (or a re-export, or a type-level import) points down or
//   sideways: a layer's file imports its own layer, a lower one or the
//   contract; nothing in src/ imports the composition root or the tests; and
//   the contract imports nothing of the mod but itself, so no edge is
//   laundered through it;
// - nothing is imported from outside the mod but 'claude-code', the engine's
//   own module; 'claude-code/testing' is the tests' alone; and no file holds
//   import(), which the engine refuses to load;
// - the engine's nouns stay in their layers: $.process, $.fs, $.env and
//   $.clock (since 1b: time and timers are the host's, as processes are) are
//   Infrastructure's; $.state, $.store and the atom library ('claude-code''s
//   atom, derive, memberOf, read, update, however imported or re-exported,
//   and 'claude-code' taken whole) Data's; $.ui, $.command and $.tool
//   Presentation's (it draws, toasts and registers; Business decides); $.http
//   is no layer's, since the mod is a client of the binary and opens no port;
//   and the composition root and the contract reach no noun themselves. A
//   noun is caught however the engine is named, destructured or indexed: the
//   type checker says whether a property is the engine's.
import { posix } from 'node:path'
import ts from 'typescript'

/** Lowest first: a file may import its own layer or any before it. */
export const LAYERS = ['infrastructure', 'data', 'business', 'presentation']

/** The composition root: wires the layers, holds nothing itself. */
export const ROOT = 'hooks/register.tsx'

/** The contract: declarations only, the floor of every layer. */
const CONTRACT = 'types/'

const TESTS = 'tests/'

/** Each confined noun of the engine's `$`, and the one layer that may reach it. */
export const OWNERS = {
  process: 'infrastructure',
  fs: 'infrastructure',
  env: 'infrastructure',
  clock: 'infrastructure',
  state: 'data',
  store: 'data',
  ui: 'presentation',
  command: 'presentation',
  tool: 'presentation',
}

/** Nouns no layer may reach, and why. */
const REFUSED = { http: 'the mod is a client of the binary and opens no port (the client invariant)' }

/** The state library 'claude-code' exports: Data's, as atoms are. */
const ATOM_LIBRARY = new Set(['atom', 'derive', 'memberOf', 'read', 'update'])

const LAYER_NAME = { infrastructure: 'Infrastructure', data: 'Data', business: 'Business', presentation: 'Presentation' }

const TYPESCRIPT = /\.(ts|tsx|mts|cts)$/

/** A declaration file: all the contract may hold. */
const DECLARATION = /\.d\.(ts|mts|cts)$/

/** Every violation of `{ files, api }`, in a stable order; empty when the mod holds the law. */
export function violations(tree) {
  return check(tree).violations
}

/** The violations, and what was read: the files held to the law and the edges between them. */
export function check({ files, api }) {
  const src = [...files.keys()].filter(path => path.startsWith('src/')).sort()
  if (src.length === 0) {
    return { violations: ['the import graph is empty: src/ holds no file, and an empty graph proves nothing'], files: 0, edges: 0 }
  }
  const found = []
  const placed = new Map()
  for (const path of src) {
    const parts = path.split('/')
    if (parts.length < 4 || !LAYERS.includes(parts[1])) {
      found.push(`${path} is not at src/<layer>/<package>/ with a layer of ${LAYERS.join(', ')}`)
      continue
    }
    placed.set(path, parts[1])
  }
  // Outside src/, the plugin's code has two places: the root, and the contract's declarations.
  const outside = [...files.keys()].filter(path => !path.startsWith('src/') && !path.startsWith(TESTS)).sort()
  for (const path of outside) {
    if (path.startsWith('hooks/') && path !== ROOT) {
      found.push(`${path} is code beside the composition root: ${ROOT} is the hooks module's one file, and code lives at src/<layer>/<package>/`)
    } else if (path.startsWith(CONTRACT) && !DECLARATION.test(path)) {
      found.push(`${path} is code in the contract: ${CONTRACT} holds declarations (.d.ts) only, and code lives at src/<layer>/<package>/`)
    }
  }
  const layers = new Set(placed.values())
  for (const layer of LAYERS) {
    if (!layers.has(layer)) {
      found.push(`${layer} has no package: each layer is a folder src/${layer}/<package>/, and the four layers exist from the first commit`)
    }
  }
  const packages = new Set([...placed.keys()].map(path => path.split('/').slice(1, 3).join('/')))
  for (const one of [...packages].sort()) {
    if (![...files.keys()].some(path => path.startsWith(`${TESTS}${one}/`))) {
      found.push(`src/${one}/ has no tests: they mirror it at ${TESTS}${one}/`)
    }
  }

  const contract = outside.filter(path => path.startsWith(CONTRACT) && DECLARATION.test(path))
  const held = [...placed.keys(), ...(files.has(ROOT) ? [ROOT] : []), ...contract]
  const program = programOf(files, api)
  const checker = program.getTypeChecker()
  let read = 0
  let edges = 0
  for (const path of held) {
    const source = program.getSourceFile(virtual(path))
    if (source === undefined) {
      found.push(`${path} is not TypeScript: the engine loads it, but the layering check reads only .ts, .tsx, .mts and .cts files, and a file it cannot read is a file it cannot hold`)
      continue
    }
    const imports = importsOf(path, source, files, placed)
    read += 1
    edges += imports.edges
    found.push(...imports.found, ...nounsOf(path, source, checker, placed))
  }

  return { violations: found, files: read, edges }
}

// -- The import graph ---------------------------------------------------------

function importsOf(path, source, files, placed) {
  const found = []
  let edges = 0
  const edge = (node, specifier) => {
    edges += 1
    found.push(...judgeEdge(path, specifier, lineOf(source, node), files, placed))
  }
  const visit = node => {
    if ((ts.isImportDeclaration(node) || ts.isExportDeclaration(node)) && node.moduleSpecifier && ts.isStringLiteral(node.moduleSpecifier)) {
      edge(node, node.moduleSpecifier.text)
      if (node.moduleSpecifier.text === 'claude-code' && placed.get(path) !== 'data') {
        found.push(...atomImports(path, node, lineOf(source, node)))
      }
    } else if (ts.isCallExpression(node) && node.expression.kind === ts.SyntaxKind.ImportKeyword) {
      found.push(`${path}:${lineOf(source, node)} holds import(), which the engine refuses to load`)
    } else if (ts.isImportTypeNode(node) && ts.isLiteralTypeNode(node.argument) && ts.isStringLiteral(node.argument.literal)) {
      edge(node, node.argument.literal.text)
    }
    ts.forEachChild(node, visit)
  }
  visit(source)

  return { found, edges }
}

function judgeEdge(path, specifier, line, files, placed) {
  const at = `${path}:${line}`
  if (specifier === 'claude-code') return []
  if (specifier === 'claude-code/testing') return [`${at} imports 'claude-code/testing': the test kit is the tests' alone`]
  const target = specifier.startsWith('./') || specifier.startsWith('../') ? resolve(path, specifier, files) : undefined
  if (target === undefined) {
    return [`undeclared edge: ${at} imports '${specifier}', which is neither a file of the mod nor 'claude-code'`]
  }
  if (path.startsWith(CONTRACT) && !target.startsWith(CONTRACT)) return [`${at} imports ${target}: the contract imports nothing of the mod but itself`]
  if (target.startsWith(CONTRACT)) return []
  if (target.startsWith(TESTS)) return [`${at} imports ${target}: the plugin never imports its tests`]
  if (path === ROOT) return []
  const from = placed.get(path)
  if (target.startsWith('hooks/')) {
    return [`upward edge: ${at} (${from}) imports ${target} (the composition root) -- dependencies point down`]
  }
  const to = placed.get(target)
  if (to === undefined) return []
  if (LAYERS.indexOf(to) > LAYERS.indexOf(from)) {
    return [`upward edge: ${at} (${from}) imports ${target} (${to}) -- dependencies point down`]
  }

  return []
}

/** The file a relative specifier lands on, as the bundler resolution of the mod's tsconfig finds it; none outside the mod. */
function resolve(path, specifier, files) {
  const base = posix.normalize(posix.join(posix.dirname(path), specifier))
  if (base.startsWith('../') || base === '..') return undefined
  const tries = [base, ...['.ts', '.tsx', '.d.ts', '.mts', '.cts', '.js', '.mjs'].map(ending => base + ending)]
  tries.push(...['index.ts', 'index.tsx', 'index.d.ts'].map(index => `${base}/${index}`))

  return tries.find(one => files.has(one))
}

/**
 * The atom library taken from 'claude-code' outside Data, as values: named in
 * an import or a re-export, or with the whole module (`* as`, `export *`).
 */
function atomImports(path, node, line) {
  const whole = [`${path}:${line} takes all of 'claude-code', the atom library with it: atoms are Data's alone (ADR 0001)`]
  if (ts.isExportDeclaration(node)) {
    if (node.isTypeOnly) return []
    if (node.exportClause === undefined || ts.isNamespaceExport(node.exportClause)) return whole

    return atomsNamed(path, line, node.exportClause.elements, 're-exports')
  }
  const clause = node.importClause
  if (clause === undefined || clause.isTypeOnly || clause.namedBindings === undefined) return []
  if (ts.isNamespaceImport(clause.namedBindings)) return whole

  return atomsNamed(path, line, clause.namedBindings.elements, 'imports')
}

function atomsNamed(path, line, elements, verb) {
  return elements
    .filter(element => !element.isTypeOnly && ATOM_LIBRARY.has((element.propertyName ?? element.name).text))
    .map(element => `${path}:${line} ${verb} ${(element.propertyName ?? element.name).text} from 'claude-code': atoms are Data's alone (ADR 0001)`)
}

// -- The engine's nouns -------------------------------------------------------

function nounsOf(path, source, checker, placed) {
  const layer = placed.get(path)
  const found = []
  const judge = (node, noun) => {
    const at = `${path}:${lineOf(source, node)}`
    if (Object.hasOwn(REFUSED, noun)) {
      found.push(`${at} uses $.${noun}: ${REFUSED[noun]}`)
    } else if (path.startsWith(CONTRACT)) {
      found.push(`${at} uses $.${noun}: the contract holds declarations only (ADR 0001)`)
    } else if (layer === undefined) {
      found.push(`${at} uses $.${noun}: the composition root wires the layers and holds nothing itself (ADR 0001)`)
    } else if (OWNERS[noun] !== layer) {
      found.push(`${at} uses $.${noun}, which is ${LAYER_NAME[OWNERS[noun]]}'s alone (ADR 0001)`)
    }
  }
  const visit = node => {
    if (ts.isPropertyAccessExpression(node) && isNoun(node.name.text) && isEngine(checker, node.expression, node.name.text)) {
      judge(node, node.name.text)
    } else if (
      ts.isElementAccessExpression(node) &&
      ts.isStringLiteralLike(node.argumentExpression) &&
      isNoun(node.argumentExpression.text) &&
      isEngine(checker, node.expression, node.argumentExpression.text)
    ) {
      judge(node, node.argumentExpression.text)
    } else if (ts.isBindingElement(node) && ts.isObjectBindingPattern(node.parent)) {
      const key = node.propertyName ?? node.name
      const noun = ts.isIdentifier(key) || ts.isStringLiteralLike(key) ? key.text : undefined
      if (noun !== undefined && isNoun(noun) && isEngineBinding(checker, node.parent, noun)) judge(node, noun)
    }
    ts.forEachChild(node, visit)
  }
  visit(source)

  return found
}

function isNoun(name) {
  return Object.hasOwn(OWNERS, name) || Object.hasOwn(REFUSED, name)
}

/** Whether `expression.noun` is the engine's noun: spelled `$`, or typed as the engine is. */
function isEngine(checker, expression, noun) {
  if (ts.isIdentifier(expression) && expression.text === '$') return true

  return isEngineProperty(checker.getTypeAtLocation(expression).getProperty(noun))
}

/** Whether `{ noun } = <the engine>`: the destructured value is spelled `$` or typed as the engine. */
function isEngineBinding(checker, pattern, noun) {
  const holder = pattern.parent
  if (ts.isVariableDeclaration(holder) && holder.initializer && ts.isIdentifier(holder.initializer) && holder.initializer.text === '$') {
    return true
  }

  return isEngineProperty(checker.getTypeAtLocation(pattern).getProperty(noun))
}

/** A property the engine's interface declares, as the API declarations spell it. */
function isEngineProperty(symbol) {
  return (symbol?.declarations ?? []).some(
    declaration =>
      ts.isPropertySignature(declaration) &&
      ts.isInterfaceDeclaration(declaration.parent) &&
      declaration.parent.name.text === 'CoreEngineInterface',
  )
}

function lineOf(source, node) {
  return source.getLineAndCharacterOfPosition(node.getStart(source)).line + 1
}

// -- The program ----------------------------------------------------------------

/** The tree is served from a folder of its own, so a planted tree never reads the disk's copy. */
const VIRTUAL = '/__apogee_mod__/'

function virtual(path) {
  return VIRTUAL + path
}

/** The mod's own compiler options (lib/src/mod/tsconfig.json), as far as reading symbols needs them. */
const OPTIONS = {
  target: ts.ScriptTarget.ES2023,
  lib: ['lib.es2023.d.ts'],
  types: [],
  module: ts.ModuleKind.ESNext,
  moduleResolution: ts.ModuleResolutionKind.Bundler,
  strict: true,
  noEmit: true,
  skipLibCheck: true,
  jsx: ts.JsxEmit.React,
  jsxFactory: 'h',
  jsxFragmentFactory: 'Fragment',
}

/** The standard library and the API declarations, parsed once for every program. */
const parsed = new Map()

function programOf(files, api) {
  const base = ts.createCompilerHost(OPTIONS, true)
  const inTree = name => name.startsWith(VIRTUAL)
  const host = {
    ...base,
    getCurrentDirectory: () => VIRTUAL,
    fileExists: name => (inTree(name) ? files.has(name.slice(VIRTUAL.length)) : base.fileExists(name)),
    readFile: name => (inTree(name) ? files.get(name.slice(VIRTUAL.length)) : base.readFile(name)),
    directoryExists: name =>
      inTree(`${name}/`) || name === VIRTUAL.slice(0, -1)
        ? [...files.keys()].some(path => virtual(path).startsWith(`${name}/`))
        : (base.directoryExists?.(name) ?? true),
    getSourceFile: (name, language) => {
      if (inTree(name)) {
        const text = files.get(name.slice(VIRTUAL.length))

        return text === undefined ? undefined : ts.createSourceFile(name, text, language, true)
      }
      if (!parsed.has(name)) parsed.set(name, base.getSourceFile(name, language))

      return parsed.get(name)
    },
    writeFile: () => {},
  }
  const roots = [...files.keys()].filter(path => TYPESCRIPT.test(path)).map(virtual)

  return ts.createProgram({ rootNames: [...roots, api], options: OPTIONS, host })
}
