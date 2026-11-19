import { dirname } from 'node:path'
import { fileURLToPath } from 'node:url'
import { configDefaults, defineConfig } from 'vitest/config'
import { recordedAlias, recordedRoot } from './tooling/sources.ts'
import { tokens } from './tooling/tokens.ts'

const root = dirname(fileURLToPath(import.meta.url))

// Three projects, so each npm script runs its own share once (DEVELOPER.md):
// `test` every *.test.ts(x) in the prototype outside tests/architecture/ and
// tests/bundle/ (the modules' own tests under tests/<layer>/<module>/, and a
// script's tests beside it), `layering` the architecture check, and `stale`
// the built file's checks. A test imports a recording as a preset does,
// through vite.config.ts's `@recorded` alias (design 1d), and the styles read
// the tokens through its `@tokens` plugin (design 1e).
export default defineConfig({
  plugins: [tokens({ root })],
  resolve: { alias: { [recordedAlias]: recordedRoot(root) } },
  test: {
    environment: 'node',
    projects: [
      {
        test: {
          name: 'unit',
          include: ['**/*.test.{ts,tsx}'],
          exclude: [...configDefaults.exclude, 'tests/architecture/**', 'tests/bundle/**'],
        },
      },
      {
        test: {
          name: 'layering',
          include: ['tests/architecture/**/*.test.ts'],
        },
      },
      {
        test: {
          name: 'bundle',
          include: ['tests/bundle/**/*.test.ts'],
          // The determinism test runs two production builds.
          testTimeout: 180_000,
        },
      },
    ],
  },
})
