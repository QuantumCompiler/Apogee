import { createContext, useContext, useMemo, useState } from 'react'
import type { CSSProperties, ReactNode } from 'react'
import { CodeLexer } from './CodeLexer'
import { MarkdownDocument, MarkdownLink } from './MarkdownDocument'
import type { MarkdownAlignment, MarkdownBlock, MarkdownSpan } from './MarkdownDocument'

/**
 * A model's markdown drawn in the design's terms (design 1u): the Mac app's
 * `MarkdownText` (`PresentationStyles`), as the old prototype draws an
 * answer (`main.html:286-299` and `:611-626` at `b375fac9`) -- prose with
 * emphasis, strong, links and inline code; headings by the type scale;
 * lists, nested; quotes; code as a bordered card on `surface.code` under its
 * language's caption with copy, coloured by `CodeLexer` in the five code
 * colours; rules; tables. No new package: `MarkdownDocument` reads it.
 *
 * A link is followed only for http, https and mailto; any other is drawn and
 * not followed. An image is drawn as its alt text and its address, and never
 * fetched: no `<img>`, so an answer asks nothing of the network.
 *
 * With `citations` (design 1v), a `[n]` in the prose for a source the turn
 * drew on, 1 to `count`, is a button that opens it -- the old prototype's
 * citation (`main.html:457` at `b375fac9`); any other bracket stays text.
 */
export function MarkdownText({ text, streaming = false, citations }: { readonly text: string; readonly streaming?: boolean; readonly citations?: Citations }) {
  const document = useMemo(() => MarkdownDocument.parse(text), [text])
  return (
    <Cited.Provider value={citations}>
      <div className="prose" data-streaming={streaming ? '' : undefined} style={{ minWidth: 0, overflowWrap: 'anywhere' }}>
        <Blocks blocks={document.blocks} />
      </div>
    </Cited.Provider>
  )
}

/** The sources an answer may cite: how many, which is open, what opens one, and each one's name for the ear. */
export interface Citations {
  readonly count: number
  readonly opened: number | undefined
  readonly open: (index: number) => void
  readonly label: (index: number) => string
}

const Cited = createContext<Citations | undefined>(undefined)

/** Plain text, its `[n]` citations drawn as buttons where the answer has sources. */
function CitedText({ text }: { readonly text: string }) {
  const citations = useContext(Cited)
  if (!citations || citations.count === 0) return text
  const parts = text.split(/(\[\d+\])/)
  return parts.map((part, index) => {
    const n = /^\[(\d+)\]$/.exec(part)?.[1]
    const number = n === undefined ? 0 : Number(n)
    if (number < 1 || number > citations.count) return part
    return (
      <button
        key={index}
        type="button"
        className="mono"
        onClick={() => citations.open(number)}
        aria-label={citations.label(number)}
        aria-expanded={citations.opened === number}
        style={{ display: 'inline-flex', alignItems: 'center', height: 17, padding: '0 5px', margin: '0 2px', border: 0, borderRadius: 4, background: 'var(--surface-control)', color: 'var(--status-accent)', fontSize: 'var(--t-label)', verticalAlign: 1 }}
      >
        {number}
      </button>
    )
  })
}

function Blocks({ blocks }: { readonly blocks: readonly MarkdownBlock[] }) {
  return blocks.map((block, index) => <Block key={index} block={block} last={index === blocks.length - 1} />)
}

const headingSize = (level: number) => (level <= 1 ? 'var(--t-h1)' : level === 2 ? 'var(--t-h2)' : 'var(--t-h3)')

