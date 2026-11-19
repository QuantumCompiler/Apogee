/**
 * What a run of code is, for the colour it draws in: one of the design's five
 * code colours (`code.keyword` to `code.function`), or, in a diff coloured by
 * line, what its line is. The Mac app's `CodeKind` (`CodeLexer.swift`).
 */
export type CodeKind = 'keyword' | 'string' | 'comment' | 'number' | 'function' | 'added' | 'removed' | 'header' | 'context'

/** A run of a fence's text: its characters, and its kind where it has one. */
export interface CodeRun {
  readonly text: string
  readonly kind: CodeKind | undefined
}

/** One way a language writes a string. */
interface StringRule {
  readonly open: string
  readonly close: string
  /** What may lead the opening, at a word's start: Python's `r`, `f`, `b`. */
  readonly prefixes: readonly string[]
  /** A backslash escapes the next character. */
  readonly escapes: boolean
  /** It may run past the end of its line. */
  readonly multiline: boolean
  /** One character or one escape between single quotes, else no string at all (Rust's lifetime `'a`). */
  readonly character: boolean
}

/** A token language's rules: the Mac app's `TokenRules`, the forms this port reads. */
interface TokenRules {
  readonly keywords: ReadonlySet<string>
  /** Keywords matched in any case (SQL), written lower-case. */
  readonly anyCase: boolean
  /** A word just after a `.` is a member, never a keyword -- except these (Rust's `.await`). */
  readonly memberKeywords: ReadonlySet<string>
  readonly lineComments: readonly string[]
  /** A line comment opens only where a word could start (`#` in a shell or YAML). */
  readonly commentAtWordStart: boolean
  readonly blockComment: readonly [string, string] | undefined
  readonly nestedComments: boolean
  readonly strings: readonly StringRule[]
  /** Words after which the next word is a definition's name (`def`, `func`). */
  readonly definers: ReadonlySet<string>
  /** A word just before `(` is a call's name. */
  readonly calls: boolean
  /** A word just before `!(`, `![` or `!{` is a macro's name (Rust). */
  readonly macros: boolean
  /** What may start a word besides letters and `_`: `$`, `#`, `@`. */
  readonly wordStarts: string
  /** What may continue a word besides letters, digits and `_`: `$`, `-`. */
  readonly wordInners: string
  /** A keyword used as an argument's label -- after `(` or `,`, just before `:` -- is a plain word (Swift). */
  readonly labels: boolean
}

/** A language the lexer knows: its name, its aliases, and its rules -- none for a diff, coloured by line. */
export interface CodeLanguage {
  readonly name: string
  readonly aliases: readonly string[]
  readonly rules: TokenRules | undefined
}

const words = (list: string): Set<string> => new Set(list.split(/\s+/).filter((word) => word !== ''))

const quoted = (open: string, options: Partial<Omit<StringRule, 'open'>> = {}): StringRule => ({
  open,
  close: options.close ?? open,
  prefixes: options.prefixes ?? [''],
  escapes: options.escapes ?? true,
  multiline: options.multiline ?? false,
  character: options.character ?? false,
})

/** Each of `prefixes` in every mix of upper and lower case (Python's `Rb`, `bR`). */
const anyCase = (prefixes: readonly string[]): string[] => {
  const all = new Set<string>()
  for (const prefix of prefixes) {
    let forms = ['']
    for (const letter of prefix) forms = forms.flatMap((form) => [form + letter.toLowerCase(), form + letter.toUpperCase()])
    for (const form of forms) all.add(form)
  }
  return [...all]
}

const rules = (partial: Partial<TokenRules> & Pick<TokenRules, 'keywords'>): TokenRules => ({
  anyCase: false,
  memberKeywords: new Set(),
  lineComments: [],
  commentAtWordStart: false,
  blockComment: undefined,
  nestedComments: false,
  strings: [],
  definers: new Set(),
  calls: true,
  macros: false,
  wordStarts: '',
  wordInners: '',
  labels: false,
  ...partial,
})

