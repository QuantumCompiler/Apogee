// .github/workflows/mod.yml, the mod's pipeline (the mod's 1d), proven where
// it can be off GitHub: the file parsed as YAML (by ruby's own parser, Psych,
// which macOS and GitHub's Ubuntu image both carry -- the mod's toolchain has
// no YAML package), its shape held to the house's required-names pattern, and
// two of its steps run for real: the gate step over throwaway commits in a
// `git clone --shared --no-checkout` under $TMPDIR, with the latest release
// stood in for, and the claude CLI's install step over a stand-in npm. Nothing
// is written to this checkout or its history. Run by node (npm run test:node).
import assert from 'node:assert/strict'
import { chmodSync, copyFileSync, mkdirSync, readFileSync, writeFileSync, existsSync } from 'node:fs'
import { join } from 'node:path'
import { after, before, test } from 'node:test'

import { MOD_ROOT, childEnv, git, run, scratch } from './support.mjs'

const REPO = git(['rev-parse', '--show-toplevel'], MOD_ROOT)
const WORKFLOWS = join(REPO, '.github', 'workflows')

/** A workflow file as data, parsed by Psych; YAML 1.1 reads the key `on` as true. */
function parsed(file) {
  let ran
  try {
    ran = run('ruby', ['-ryaml', '-rjson', '-e', 'print JSON.generate(YAML.safe_load(File.read(ARGV[0])))', join(WORKFLOWS, file)])
  } catch (error) {
    throw new Error(`ruby could not be run (${error.code}): these tests parse the workflows with ruby's YAML`)
  }
  assert.equal(ran.status, 0, `${file} does not parse: ${ran.stderr}`)
  const workflow = JSON.parse(ran.stdout)
  workflow.on ??= workflow.true

  return workflow
}

const CHANGED = "needs.changes.outputs.mod == 'true'"
const UNCHANGED = "needs.changes.outputs.mod != 'true'"

let mod
let build

before(() => {
  mod = parsed('mod.yml')
  build = mod.jobs.build
})

test('mod.yml runs on a pull request into stable, or on request, and reads the repository alone', () => {
  assert.deepEqual(Object.keys(mod.on).sort(), ['pull_request', 'workflow_dispatch'])
  assert.deepEqual(mod.on.pull_request, { branches: ['stable'] })
  assert.deepEqual(mod.permissions, { contents: 'read' })
})

test('its two jobs are named its own: what changed (mod) and build mod', () => {
  assert.deepEqual(Object.keys(mod.jobs), ['changes', 'build'])
  assert.equal(mod.jobs.changes.name, 'what changed (mod)')
  assert.equal(build.name, 'build mod')
})

test('the gate asks changed.sh mod from the latest release, on the whole history, and hands its answer on', () => {
  const changes = mod.jobs.changes
  assert.equal(changes['runs-on'], 'ubuntu-24.04')
  assert.deepEqual(changes.outputs, { mod: '${{ steps.diff.outputs.mod }}' })
  const checkout = changes.steps.find(step => step.uses?.startsWith('actions/checkout@'))
  assert.deepEqual(checkout.with, { 'fetch-depth': 0 })
  const diff = changes.steps.find(step => step.id === 'diff')
  assert.match(diff.run, /found="\$\(lib\/scripts\/latest-release\.sh\)"/)
  assert.match(diff.run, /lib\/scripts\/changed\.sh mod "\$commit" HEAD/)
})

test('build mod has no job-level if, so it always reports under its name: every step is the gate\'s but the one saying unchanged', () => {
  assert.equal('if' in build, false)
  assert.equal(build.needs, 'changes')
  assert.equal(build['runs-on'], 'ubuntu-24.04')
  const last = build.steps.at(-1)
  assert.equal(last.if, UNCHANGED)
  assert.equal(last.run.trim(), 'echo "the mod is unchanged since the latest release -- nothing to build"')
  for (const step of build.steps.slice(0, -1)) assert.equal(step.if, CHANGED, step.name ?? step.uses)
})

