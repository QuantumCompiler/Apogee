// The not-installed guard, the scaffold's one visible behavior: loaded with no
// binary, the mod says so once, with the install hint, and never crashes. This
// stands in for the live look (`claude --plugin-dir lib/src/mod`), which is the
// person's to take.
import { expect, test } from 'claude-code/testing'

import { BIN, START, machine } from '../../support/machine'

const INSTALL =
  'Install Apogee with: curl -fsSL https://raw.githubusercontent.com/QuantumCompiler/Apogee/stable/lib/scripts/install.sh | bash'

test('no binary: the condition is said with every place looked and the install hint', async ($, on) => {
  const made = machine(on, {})

  await $.session.start(START)

  expect(made.toasts).toEqual([
    {
      text:
        'Apogee is not installed where the mod looks: ~/.local/bin/apogee (not installed), ' +
        '~/.local/bin/apogee-dev (not installed), ~/.local/bin/apogee-test (not installed). ' +
        INSTALL,
      timeoutMs: 10_000,
    },
  ])
})

test('a binary passed over is said with its reason', { options: { binary: '/opt/tools/apogee' } }, async ($, on) => {
  const made = machine(on, { [`${BIN}/apogee`]: { exitCode: 1, stderr: 'error: broken install\n' } })

  await $.session.start(START)

  expect(made.toasts.map(toast => toast.text)).toEqual([
    'Apogee is not installed where the mod looks: /opt/tools/apogee (not installed), ' +
      '~/.local/bin/apogee (`version` exited with status 1: error: broken install), ' +
      '~/.local/bin/apogee-dev (not installed), ~/.local/bin/apogee-test (not installed). ' +
      INSTALL,
  ])
})

test('a place that cannot be looked at is said once with why, the absence said all the same', { options: { binary: '/net/share/apogee' } }, async ($, on) => {
  const made = machine(on, { '/net/share/apogee': { unreadable: 'network location' } })

  await $.session.start(START)

  expect(made.toasts.map(toast => toast.text)).toEqual([
    expect.stringMatching(
      /^Apogee is not installed where the mod looks: \/net\/share\/apogee \(could not be looked at: .*network location\), ~\/\.local\/bin\/apogee \(not installed\), /,
    ),
  ])
  expect(made.toasts[0]?.text.endsWith(INSTALL)).toBe(true)
})

test("the dev channel's absence names the dev channel's install", { options: { channel: 'dev' } }, async ($, on) => {
  const made = machine(on, {})

  await $.session.start(START)

  expect(made.toasts.map(toast => toast.text)).toEqual([
    'apogee-dev is not installed where the mod looks: ~/.local/bin/apogee-dev (not installed). ' +
      'Install the dev channel with: make -C lib/src/cli install MODE=dev',
  ])
})

test('the session starts whatever discovery meets: the start is answered as the engine answered it', async ($, on) => {
  machine(on, { [`${BIN}/apogee`]: { refuses: 'permission denied' } })

  expect(await $.session.start({ ...START, cwd: '/work' })).toEqual({ cwd: '/work' })
})