const tripleQuote = '"""'
const pythonPrefixes = anyCase(['', 'r', 'u', 'b', 'f', 't', 'br', 'rb', 'fr', 'rf', 'tr', 'rt'])
const javascriptKeywords = words(`break case catch class const continue debugger default delete do else export extends finally for function if
  import in instanceof let new return super switch this throw try typeof var void while with yield async await of static from as true false null undefined`)
const javascriptStrings = [quoted('`', { multiline: true }), quoted('"'), quoted("'")]
const cKeywords = words(`auto break case char const continue default do double else enum extern float for goto if inline int long register
  restrict return short signed sizeof static struct switch typedef union unsigned void volatile while _Bool _Atomic _Alignas _Alignof _Generic
  _Noreturn _Static_assert _Thread_local bool true false NULL #include #define #undef #if #ifdef #ifndef #elif #else #endif #pragma #error #warning #line`)
const cStrings = [quoted('"', { prefixes: ['', 'u8', 'u', 'U', 'L'] }), quoted("'", { prefixes: ['', 'u8', 'u', 'U', 'L'], character: true })]

/**
 * Every language the lexer knows, with its aliases: the Mac app's
 * `CodeLanguage.all`, in its order. Any other fence draws in one colour.
 * This port reads each language's quoted strings, comments, numbers,
 * keywords, definitions and calls; a raw string fenced by `#` and a
 * here-document read as the quoted form they open with.
 */