test("one build path: the job runs the mod's own npm scripts, in the order a developer does, and the install alone besides", () => {
  const scripts = JSON.parse(readFileSync(join(MOD_ROOT, 'package.json'), 'utf8')).scripts
  const runs = build.steps.filter(step => step.run !== undefined && step.if === CHANGED)
  const inMod = runs.filter(step => step['working-directory'] === 'lib/src/mod').map(step => step.run.trim())
  assert.deepEqual(inMod, ['npm ci', 'npm run typecheck', 'npm run layering', 'npm run test:node', 'npm run package', 'npm run validate:package'])
  for (const command of inMod.slice(1)) assert.ok(command.slice('npm run '.length) in scripts, command)
  const besides = runs.filter(step => step['working-directory'] !== 'lib/src/mod')
  assert.deepEqual(besides.map(step => step.id), ['claude'])
})

test("the job's node is the one package.json's engines name", () => {
  const setup = build.steps.find(step => step.uses?.startsWith('actions/setup-node@'))
  assert.deepEqual(setup.with, { 'node-version-file': 'lib/src/mod/package.json' })
  assert.equal(JSON.parse(readFileSync(join(MOD_ROOT, 'package.json'), 'utf8')).engines.node, '26.x')
})

test('validate is handed the claude the install step put in place, and the artifact is the zip it validated: apogee-mod', () => {
  const at = build.steps.findIndex(step => step.run?.trim() === 'npm run validate:package')
  const validate = build.steps[at]
  assert.equal(validate.env.CLAUDE, '${{ runner.temp }}/claude-code/node_modules/.bin/claude')
  assert.equal(validate.env.CLAUDE_CODE_DISABLE_NONESSENTIAL_TRAFFIC, '1')
  const install = build.steps.findIndex(step => step.id === 'claude')
  assert.ok(install < at, 'the install comes before validate')
  const upload = build.steps.findIndex(step => step.uses?.startsWith('actions/upload-artifact@'))
  assert.ok(upload > at, 'the zip is kept after it is validated')
  assert.deepEqual(build.steps[upload].with, { name: 'apogee-mod', path: 'lib/src/mod/build/apogee-mod.zip', 'if-no-files-found': 'error' })
})

// --- The steps, run. ---------------------------------------------------------

let space

before(() => {
  space = scratch('apogee-workflow-')
})

after(() => {
  space?.remove()
})

/** A step's script run as GitHub runs a `run:` (bash --noprofile --norc -eo pipefail), in `cwd`. */
function runStep(step, cwd, env) {
  return run('bash', ['--noprofile', '--norc', '-eo', 'pipefail', '-c', step.run], { cwd, env: childEnv(env) })
}

/** A stand-in npm on a PATH of its own: an install that fails, or one that lays a claude at the prefix. */
function standInNpm(name, installs) {
  const bin = join(space.folder, name, 'bin')
  mkdirSync(bin, { recursive: true })
  const lay = [
    'prefix=""',
    'while [ $# -gt 0 ]; do [ "$1" = "--prefix" ] && prefix="$2"; shift; done',
    'mkdir -p "$prefix/node_modules/.bin"',
    `printf '#!/bin/sh\\necho "2.1.326 (Claude Code)"\\n' > "$prefix/node_modules/.bin/claude"`,
    'chmod 755 "$prefix/node_modules/.bin/claude"',
    'exit 0',
  ]
  writeFileSync(join(bin, 'npm'), ['#!/bin/sh', 'echo "npm $*"', ...(installs ? lay : ['echo "npm error 404 Not Found" >&2', 'exit 1']), ''].join('\n'))
  chmodSync(join(bin, 'npm'), 0o755)

  return `${bin}:/usr/bin:/bin`
}

test('the claude CLI that installs is put where validate looks for it', () => {
  const step = build.steps.find(one => one.id === 'claude')
  const temp = join(space.folder, 'installs', 'runner-temp')
  mkdirSync(temp, { recursive: true })
  const ran = runStep(step, space.folder, { PATH: standInNpm('installs', true), RUNNER_TEMP: temp })
  assert.equal(ran.status, 0, ran.stderr)
  assert.match(ran.stdout, /^npm install --prefix \S+ --no-audit --no-fund @anthropic-ai\/claude-code@latest$/m)
  assert.equal(ran.stdout.trim().split('\n').at(-1), `the claude CLI installed into ${temp}/claude-code`)
  assert.equal(existsSync(join(temp, 'claude-code', 'node_modules', '.bin', 'claude')), true)
})

