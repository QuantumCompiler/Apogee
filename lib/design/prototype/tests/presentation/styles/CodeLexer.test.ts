import codeAnswer from '@recorded/data/protocol/Fixtures/code-answer.jsonl?raw'
import { describe, expect, test } from 'vitest'
import { CodeLexer } from '../../../source/presentation/styles/Components/CodeLexer'
import type { CodeKind } from '../../../source/presentation/styles/Components/CodeLexer'
import { MarkdownDocument } from '../../../source/presentation/styles/Components/MarkdownDocument'

/** A recorded answer's text: its deltas joined. */
const answerOf = (recording: string) =>
  recording
    .split('\n')
    .filter((line) => line.trim() !== '')
    .map((line) => JSON.parse(line) as { type: string; text?: string })
    .filter((event) => event.type === 'answer_delta')
    .map((event) => event.text ?? '')
    .join('')

/** Each run of `text` in `language` that has a kind, as `kind:text`. */
const kinds = (text: string, language: string) =>
  CodeLexer.runs(text, language)
    .filter((run) => run.kind !== undefined)
    .map((run) => `${run.kind}:${run.text}`)

describe("the code lexer: a fence's language, coloured in the design's five code colours (the Mac app's CodeLexer)", () => {
  test('a fence names its language by any alias; one it does not know, or none, is drawn uncoloured', () => {
    expect(CodeLexer.language('python')?.name).toBe('python')
    expect(CodeLexer.language('py')?.name).toBe('python')
    expect(CodeLexer.language('bash')?.name).toBe('shell')
    expect(CodeLexer.language('ts')?.name).toBe('typescript')
    expect(CodeLexer.language('Swift')?.name).toBe('swift')
    expect(CodeLexer.language('elixir')).toBeUndefined()
    expect(CodeLexer.language(undefined)).toBeUndefined()
    expect(CodeLexer.language('')).toBeUndefined()
    expect(CodeLexer.runs('Enum.each(1..3, fn i -> IO.puts("x") end)', 'elixir')).toEqual([{ text: 'Enum.each(1..3, fn i -> IO.puts("x") end)', kind: undefined }])
  })

  test('python: keywords, a definition’s name, a call, a string holding a #, a comment and a number', () => {
    expect(kinds('async def fetch(self, url: str) -> bytes | None:', 'python')).toEqual(['keyword:async', 'keyword:def', 'function:fetch', 'keyword:None'])
    expect(kinds('log.warning("attempt %d # timed out", n)  # try again', 'python')).toEqual(['function:warning', 'string:"attempt %d # timed out"', 'comment:# try again'])
    expect(kinds('async with asyncio.timeout(10.5):', 'python')).toEqual(['keyword:async', 'keyword:with', 'function:timeout', 'number:10.5'])
    expect(kinds('"""Fetch url."""', 'python')).toEqual(['string:"""Fetch url."""'])
  })

  test('rust, swift, typescript, shell and json: their own comments, numbers, strings and members', () => {
    expect(kinds('let base = 0x10u64; // 16 ms', 'rust')).toEqual(['keyword:let', 'number:0x10u64', 'comment:// 16 ms'])
    expect(kinds('/* a /* nested */ comment */ fn main() {}', 'rust')).toEqual(['comment:/* a /* nested */ comment */', 'keyword:fn', 'function:main'])
    expect(kinds('try await Task.sleep(for: .milliseconds(250))', 'swift')).toEqual(['keyword:try', 'keyword:await', 'function:sleep', 'function:milliseconds', 'number:250'])
    expect(kinds('console.warn(`attempt ${attempt} failed`);', 'ts')).toEqual(['function:warn', 'string:`attempt ${attempt} failed`'])
    expect(kinds('"$@" && return 0 # done', 'bash')).toEqual(['string:"$@"', 'keyword:return', 'number:0', 'comment:# done'])
    expect(kinds('{"proxy": null, "timeout": 10.5}', 'json')).toEqual(['string:"proxy"', 'keyword:null', 'string:"timeout"', 'number:10.5'])
  })

  test('a diff is coloured by line: added, removed, and its headers and context', () => {
    expect(CodeLexer.runs('--- a/x\n+++ b/x\n@@ -1 +1 @@\n context\n-old\n+new', 'diff').map((run) => run.kind ?? 'plain')).toEqual([
      'header', 'plain', 'header', 'plain', 'header', 'plain', 'context', 'plain', 'removed', 'plain', 'added',
    ])
  })

  test('the runs are the text, every character once, in order', () => {
    const text = 'def f(x):\n    return "a\\"b" # c\n'
    expect(CodeLexer.runs(text, 'python').map((run) => run.text).join('')).toBe(text)
  })

  test("each colour is a token variable: the five code colours, and a diff's in the accent, error and tertiary text", () => {
    const colors: Record<CodeKind, string> = {
      keyword: 'var(--code-keyword)',
      string: 'var(--code-string)',
      comment: 'var(--code-comment)',
      number: 'var(--code-number)',
      function: 'var(--code-function)',
      added: 'var(--status-accent)',
      removed: 'var(--status-error)',
      header: 'var(--text-tertiary)',
      context: 'var(--text-tertiary)',
    }
    for (const [kind, color] of Object.entries(colors)) expect(CodeLexer.color(kind as CodeKind)).toBe(color)
  })

  test("code-answer.jsonl: each fence the Mac's lexer knows is coloured; the unnamed and the elixir fence are drawn plain (CodeLexerTests.swift:275)", () => {
    const fences = MarkdownDocument.parse(answerOf(codeAnswer)).blocks.flatMap((block) => (block.case === 'code' ? [block] : []))
    expect(fences.map((fence) => fence.language ?? '(none)')).toEqual(['python', 'swift', 'ts', 'rust', 'bash', 'json', 'diff', '(none)', 'elixir'])
    for (const fence of fences) {
      const coloured = CodeLexer.runs(fence.text, fence.language).some((run) => run.kind !== undefined)
      expect(coloured, `${fence.language ?? 'the unnamed fence'}`).toBe(fence.language !== undefined && fence.language !== 'elixir')
    }
  })
})
