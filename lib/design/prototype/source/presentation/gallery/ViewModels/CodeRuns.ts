/** A stretch of words, in the code face or not. */
export interface CodeRun {
  readonly text: string
  readonly code: boolean
}

/**
 * Words split into their runs (design 1r): what the Build pages' data writes
 * in backticks (`` `models list` ``) is code, the rest plain, in order. The
 * backticks are the data's, never drawn.
 */
export function codeRuns(words: string): CodeRun[] {
  return words
    .split('`')
    .map((text, index) => ({ text, code: index % 2 === 1 }))
    .filter((run) => run.text !== '')
}