const languages: readonly CodeLanguage[] = [
  {
    name: 'python',
    aliases: ['python', 'py', 'python3', 'py3'],
    rules: rules({
      keywords: words(`False None True and as assert async await break class continue def del elif else except finally for from
        global if import in is lambda nonlocal not or pass raise return try while with yield`),
      lineComments: ['#'],
      strings: [
        quoted(tripleQuote, { prefixes: pythonPrefixes, multiline: true }),
        quoted("'''", { prefixes: pythonPrefixes, multiline: true }),
        quoted('"', { prefixes: pythonPrefixes }),
        quoted("'", { prefixes: pythonPrefixes }),
      ],
      definers: words('def'),
    }),
  },
  {
    name: 'swift',
    aliases: ['swift'],
    rules: rules({
      keywords: words(`associatedtype class deinit enum extension fileprivate func import init inout internal let operator private
        precedencegroup protocol public rethrows static struct subscript typealias var break case catch continue default
        defer do else fallthrough for guard if in repeat return throw switch where while as false is nil self Self super
        throws true try await async actor nonisolated isolated some any convenience final indirect lazy mutating
        nonmutating override required unowned weak willSet didSet macro #if #else #elseif #endif #available
        #unavailable #selector #keyPath #sourceLocation #warning #error`),
      memberKeywords: words('self init Type'),
      lineComments: ['//'],
      blockComment: ['/*', '*/'],
      nestedComments: true,
      strings: [quoted(tripleQuote, { multiline: true }), quoted('"')],
      definers: words('func'),
      wordStarts: '#',
      labels: true,
    }),
  },
  {
    name: 'javascript',
    aliases: ['javascript', 'js', 'jsx', 'mjs', 'cjs'],
    rules: rules({ keywords: javascriptKeywords, lineComments: ['//'], blockComment: ['/*', '*/'], strings: javascriptStrings, definers: words('function'), wordStarts: '$', wordInners: '$' }),
  },
  {
    name: 'typescript',
    aliases: ['typescript', 'ts', 'tsx', 'mts', 'cts'],
    rules: rules({
      keywords: new Set([...javascriptKeywords, ...words('interface type enum implements private protected public readonly declare namespace abstract keyof infer is satisfies override')]),
      lineComments: ['//'],
      blockComment: ['/*', '*/'],
      strings: javascriptStrings,
      definers: words('function'),
      wordStarts: '$',
      wordInners: '$',
    }),
  },
  { name: 'c', aliases: ['c', 'h'], rules: rules({ keywords: cKeywords, lineComments: ['//'], blockComment: ['/*', '*/'], strings: cStrings, wordStarts: '#' }) },
  {
    name: 'cpp',
    aliases: ['cpp', 'c++', 'cc', 'cxx', 'hpp', 'hxx', 'hh'],
    rules: rules({
      keywords: new Set([
        ...cKeywords,
        ...words(`alignas alignof asm catch class concept consteval constexpr constinit const_cast co_await co_return co_yield
          decltype delete dynamic_cast explicit export friend mutable namespace new noexcept nullptr operator private
          protected public reinterpret_cast requires static_assert static_cast template this thread_local throw try typeid
          typename using virtual override final wchar_t char8_t char16_t char32_t`),
      ]),
      lineComments: ['//'],
      blockComment: ['/*', '*/'],
      strings: cStrings,
      wordStarts: '#',
    }),
  },
  {
    name: 'objc',
    aliases: ['objc', 'objective-c', 'objectivec', 'obj-c', 'objc++', 'objective-c++', 'mm'],
    rules: rules({
      keywords: new Set([
        ...cKeywords,
        ...words(`#import @interface @implementation @end @property @protocol @class @selector @synthesize @dynamic
          @autoreleasepool @try @catch @finally @throw @optional @required @public @private @protected @package @import
          @encode @YES @NO self super nil Nil YES NO id instancetype BOOL nonatomic strong weak readonly readwrite
          nullable nonnull __weak __strong __block`),
      ]),
      lineComments: ['//'],
      blockComment: ['/*', '*/'],
      strings: [quoted('"', { prefixes: ['', '@', 'u8', 'u', 'U', 'L'] }), quoted("'", { prefixes: ['', 'u8', 'u', 'U', 'L'], character: true })],
      wordStarts: '#@',
    }),
  },
  {
    name: 'rust',
    aliases: ['rust', 'rs'],
    rules: rules({
      keywords: words(`as async await break const continue crate dyn else enum extern false fn for if impl in let loop match mod move
        mut pub ref return self Self static struct super trait true type unsafe use where while union macro_rules`),
      memberKeywords: words('await'),
      lineComments: ['//'],
      blockComment: ['/*', '*/'],
      nestedComments: true,
      strings: [quoted('"', { prefixes: ['', 'b', 'c', 'r', 'br', 'cr'], multiline: true }), quoted("'", { prefixes: ['', 'b'], character: true })],
      definers: words('fn'),
      macros: true,
    }),
  },
  {
    name: 'go',
    aliases: ['go', 'golang'],
    rules: rules({
      keywords: words(`break case chan const continue default defer else fallthrough for func go goto if import interface map package
        range return select struct switch type var true false nil iota`),
      lineComments: ['//'],
      blockComment: ['/*', '*/'],
      strings: [quoted('"'), quoted('`', { escapes: false, multiline: true }), quoted("'", { character: true })],
      definers: words('func'),
    }),
  },
  {
    name: 'java',
    aliases: ['java'],
    rules: rules({
      keywords: words(`abstract assert boolean break byte case catch char class const continue default do double else enum extends
        final finally float for goto if implements import instanceof int interface long native new package private
        protected public return short static strictfp super switch synchronized this throw throws transient try void
        volatile while var record yield sealed permits true false null`),
      lineComments: ['//'],
      blockComment: ['/*', '*/'],
      strings: [quoted(tripleQuote, { multiline: true }), quoted('"'), quoted("'", { character: true })],
    }),
  },
  {
    name: 'kotlin',
    aliases: ['kotlin', 'kt', 'kts'],
    rules: rules({
      keywords: words(`as break class continue do else false for fun if in interface is null object package return super this throw
        true try typealias typeof val var when while by catch constructor finally get import init set where abstract
        annotation companion const crossinline data enum expect actual external final infix inline inner internal
        lateinit noinline open operator out override private protected public reified sealed suspend tailrec vararg`),
      lineComments: ['//'],
      blockComment: ['/*', '*/'],
      nestedComments: true,
      strings: [quoted(tripleQuote, { escapes: false, multiline: true }), quoted('"'), quoted("'", { character: true })],
      definers: words('fun'),
    }),
  },
  {
    name: 'csharp',
    aliases: ['csharp', 'cs', 'c#'],
    rules: rules({
      keywords: words(`abstract as base bool break byte case catch char checked class const continue decimal default delegate do
        double else enum event explicit extern false finally fixed float for foreach goto if implicit in int interface
        internal is lock long namespace new null object operator out override params private protected public readonly
        ref return sbyte sealed short sizeof stackalloc static string struct switch this throw true try typeof uint
        ulong unchecked unsafe ushort using virtual void volatile while async await var dynamic get set init record
        yield partial where when nameof global #if #else #elif #endif #region #endregion #define #pragma #nullable`),
      lineComments: ['//'],
      blockComment: ['/*', '*/'],
      strings: [quoted(tripleQuote, { prefixes: ['', '$', '$$'], escapes: false, multiline: true }), quoted('"', { prefixes: ['', '$', '@', '$@', '@$'] }), quoted("'", { character: true })],
      wordStarts: '#',
    }),
  },
  {
    name: 'ruby',
    aliases: ['ruby', 'rb'],
    rules: rules({
      keywords: words(`BEGIN END alias and begin break case class def defined? do else elsif end ensure false for if in module next
        nil not or redo rescue retry return self super then true undef unless until when while yield __FILE__ __LINE__`),
      lineComments: ['#'],
      strings: [quoted('"', { multiline: true }), quoted("'", { multiline: true })],
      definers: words('def'),
    }),
  },
  {
    name: 'shell',
    aliases: ['shell', 'sh', 'bash', 'zsh'],
    rules: rules({
      keywords: words(`if then else elif fi case esac for select while until do done in function time return break continue local
        export readonly declare unset exit`),
      lineComments: ['#'],
      commentAtWordStart: true,
      strings: [quoted('"', { multiline: true }), quoted("'", { escapes: false, multiline: true }), quoted('`', { multiline: true })],
      definers: words('function'),
      wordInners: '-',
    }),
  },
  {
    name: 'sql',
    aliases: ['sql'],
    rules: rules({
      keywords: words(`select from where and or not insert into values update set delete create table drop alter add column index
        primary key foreign references join inner left right outer full cross natural on using as group by order having
        limit offset distinct union all exists in is null like ilike between case when then else end begin commit
        rollback transaction with returning default unique check constraint view trigger function procedure if replace
        asc desc true false cast database schema grant revoke over partition window int integer bigint smallint varchar
        char text boolean bool date time timestamp timestamptz interval numeric decimal real float double serial
        bigserial uuid json jsonb blob`),
      anyCase: true,
      lineComments: ['--'],
      blockComment: ['/*', '*/'],
      strings: [quoted("'", { escapes: false, multiline: true })],
      definers: words('function procedure'),
    }),
  },
  { name: 'json', aliases: ['json'], rules: rules({ keywords: words('true false null'), strings: [quoted('"')], calls: false }) },
  {
    name: 'yaml',
    aliases: ['yaml', 'yml'],
    rules: rules({
      keywords: words('true false null True False TRUE FALSE Null NULL'),
      lineComments: ['#'],
      commentAtWordStart: true,
      strings: [quoted('"', { multiline: true }), quoted("'", { escapes: false, multiline: true })],
      calls: false,
      wordInners: '-',
    }),
  },
  {
    name: 'toml',
    aliases: ['toml'],
    rules: rules({
      keywords: words('true false inf nan'),
      lineComments: ['#'],
      strings: [quoted(tripleQuote, { multiline: true }), quoted("'''", { escapes: false, multiline: true }), quoted('"'), quoted("'", { escapes: false })],
      calls: false,
      wordInners: '-',
    }),
  },
  { name: 'diff', aliases: ['diff', 'patch'], rules: undefined },
]

