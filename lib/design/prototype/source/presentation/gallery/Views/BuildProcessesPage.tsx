import type { CSSProperties } from 'react'
import { CodeText, paint } from '../Components/CodeText'
import type { ProcessLine } from '../Models/processes'
import type { BuildProcessesViewModel, ProcessLineFace } from '../ViewModels/BuildProcessesViewModel'

/** An arrowhead's length and half its width, at the end of a child's line. */
const head = { length: 8, half: 4 }

/** The code face in a box, as the board sets it beside the box's words. */
const code: CSSProperties = { fontFamily: 'var(--font-mono)', fontSize: 'var(--t-ui)', color: 'var(--text-primary)' }

/** A box's line under its title: a command, a note, plain words, or a gap. */
function BoxLine({ line }: { readonly line: ProcessLine }) {
  if (line.kind === 'space') return <br />
  return (
    <>
      <br />
      {line.kind === 'code' ? (
        <span style={code}>{line.text}</span>
      ) : line.kind === 'note' ? (
        <span style={{ fontSize: 'var(--t-small)', color: 'var(--text-secondary)' }}>{line.text}</span>
      ) : (
        <CodeText text={line.text} code={code} />
      )}
    </>
  )
}

/** A connector: its line, short of its arrowhead where it has one. */
function Connector({ line }: { readonly line: ProcessLineFace }) {
  const end = line.arrow ? line.x2 - head.half : line.x2
  return (
    <>
      <line x1={line.x1} y1={line.y1} x2={end} y2={line.y2} strokeDasharray={line.dashed ? '6 6' : undefined} />
      {line.arrow ? <path d={`M ${line.x2} ${line.y2} L ${line.x2 - head.length} ${line.y2 - head.half} L ${line.x2 - head.length} ${line.y2 + head.half} Z`} fill="currentColor" stroke="none" /> : null}
    </>
  )
}

/**
 * The processes page (design 1r): the old BuildProcesses diagram -- the app,
 * its own files and the media's records on the left, and the six kinds of
 * `apogee` child it starts over pipes on the right -- drawn from its
 * view-model, every color a token, so it follows its board's theme.
 */
export function BuildProcessesPage({ model }: { readonly model: BuildProcessesViewModel }) {
  const face = model.face()
  return (
    <div data-build-page="processes" style={{ position: 'relative', width: face.width, height: face.height, background: paint(face.background), color: paint(face.ink), fontFamily: 'var(--font-ui)' }}>
      <svg aria-hidden="true" width={face.width} height={face.height} style={{ position: 'absolute', left: 0, top: 0, overflow: 'visible', color: paint(face.connector) }} fill="none" stroke="currentColor" strokeWidth={2} strokeLinecap="round" strokeLinejoin="round">
        {face.lines.map((line, index) => (
          <Connector key={index} line={line} />
        ))}
      </svg>
      <h1 style={{ position: 'absolute', left: 64, top: 56, margin: 0, fontSize: 'var(--t-display)', lineHeight: '36px', fontWeight: 600 }}>{face.title}</h1>
      <p style={{ position: 'absolute', left: 64, top: 100, width: 1240, margin: 0, fontSize: 'var(--t-prose)', lineHeight: '21px', color: paint(face.note) }}>
        <CodeText text={face.intro} code={{ fontFamily: 'var(--font-mono)', fontSize: 'var(--t-dense)' }} />
      </p>
      {face.boxes.map((box) => (
        <div
          key={box.id}
          data-box={box.id}
          style={{ position: 'absolute', left: box.x, top: box.y, width: box.width, height: box.height, boxSizing: 'border-box', padding: '12px 16px', display: 'flex', alignItems: 'center', background: paint(box.fill), border: `1px solid ${paint(box.border)}`, borderRadius: 'var(--radius-menu)', fontSize: 'var(--t-body)', lineHeight: '19px' }}
        >
          <span>
            <b style={box.app ? { fontSize: 'var(--t-h2)' } : undefined}>{box.title}</b>
            {box.lines.map((line, index) => (
              <BoxLine key={index} line={line} />
            ))}
          </span>
        </div>
      ))}
      {face.labels.map((label) => (
        <div key={label.text} style={{ position: 'absolute', left: label.x, top: label.y, ...(label.width === undefined ? { whiteSpace: 'nowrap' } : { width: label.width, textAlign: 'center' }), fontSize: 'var(--t-body)', lineHeight: '16px', color: paint(face.note) }}>
          {label.text}
        </div>
      ))}
    </div>
  )
}
