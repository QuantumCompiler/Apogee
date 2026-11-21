import type { CSSProperties } from 'react'
import { Fragment } from 'react'
import type { ColorName } from '../../styles/Tokens/Tokens'
import { codeRuns } from '../ViewModels/CodeRuns'

/** A token's variable: `status.accent` is `var(--status-accent)`. */
export function paint(color: ColorName): string {
  return `var(--${color.replace('.', '-')})`
}

/**
 * Words with their inline code (design 1r): each stretch the data writes in
 * backticks in the code face, the one span the Build pages draw code with --
 * a little smaller than the words around it, as the old boards' `.mono` was,
 * unless `code` says how.
 */
export function CodeText({ text, code }: { readonly text: string; readonly code?: CSSProperties }) {
  return (
    <>
      {codeRuns(text).map((run, index) =>
        run.code ? (
          <span key={index} className="mono" style={code ?? { fontSize: '.92em' }}>
            {run.text}
          </span>
        ) : (
          <Fragment key={index}>{run.text}</Fragment>
        ),
      )}
    </>
  )
}