const byAlias = new Map(languages.flatMap((language) => language.aliases.map((alias) => [alias, language] as const)))

const isLetter = (character: string) => /[A-Za-z_]/.test(character) || character.charCodeAt(0) > 127
const isDigit = (character: string) => character >= '0' && character <= '9'

/**
 * Colours code by its fence's language, in the design's five code colours:
 * the Mac app's `CodeLexer` (`PresentationStyles`, `CodeLexer.swift`). A
 * language it does not know -- or a fence that names none -- draws in one
 * colour (`CodeLexerTests.swift:275`).
 */
export const CodeLexer = {
  languages,

  /** The language a fence's info string names, by any alias, in any case; `undefined` for none it knows. */
  language(info: string | undefined): CodeLanguage | undefined {
    const name = info?.trim().split(/\s+/)[0]?.toLowerCase()
    return name === undefined || name === '' ? undefined : byAlias.get(name)
  },

  /** `text` cut into runs, each with its kind where it has one: every character once, in order. */
  runs(text: string, info: string | undefined): CodeRun[] {
    const language = CodeLexer.language(info)
    if (!language) return [{ text, kind: undefined }]
    return language.rules ? tokenRuns(text, language.rules) : diffRuns(text)
  },

  /** The colour a kind draws in: a token variable, never a value. */
  color(kind: CodeKind): string {
    switch (kind) {
      case 'keyword':
        return 'var(--code-keyword)'
      case 'string':
        return 'var(--code-string)'
      case 'comment':
        return 'var(--code-comment)'
      case 'number':
        return 'var(--code-number)'
      case 'function':
        return 'var(--code-function)'
      case 'added':
        return 'var(--status-accent)'
      case 'removed':
        return 'var(--status-error)'
      case 'header':
      case 'context':
        return 'var(--text-tertiary)'
    }
  },
}

