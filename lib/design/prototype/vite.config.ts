import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'
import { viteSingleFile } from 'vite-plugin-singlefile'
import { oneFile } from './tooling/oneFile.ts'
import { recordedAlias, recordedRoot } from './tooling/sources.ts'
import { tokens } from './tooling/tokens.ts'

const root = dirname(fileURLToPath(import.meta.url))

// The build writes one self-contained file, lib/design/main.html, with its code
// and styles inlined, so it opens by double-click with no server (the design's
// ADR 0001 §5). Vite's own output goes to dist/, which git ignores; the
// staleness check reads the hash the build writes into main.html.
// APOGEE_DESIGN_OUT names another destination: the determinism test's builds
// write there, never over the committed file.
// `@recorded` reaches the Mac app's recordings in place (design 1d): each is
// imported by name with `?raw` and inlined, and the dev server may read them.
// `@tokens` is the design's tokens, generated from tokens.json at build time
// (design 1e). CSS is not minified, so the built file carries the generated
// custom properties as tokens.json writes them (a minifier rewrites #FFFFFF as
// #fff and rgba() as hex), and the token check reads it value for value.
export default defineConfig({
  root,
  publicDir: false,
  resolve: { alias: { [recordedAlias]: recordedRoot(root) } },
  server: { fs: { allow: [root, recordedRoot(root)] } },
  plugins: [
    tokens({ root }),
    react(),
    viteSingleFile(),
    oneFile({ root, destination: process.env.APOGEE_DESIGN_OUT ?? resolve(root, '../main.html') }),
  ],
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    cssMinify: false,
    rolldownOptions: { input: resolve(root, 'main.html') },
  },
})
