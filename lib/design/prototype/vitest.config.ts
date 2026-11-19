import { configDefaults, defineConfig } from 'vitest/config'

// Three projects, so each npm script runs its own share once (DEVELOPER.md):
// `test` every *.test.ts(x) in the prototype outside tests/architecture/ and
// tests/bundle/ (the modules' own tests under tests/<layer>/<module>/, and a
// script's tests beside it), `layering` the architecture check, and `stale`
// the built file's checks.
export default defineConfig({
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
