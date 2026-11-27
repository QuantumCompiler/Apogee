import codeAnswer from '@recorded/data/protocol/Fixtures/code-answer.jsonl?raw'
import executeProject from '@recorded/data/protocol/Fixtures/execute-project.jsonl?raw'
import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import { MarkdownText } from '../Components/MarkdownText'

/**
 * The markdown component on its own (design 1u), one preview per state a
 * person sees differently, each an answer the CLI recorded: its deltas
 * joined, as the transcript joins them. `styles` leans on no sibling, so the
 * recording's lines are read here, plainly.
 */
function answerOf(recording: string, deltas = Number.POSITIVE_INFINITY): string {
  return recording
    .split('\n')
    .filter((line) => line.trim() !== '')
    .map((line) => JSON.parse(line) as { readonly type: string; readonly text?: string })
    .filter((event) => event.type === 'answer_delta')
    .slice(0, deltas)
    .map((event) => event.text ?? '')
    .join('')
}

const frame = { width: 720, padding: 24, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)' } as const

/** markdown-answer.jsonl whole: a heading, bold, emphasis, inline code, a python fence, lists, a quote, a table, a rule, three links and an image drawn as its words. */
export const Answer = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <MarkdownText text={answerOf(markdownAnswer)} />
  </div>
)

/** code-answer.jsonl whole: each fence coloured by its language, a diff by line, the unnamed and the elixir fence plain. */
export const CodeFences = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <MarkdownText text={answerOf(codeAnswer)} />
  </div>
)

/** markdown-answer.jsonl mid-stream, its python fence still open. */
export const MidStream = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <MarkdownText text={answerOf(markdownAnswer, 40)} streaming />
  </div>
)

/** A plain answer, one paragraph: execute-project.jsonl's. */
export const Plain = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <MarkdownText text={answerOf(executeProject)} />
  </div>
)

/** The same answer in Graphite Light. */
export const AnswerLight = () => (
  <div className="ap" data-theme="light" style={frame}>
    <MarkdownText text={answerOf(markdownAnswer)} />
  </div>
)