/** A diff coloured by line: its headers (`---`, `+++`, `@@`, a tool's own lines), added, removed and context. */
function diffRuns(text: string): CodeRun[] {
  const runs: CodeRun[] = []
  const lines = text.split('\n')
  lines.forEach((line, index) => {
    if (line !== '') {
      const first = line[0] ?? ''
      const three = line.length >= 3 && line[1] === first && line[2] === first
      const kind: CodeKind =
        (first === '+' || first === '-') && three
          ? 'header'
          : first === '@' && line[1] === '@'
            ? 'header'
            : first === '+'
              ? 'added'
              : first === '-'
                ? 'removed'
                : first === ' ' || first === '\t'
                  ? 'context'
                  : 'header'
      runs.push({ text: line, kind })
    }
    if (index < lines.length - 1) runs.push({ text: '\n', kind: undefined })
  })
  return merge(runs)
}

/** Adjacent runs of the same kind, joined. */
function merge(runs: readonly CodeRun[]): CodeRun[] {
  const merged: CodeRun[] = []
  for (const run of runs) {
    const last = merged.at(-1)
    if (last && last.kind === run.kind) merged[merged.length - 1] = { text: last.text + run.text, kind: run.kind }
    else if (run.text !== '') merged.push(run)
  }
  return merged
}

