import { describe, expect, test } from 'vitest'
import { sessionSamples } from '../../../source/presentation/session/Previews/SessionPresets'
import { AttachmentKind, ContextMeter, PrivateMode, SlashCommand } from '../../../source/presentation/session/ViewModels/SessionActions'

describe("the session's acts and facts (the Mac app's SessionActions)", () => {
  test("the slash commands are the old prototype's, in its order and words: /play and /symphonies first in an execute session", () => {
    const chat = SlashCommand.all(false)
    expect(chat[0]).toEqual({ name: 'model', args: '[backend]', summary: 'Show the backend answering, or switch to another' })
    expect(chat.map((command) => command.name)).toEqual([
      'model', 'models', 'suite', 'think', 'compact', 'title', 'attach', 'attachments', 'detach', 'recall', 'private',
      'allow', 'deny', 'revoke', 'permissions', 'system', 'temperature', 'max-tokens', 'retriever', 'rerank', 'capture', 'help',
    ])
    const execute = SlashCommand.all(true)
    expect(execute.slice(0, 2).map((command) => command.name)).toEqual(['play', 'symphonies'])
    expect(execute[0]?.summary).toBe('Play a symphony on the input: its stages on the suite’s members, its output this session’s answer')
  })

  test('the commands the draft names: those starting with what is typed after /, none once a space is typed', () => {
    expect(SlashCommand.matching('/de', false)?.map((command) => command.name)).toEqual(['detach', 'deny'])
    expect(SlashCommand.matching('/zz', false)).toEqual([])
    expect(SlashCommand.matching('/model gpt', false)).toBeUndefined()
    expect(SlashCommand.matching('hello', false)).toBeUndefined()
  })

  test("the context meter: the app's arithmetic over what the CLI reports, unknown when either is missing, never guessed", () => {
    expect(ContextMeter.of(undefined, undefined)).toMatchObject({ percent: undefined, text: 'unknown', warns: false })
    expect(ContextMeter.of(undefined, 8192)).toMatchObject({ percent: undefined, text: 'not reported' })
    expect(ContextMeter.of(820, 8192)).toMatchObject({ percent: 10, warns: false, text: '10% · 820 of 8,192 tokens' })
    expect(ContextMeter.of(7000, 8192)).toMatchObject({ percent: 85, warns: true })
    expect(ContextMeter.of(9000, 8192).percent).toBe(100)
    expect(ContextMeter.short(2140)).toBe('2.1K')
    expect(ContextMeter.short(820)).toBe('820')
  })

  test("a private session's words: the Mac app's", () => {
    expect(PrivateMode.words('nothingWritten')).toBe('Nothing of this session is written: no transcript, no recall in later sessions.')
  })

  test("an attachment's kind is read from its path: a trailing / a folder, .pdf a PDF, and the CLI's own image, audio and video extensions (media.cpp's medium_of)", () => {
    expect(sessionSamples.attachChoices.map((choice) => [choice.path, AttachmentKind.of(choice.path)])).toEqual([
      ['notes/wal-design.md', 'file'],
      ['~/code/ingest/', 'folder'],
      ['storage-engine-design.pdf', 'pdf'],
      ['crash-0912.png', 'image'],
      ['standup-2026-10-09.m4a', 'audio'],
      ['repro-crash.mov', 'video'],
    ])
    for (const image of ['.png', '.jpg', '.jpeg', '.gif', '.webp', '.bmp', '.tif', '.tiff', '.heic', '.heif']) expect(AttachmentKind.of(`a${image}`), image).toBe('image')
    for (const audio of ['.wav', '.mp3', '.m4a', '.aac', '.flac', '.ogg', '.oga', '.opus', '.aiff', '.aif', '.wma']) expect(AttachmentKind.of(`a${audio}`), audio).toBe('audio')
    for (const video of ['.mp4', '.mov', '.m4v', '.mkv', '.webm', '.avi', '.wmv', '.mpg', '.mpeg']) expect(AttachmentKind.of(`a${video}`), video).toBe('video')
    expect(AttachmentKind.of('Crash.PNG'), 'the case of the extension does not matter, as the CLI lowercases it').toBe('image')
    expect(AttachmentKind.of('src/**/*.py')).toBe('file')
    expect(AttachmentKind.of('png')).toBe('file')
    expect(AttachmentKind.of('archive.tar.gz')).toBe('file')
  })
})
