import { dirname, resolve } from 'node:path'
import { fileURLToPath } from 'node:url'
import react from '@vitejs/plugin-react'
import { defineConfig } from 'vite'
import { viteSingleFile } from 'vite-plugin-singlefile'
import { oneFile } from './tooling/oneFile.ts'

const root = dirname(fileURLToPath(import.meta.url))

// The build writes one self-contained file, lib/design/main.html, with its code
// and styles inlined, so it opens by double-click with no server (the design's
// ADR 0001 §5). Vite's own output goes to dist/, which git ignores; the
// staleness check reads the hash the build writes into main.html.
// APOGEE_DESIGN_OUT names another destination: the determinism test's builds
// write there, never over the committed file.
export default defineConfig({
  root,
  publicDir: false,
  plugins: [
    react(),
    viteSingleFile(),
    oneFile({ root, destination: process.env.APOGEE_DESIGN_OUT ?? resolve(root, '../main.html') }),
  ],
  build: {
    outDir: 'dist',
    emptyOutDir: true,
    rolldownOptions: { input: resolve(root, 'main.html') },
  },
})
