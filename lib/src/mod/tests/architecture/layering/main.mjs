// `npm run layering`: the layering check over the mod as it is on disk. Exits 1
// naming every violation, 0 with what it read.
import { check } from './check.mjs'
import { readMod } from './tree.mjs'

const { violations, files, edges } = check(readMod())
if (violations.length > 0) {
  for (const violation of violations) console.error(`layering: ${violation}`)
  console.error(`layering: ${violations.length} violation(s) of ADR 0001`)
  process.exit(1)
}
console.log(`layering: ${files} files and ${edges} imports hold ADR 0001`)