/** A token language's runs. */
function tokenRuns(text: string, rules: TokenRules): CodeRun[] {
  const runs: CodeRun[] = []
  let at = 0
  let plainFrom = 0
  /** The last word read, and what came before it: a definer, a `.`. */
  let lastWord = ''
  let afterDot = false
  const push = (end: number, kind: CodeKind) => {
    if (plainFrom < at) runs.push({ text: text.slice(plainFrom, at), kind: undefined })
    runs.push({ text: text.slice(at, end), kind })
    at = end
    plainFrom = end
  }
  const atWordStart = () => at === 0 || !(isLetter(text[at - 1] ?? '') || isDigit(text[at - 1] ?? ''))
  const lineStartOrBlank = () => at === 0 || /\s/.test(text[at - 1] ?? '')

  while (at < text.length) {
    const character = text[at] ?? ''

    // A block comment, nested where the language nests them.
    const block = rules.blockComment
    if (block && text.startsWith(block[0], at)) {
      let depth = 0
      let end = at
      while (end < text.length) {
        if (text.startsWith(block[0], end)) {
          depth += 1
          end += block[0].length
        } else if (text.startsWith(block[1], end)) {
          depth -= 1
          end += block[1].length
          if (depth === 0 || !rules.nestedComments) break
        } else end += 1
      }
      push(end, 'comment')
      continue
    }

    // A line comment, to the end of its line.
    const line = rules.lineComments.find((marker) => text.startsWith(marker, at))
    if (line && (!rules.commentAtWordStart || lineStartOrBlank())) {
      const newline = text.indexOf('\n', at)
      push(newline < 0 ? text.length : newline, 'comment')
      continue
    }

    // A string, its prefix included, opening at a word's start.
    const string = atWordStart() ? matchString(text, at, rules) : undefined
    if (string !== undefined) {
      push(string, 'string')
      lastWord = ''
      continue
    }

    // A number: an integer or a float, hex, octal or binary, with underscores, an exponent or a type's suffix.
    if (isDigit(character) || (character === '.' && isDigit(text[at + 1] ?? '') && atWordStart())) {
      if (atWordStart() || !isLetter(text[at - 1] ?? '')) {
        const match = /^(0[xX][0-9a-fA-F_]+|0[bB][01_]+|0[oO][0-7_]+|(\d[\d_]*)?\.?\d[\d_]*([eE][+-]?\d+)?)[A-Za-z0-9_]*/.exec(text.slice(at))
        if (match) {
          // `1..3` is a range of two numbers, not one.
          const value = match[0].includes('..') ? (match[0].split('..')[0] ?? match[0]) : match[0]
          push(at + value.length, 'number')
          lastWord = ''
          continue
        }
      }
    }

    // A word: a keyword, a definition's name, a call's, a macro's -- or plain.
    if (isLetter(character) || rules.wordStarts.includes(character)) {
      let end = at + 1
      while (end < text.length) {
        const next = text[end] ?? ''
        if (isLetter(next) || isDigit(next) || (rules.wordInners.includes(next) && isLetter(text[end + 1] ?? ''))) end += 1
        else break
      }
      const word = text.slice(at, end)
      const rest = text.slice(end)
      const keyword = rules.keywords.has(rules.anyCase ? word.toLowerCase() : word)
      const member = afterDot && !rules.memberKeywords.has(word)
      const label = rules.labels && /^\s*:(?!:)/.test(rest) && /[(,]\s*$/.test(text.slice(0, at))
      let kind: CodeKind | undefined
      if (keyword && !member && !label) kind = 'keyword'
      else if (rules.definers.has(lastWord)) kind = 'function'
      else if (rules.calls && /^\s*\(/.test(rest) && !keyword) kind = 'function'
      else if (rules.macros && /^!\s*[([{]/.test(rest)) kind = 'function'
      lastWord = word
      afterDot = false
      if (kind) push(end, kind)
      else at = end
      continue
    }

    if (!/\s/.test(character)) {
      afterDot = character === '.'
      if (character !== '.') lastWord = ''
    }
    at += 1
  }
  if (plainFrom < text.length) runs.push({ text: text.slice(plainFrom), kind: undefined })
  return merge(runs)
}

/** Where a string opening at `at` ends, its prefix included; `undefined` where none opens there. */
function matchString(text: string, at: number, rules: TokenRules): number | undefined {
  for (const rule of rules.strings) {
    for (const prefix of rule.prefixes) {
      if (!text.startsWith(prefix + rule.open, at)) continue
      let end = at + prefix.length + rule.open.length
      if (rule.character) {
        const body = /^(\\.[^']*?|[^'\\\n])'/.exec(text.slice(end))
        if (!body) continue
        return end + body[0].length
      }
      while (end < text.length) {
        const next = text[end] ?? ''
        if (rule.escapes && next === '\\') {
          end += 2
          continue
        }
        if (text.startsWith(rule.close, end)) return end + rule.close.length
        if (next === '\n' && !rule.multiline) return end
        end += 1
      }
      return text.length
    }
  }
  return undefined
}