test('a claude CLI that will not install never fails the job: it says so, and validate says the skip', () => {
  const step = build.steps.find(one => one.id === 'claude')
  const temp = join(space.folder, 'refuses', 'runner-temp')
  mkdirSync(temp, { recursive: true })
  const ran = runStep(step, space.folder, { PATH: standInNpm('refuses', false), RUNNER_TEMP: temp })
  assert.equal(ran.status, 0, ran.stderr)
  assert.equal(ran.stdout.trim().split('\n').at(-1), 'the claude CLI did not install headlessly (npm exited 1): validate below says the skip')
})

let clone
let base

/** A commit on `base` that adds one file, made in the clone's own index. */
function commitAdding(path, message) {
  git(['read-tree', base], clone)
  const blob = git(['hash-object', '-w', '--stdin'], clone, `${message}\n`)
  git(['update-index', '--add', '--cacheinfo', `100644,${blob},${path}`], clone)
  const tree = git(['write-tree'], clone)

  return git(['-c', 'user.name=mod.yml test', '-c', 'user.email=mod-yml@test.invalid', 'commit-tree', tree, '-p', base, '-m', message], clone)
}

/** The gate step's answer, as it lands in $GITHUB_OUTPUT, with HEAD at `head`, the latest release at `released`. */
function gate({ head, released = base, event = 'pull_request' }) {
  git(['update-ref', '--no-deref', 'HEAD', head], clone)
  writeFileSync(join(clone, 'lib', 'scripts', 'latest-release.sh'), `#!/bin/sh\n${released === '' ? 'exit 0' : `echo "v0.2.0 ${released}"`}\n`)
  const output = join(space.folder, 'output')
  writeFileSync(output, '')
  const step = mod.jobs.changes.steps.find(one => one.id === 'diff')
  const ran = runStep(step, clone, { GITHUB_OUTPUT: output, EVENT: event })
  assert.equal(ran.status, 0, ran.stderr)

  return readFileSync(output, 'utf8').trim()
}

let modOnly
let docsOnly
let workflowOnly

before(() => {
  clone = join(space.folder, 'repo')
  git(['clone', '--quiet', '--shared', '--no-checkout', REPO, clone], space.folder)
  base = git(['rev-parse', 'HEAD'], clone)
  modOnly = commitAdding('lib/src/mod/src/data/state/probe.ts', 'a mod-only change')
  docsOnly = commitAdding('lib/documentation/assistant/mod/probe.md', 'a docs-only change')
  workflowOnly = commitAdding('.github/workflows/mod.yml', 'the workflow changed')
  // The gate's two scripts, laid in the clone's empty work tree: the working
  // tree's changed.sh, and a latest-release.sh standing in for GitHub's answer.
  mkdirSync(join(clone, 'lib', 'scripts'), { recursive: true })
  copyFileSync(join(REPO, 'lib', 'scripts', 'changed.sh'), join(clone, 'lib', 'scripts', 'changed.sh'))
  chmodSync(join(clone, 'lib', 'scripts', 'changed.sh'), 0o755)
  writeFileSync(join(clone, 'lib', 'scripts', 'latest-release.sh'), '')
  chmodSync(join(clone, 'lib', 'scripts', 'latest-release.sh'), 0o755)
})

test('the gate step, run: a mod-only pull request builds the mod', () => {
  assert.equal(gate({ head: modOnly }), 'mod=true')
})

test('the gate step, run: a docs-only pull request builds nothing, and build mod reports skipped steps under its name', () => {
  assert.equal(gate({ head: docsOnly }), 'mod=false')
})

test('the gate step, run: a change to mod.yml alone builds the mod', () => {
  assert.equal(gate({ head: workflowOnly }), 'mod=true')
})

test('the gate step, run: with nothing released yet, the mod builds', () => {
  assert.equal(gate({ head: docsOnly, released: '' }), 'mod=true')
})

test('the gate step, run: on request, everything runs', () => {
  assert.equal(gate({ head: docsOnly, event: 'workflow_dispatch' }), 'mod=true')
})
