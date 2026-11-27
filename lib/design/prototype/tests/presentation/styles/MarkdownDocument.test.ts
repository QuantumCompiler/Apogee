import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import { describe, expect, test } from 'vitest'
import { MarkdownDocument, MarkdownLink } from '../../../source/presentation/styles/Components/MarkdownDocument'
import type { MarkdownBlock, MarkdownSpan } from '../../../source/presentation/styles/Components/MarkdownDocument'

const answerOf = (recording: string) =>
  recording
    .split('\n')
    .filter((line) => line.trim() !== '')
    .map((line) => JSON.parse(line) as { type: string; text?: string })
    .filter((event) => event.type === 'answer_delta')
    .map((event) => event.text ?? '')
    .join('')

const words = (spans: readonly MarkdownSpan[]) => spans.map((span) => span.text).join('')

describe("markdown, as the model writes it: the blocks and spans the answer draws (the Mac app's MarkdownDocument)", () => {
  const blocks = MarkdownDocument.parse(answerOf(markdownAnswer)).blocks

  test("markdown-answer.jsonl's every block, in order", () => {
    expect(blocks.map((block) => block.case)).toEqual(['heading', 'paragraph', 'code', 'paragraph', 'list', 'list', 'quote', 'table', 'rule', 'paragraph', 'paragraph'])
    const heading = blocks[0] as Extract<MarkdownBlock, { case: 'heading' }>
    expect([heading.level, words(heading.spans)]).toEqual([2, 'Timeouts that keep the batch going'])
  })

  test('bold, emphasis and inline code are spans of their paragraph', () => {
    const paragraph = blocks[1] as Extract<MarkdownBlock, { case: 'paragraph' }>
    expect(paragraph.spans.filter((span) => span.strong).map((span) => span.text)).toEqual(['inside'])
    expect(paragraph.spans.filter((span) => span.emphasis).map((span) => span.text)).toEqual(['all-or-nothing'])
    expect(paragraph.spans.filter((span) => span.code).map((span) => span.text)).toEqual(['TaskGroup'])
  })

  test('the python fence: its language and its lines, as written', () => {
    const code = blocks[2] as Extract<MarkdownBlock, { case: 'code' }>
    expect(code.language).toBe('python')
    expect(code.closed).toBe(true)
    expect(code.text.split('\n')[0]).toBe('async def _fetch(self, url: str) -> bytes | None:')
    expect(code.text.endsWith('return None')).toBe(true)
  })

  test('a list nested in a list, and a numbered one', () => {
    const list = blocks[4] as Extract<MarkdownBlock, { case: 'list' }>
    expect(list.ordered).toBe(false)
    expect(list.items).toHaveLength(2)
    const nested = list.items[0]?.blocks[1]
    expect(nested?.case === 'list' ? nested.items.map((item) => (item.blocks[0]?.case === 'paragraph' ? words(item.blocks[0].spans) : '')) : []).toEqual(['the caller skips it', 'the log keeps the address'])
    const numbered = blocks[5] as Extract<MarkdownBlock, { case: 'list' }>
    expect([numbered.ordered, numbered.start, numbered.items.length]).toEqual([true, 1, 2])
  })

  test('a quote, and a table with its columns aligned as its delimiter row says', () => {
    const quote = blocks[6] as Extract<MarkdownBlock, { case: 'quote' }>
    expect(quote.blocks[0]?.case).toBe('paragraph')
    const table = blocks[7] as Extract<MarkdownBlock, { case: 'table' }>
    expect(table.header.map(words)).toEqual(['Option', 'Keeps the batch', 'Cancels on failure'])
    expect(table.align).toEqual(['leading', 'center', 'leading'])
    expect(table.rows.map((row) => row.map(words))).toEqual([
      ['asyncio.timeout in the child', 'yes', 'yes'],
      ['gather(..., return_exceptions=True)', 'yes', 'no'],
    ])
  })

  test('three links, https, mailto and file:, and an image drawn as its alt text and its address', () => {
    const links = (blocks[9] as Extract<MarkdownBlock, { case: 'paragraph' }>).spans.filter((span) => span.link !== undefined)
    expect(links.map((span) => [span.text, span.link])).toEqual([
      ['asyncio docs', 'https://docs.python.org/3/library/asyncio-task.html'],
      ['write to the team', 'mailto:team@example.com'],
      ['runner.md', 'file:///Users/someone/Documents/runner.md'],
    ])
    const image = (blocks[10] as Extract<MarkdownBlock, { case: 'paragraph' }>).spans[0]
    expect([image?.text, image?.image]).toEqual(['The batch timeline', 'https://example.com/timeline.png'])
  })

  test('a link is followed only for http, https and mailto; file: and any other scheme are drawn, not followed', () => {
    expect(MarkdownLink.follows('https://example.com')).toBe(true)
    expect(MarkdownLink.follows('http://example.com')).toBe(true)
    expect(MarkdownLink.follows('mailto:team@example.com')).toBe(true)
    expect(MarkdownLink.follows('file:///Users/someone/Documents/runner.md')).toBe(false)
    expect(MarkdownLink.follows('javascript:alert(1)')).toBe(false)
    expect(MarkdownLink.follows('runner.md')).toBe(false)
  })

  test('mid-stream: a fence not yet closed is code all the same, its text so far', () => {
    const streaming = MarkdownDocument.parse('Here:\n\n```python\nasync def f(')
    const code = streaming.blocks[1]
    expect(code).toEqual({ case: 'code', language: 'python', text: 'async def f(', closed: false })
  })

  test('plain text is one paragraph, its lines joined; blank lines part paragraphs', () => {
    expect(MarkdownDocument.parse('ROOT<hi>').blocks).toEqual([{ case: 'paragraph', spans: [{ text: 'ROOT<hi>' }] }])
    expect(MarkdownDocument.parse('one\ntwo\n\nthree').blocks.map((block) => (block.case === 'paragraph' ? words(block.spans) : ''))).toEqual(['one two', 'three'])
  })
})
