import codeAnswer from '@recorded/data/protocol/Fixtures/code-answer.jsonl?raw'
import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { MarkdownText } from '../../../source/presentation/styles/Components/MarkdownText'

const answerOf = (recording: string) =>
  recording
    .split('\n')
    .filter((line) => line.trim() !== '')
    .map((line) => JSON.parse(line) as { type: string; text?: string })
    .filter((event) => event.type === 'answer_delta')
    .map((event) => event.text ?? '')
    .join('')

describe("the markdown component, drawn: markdown-answer.jsonl's every block, and nothing fetched", () => {
  const markup = renderToStaticMarkup(<MarkdownText text={answerOf(markdownAnswer)} />)

  test('each block it carries is drawn: a heading, bold, emphasis, inline code, a fence, lists, a quote, a table and a rule', () => {
    expect(markup).toContain('<h2')
    expect(markup).toContain('<strong>inside</strong>')
    expect(markup).toContain('<em>all-or-nothing</em>')
    expect(markup).toMatch(/<code[^>]*>TaskGroup<\/code>/)
    expect(markup).toContain('<figure')
    expect(markup).toContain('<ul')
    expect(markup).toContain('<ol')
    expect(markup).toContain('<blockquote')
    expect(markup).toContain('<table')
    expect(markup).toContain('<hr')
  })

  test('the image draws no <img>: its alt text and its address, so the answer asks nothing of the network', () => {
    expect(markup).not.toContain('<img')
    expect(markup).toContain('The batch timeline')
    expect(markup).toContain('https://example.com/timeline.png')
  })

  test('https and mailto links are followed; the file: link is drawn and not followed', () => {
    expect(markup).toContain('href="https://docs.python.org/3/library/asyncio-task.html"')
    expect(markup).toContain('href="mailto:team@example.com"')
    expect(markup).not.toContain('href="file:')
    expect(markup).toContain('runner.md')
  })

  test("a fence's caption names its language, beside copy; its code is coloured by the five code tokens and nothing else", () => {
    expect(markup).toContain('>python<')
    expect(markup).toContain('>copy<')
    const figure = markup.slice(markup.indexOf('<figure'), markup.indexOf('</figure>'))
    const colors = [...figure.matchAll(/color:(var\(--[a-z-]+\))/g)].map((match) => match[1])
    expect(colors.length).toBeGreaterThan(0)
    expect(new Set(colors)).toEqual(new Set(['var(--code-keyword)', 'var(--code-string)', 'var(--code-comment)', 'var(--code-number)', 'var(--code-function)', 'var(--text-tertiary)']))
  })
})

describe("the markdown component, drawn: code-answer.jsonl's fences", () => {
  const markup = renderToStaticMarkup(<MarkdownText text={answerOf(codeAnswer)} />)

  test('the unnamed fence is captioned code, and it and the elixir fence are drawn uncoloured', () => {
    expect(markup).toContain('>code<')
    expect(markup).toContain('>elixir<')
    const elixir = markup.slice(markup.indexOf('>elixir<'))
    expect(elixir).toContain('Enum.each(1..3, fn i -&gt; IO.puts(&quot;attempt #{i}&quot;) end)')
    expect(elixir).not.toMatch(/color:var\(--code-/)
  })

  test("a diff's lines in the accent and the error colour", () => {
    expect(markup).toContain('color:var(--status-accent)')
    expect(markup).toContain('color:var(--status-error)')
  })
})

test('mid-stream: an open fence is drawn as code, and nothing is lost', () => {
  const markup = renderToStaticMarkup(<MarkdownText text={'Here:\n\n```python\nasync def f('} streaming />)
  expect(markup).toContain('<figure')
  expect(markup).toContain('async')
})

describe('citations: an answer drawn with its sources numbers [n] as a button that opens source n (design 1v)', () => {
  const text = 'One flush pays for many commits [1]. It is off until you set it [2][3]. A list [4] is not a source, nor is [x].'

  test('[1] to [3] are buttons, each named for its source and marked open; [4] and [x] stay text', () => {
    const markup = renderToStaticMarkup(<MarkdownText text={text} citations={{ count: 3, open: () => {}, opened: 2, label: (n) => `Source ${n}` }} />)
    expect((markup.match(/<button/g) ?? []).length).toBe(3)
    expect(markup).toContain('aria-label="Source 1"')
    expect(markup).toMatch(/aria-label="Source 2" aria-expanded="true"/)
    expect(markup).toMatch(/aria-label="Source 3" aria-expanded="false"/)
    expect(markup).toContain('A list [4] is not a source, nor is [x].')
  })

  test('with no citations the brackets are text, as before', () => {
    expect(renderToStaticMarkup(<MarkdownText text={text} />)).not.toContain('<button')
  })
})