function Block({ block, last }: { readonly block: MarkdownBlock; readonly last: boolean }) {
  const below = last ? 0 : 10
  switch (block.case) {
    case 'paragraph':
      return (
        <p style={{ margin: `0 0 ${below}px` }}>
          <Spans spans={block.spans} />
        </p>
      )
    case 'heading': {
      const style: CSSProperties = { margin: `0 0 ${last ? 0 : 6}px`, fontSize: headingSize(block.level), fontWeight: 600 }
      const spans = <Spans spans={block.spans} />
      // The window's own title is the page's h1: an answer's headings sit under it.
      return block.level <= 2 ? <h2 style={style}>{spans}</h2> : <h3 style={style}>{spans}</h3>
    }
    case 'list': {
      const style: CSSProperties = { margin: `0 0 ${below}px`, paddingLeft: 22 }
      const items = block.items.map((item, index) => (
        <li key={index} style={{ margin: '2px 0' }}>
          <Blocks blocks={item.blocks} />
        </li>
      ))
      return block.ordered ? (
        <ol start={block.start} style={style}>
          {items}
        </ol>
      ) : (
        <ul style={style}>{items}</ul>
      )
    }
    case 'quote':
      return (
        <blockquote style={{ margin: `0 0 ${below}px`, padding: '2px 0 2px 12px', borderLeft: '2px solid var(--line-strong)', color: 'var(--text-secondary)' }}>
          <Blocks blocks={block.blocks} />
        </blockquote>
      )
    case 'code':
      return <CodeBlock language={block.language} text={block.text} last={last} />
    case 'rule':
      return <hr style={{ margin: `0 0 ${below}px`, border: 0, borderTop: '1px solid var(--line-border)' }} />
    case 'table':
      return (
        <div style={{ overflowX: 'auto', margin: `0 0 ${last ? 0 : 14}px` }}>
          <table style={{ borderCollapse: 'collapse', fontSize: 'var(--t-dense)', minWidth: 360 }}>
            <thead>
              <tr>
                {block.header.map((cell, column) => (
                  <th key={column} scope="col" style={{ padding: '6px 16px 6px 0', borderBottom: '1px solid var(--line-strong)', textAlign: textAlign(block.align[column]), fontWeight: 600 }}>
                    <Spans spans={cell} />
                  </th>
                ))}
              </tr>
            </thead>
            <tbody>
              {block.rows.map((row, index) => (
                <tr key={index}>
                  {row.map((cell, column) => (
                    <td
                      key={column}
                      style={{ padding: '6px 16px 6px 0', borderBottom: index === block.rows.length - 1 ? undefined : '1px solid var(--line-border)', textAlign: textAlign(block.align[column]) }}
                    >
                      <Spans spans={cell} />
                    </td>
                  ))}
                </tr>
              ))}
            </tbody>
          </table>
        </div>
      )
  }
}

function textAlign(alignment: MarkdownAlignment | undefined): CSSProperties['textAlign'] {
  return alignment === 'center' ? 'center' : alignment === 'trailing' ? 'right' : 'left'
}

/** A fence: its caption (its language, or `code`) with copy, then its lines, coloured where its language is known. */
function CodeBlock({ language, text, last }: { readonly language: string | undefined; readonly text: string; readonly last: boolean }) {
  const [copied, setCopied] = useState(false)
  const runs = useMemo(() => CodeLexer.runs(text, language), [text, language])
  return (
    <figure style={{ margin: `0 0 ${last ? 0 : 12}px`, border: '1px solid var(--line-border)', borderRadius: 6, background: 'var(--surface-code)', overflow: 'hidden' }}>
      <figcaption
        className="mono"
        style={{ display: 'flex', alignItems: 'center', justifyContent: 'space-between', height: 30, padding: '0 6px 0 12px', borderBottom: '1px solid var(--line-border)', fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}
      >
        <span>{language ?? 'code'}</span>
        <button
          type="button"
          className="mono hv"
          onClick={() => {
            void globalThis.navigator?.clipboard?.writeText(text).catch(() => {})
            setCopied(true)
          }}
          style={{ height: 22, padding: '0 8px', border: 0, borderRadius: 4, fontSize: 'var(--t-meta)', color: 'var(--text-tertiary)' }}
        >
          {copied ? 'copied' : 'copy'}
        </button>
      </figcaption>
      <div className="mono" style={{ padding: '10px 14px', fontSize: 'var(--t-code)', lineHeight: 1.7, whiteSpace: 'pre', overflowX: 'auto' }}>
        {runs.map((run, index) =>
          run.kind === undefined ? (
            <span key={index}>{run.text}</span>
          ) : (
            <span key={index} style={{ color: CodeLexer.color(run.kind) }}>
              {run.text}
            </span>
          ),
        )}
      </div>
    </figure>
  )
}

function Spans({ spans }: { readonly spans: readonly MarkdownSpan[] }) {
  return spans.map((span, index) => <Span key={index} span={span} />)
}

function Span({ span }: { readonly span: MarkdownSpan }) {
  if (span.image !== undefined) {
    // Never fetched: its alt text, and its address in the mono face.
    return (
      <span title="An image, not fetched">
        {span.text === '' ? 'image' : span.text}{' '}
        <span className="mono" style={{ fontSize: 'var(--t-code)', color: 'var(--text-tertiary)' }}>
          {span.image}
        </span>
      </span>
    )
  }
  let content: ReactNode = span.code ? <code>{span.text}</code> : <CitedText text={span.text} />
  if (span.emphasis) content = <em>{content}</em>
  if (span.strong) content = <strong>{content}</strong>
  if (span.link === undefined) return content
  if (MarkdownLink.follows(span.link)) {
    return (
      <a href={span.link} target="_blank" rel="noreferrer" title={span.link} style={{ textDecorationColor: 'var(--line-strong)', textUnderlineOffset: 3 }}>
        {content}
      </a>
    )
  }
  // Drawn, never followed: a model's words must not open the person's files or another application.
  return (
    <span title={`${span.link} -- not followed from an answer`} style={{ textDecoration: 'underline', textDecorationStyle: 'dotted', textDecorationColor: 'var(--line-strong)', textUnderlineOffset: 3 }}>
      {content}
    </span>
  )
}
