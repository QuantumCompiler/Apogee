import { describe, expect, test } from 'vitest'
import ingestManyErr from '@recorded/business/jobs/Fixtures/embed-ingest-graph-many.err?raw'
import ingestManyOut from '@recorded/business/jobs/Fixtures/embed-ingest-graph-many.out?raw'
import ingestUnregistrableErr from '@recorded/business/jobs/Fixtures/embed-ingest-unregistrable.err?raw'
import ingestUnregistrableOut from '@recorded/business/jobs/Fixtures/embed-ingest-unregistrable.out?raw'
import legacyYamlErr from '@recorded/business/jobs/Fixtures/embed-ingest-legacy-yaml.err?raw'
import buildUnregistrableErr from '@recorded/business/jobs/Fixtures/graph-build-unregistrable.err?raw'
import noConfigGraphErr from '@recorded/business/jobs/Fixtures/embed-ingest-no-config-graph.err?raw'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { JobNotes } from '../../../source/business/jobs/JobNotes'
import { JobProgress } from '../../../source/business/jobs/JobProgress'
import { JobEnd, JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { BoundedTail } from '../../../source/infrastructure/process/BoundedTail'
import { ChildExit } from '../../../source/infrastructure/process/ChildExit'
import { LineFramer } from '../../../source/infrastructure/process/LineFramer'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'

const lines = (text: string) => LineFramer.lines(text)

describe("a finished run's notes, the Mac's JobNotes (GUI M22)", () => {
  test.each([
    ["apogee embed: could not register 'my notes' in config", true],
    ['apogee mcp serve: listening', true],
    ["apogee graph: warning: source tree '/Users/someone/src2' is not a directory", true],
    ['apogee knowledge: validated by utility (checker): the record agrees with its source', true],
    ['apogee set-default: x', true],
    ['apogee: /Users/someone/.apogee/config/config.yaml is in the older YAML format and still loads', false],
    ['[graph] parsing src/app.py (1/1)', false],
    ['apogee Embed: shouting', false],
    ['apogee embed:no space', false],
    ['  apogee embed: indented', false],
    ['apogee embed ingest notes file', false],
    ['apogee  embed: two spaces', false],
    ['', false],
  ])('%s is a note: %s', (line, note) => {
    expect(JobNotes.isNote(line)).toBe(note)
  })

  test('the recordings: their house-voice lines, word for word and in order, the verb-less YAML line and the narration left out', () => {
    expect(JobNotes.fromLines(lines(ingestUnregistrableErr)).lines).toEqual(lines(ingestUnregistrableErr))
    expect(JobNotes.fromLines(lines(buildUnregistrableErr)).lines).toEqual(lines(buildUnregistrableErr).slice(1))
    expect(JobNotes.fromLines(lines(legacyYamlErr)).lines).toEqual([])
    const many = lines(ingestManyErr)
    expect(JobNotes.fromLines(many).lines).toEqual([many[0], many[151], many[152]])
  })

  test("the long run's 8 KiB tail starts mid-line: that fragment is skipped, and the head note is gone from it", () => {
    const tail = BoundedTail.of(ingestManyErr)
    expect(tail.cut).toBe(true)
    expect(tail.text.startsWith('xt (file 21/150, chunk 21/150)')).toBe(true)
    const many = lines(ingestManyErr)
    expect(JobNotes.inTail(tail).lines).toEqual([many[151], many[152]])
    expect(BoundedTail.of('apogee embed: one\n').cut).toBe(false)
  })

  test('the first 20 are kept, the rest counted, and the card counts them', () => {
    const notes = JobNotes.fromLines(Array.from({ length: 25 }, (_, index) => `apogee embed: note ${index + 1}`))
    expect(notes.lines).toHaveLength(20)
    expect(notes.more).toBe(5)
    expect(notes.shown.at(-1)).toBe('… and 5 more notes')
    expect(JobNotes.fromLines(Array.from({ length: 21 }, (_, index) => `apogee embed: ${index}`)).shown.at(-1)).toBe('… and 1 more note')
  })

  test('a run that succeeded folds its notes; a failure and a stop fold none', () => {
    const done = new JobProgress()
    for (const line of lines(ingestUnregistrableOut)) done.apply({ case: 'said', line })
    done.apply({ case: 'ended', end: JobEnd.of(ChildExit.exited(0), BoundedTail.of(ingestUnregistrableErr), false) })
    expect(done.status).toEqual({ case: 'done' })
    expect(done.notices).toEqual(['1 file(s), 1 chunk(s)'])
    expect(done.notes.lines).toEqual(lines(ingestUnregistrableErr))
    const failed = new JobProgress()
    failed.apply({ case: 'ended', end: JobEnd.of(ChildExit.exited(1), BoundedTail.of(noConfigGraphErr), false) })
    expect(failed.status).toEqual({ case: 'failed', message: lines(noConfigGraphErr)[1] })
    expect(failed.notes.isEmpty).toBe(true)
    const stopped = new JobProgress()
    stopped.apply({ case: 'ended', end: JobEnd.of(ChildExit.exited(130), BoundedTail.of(ingestUnregistrableErr), true) })
    expect(stopped.notes.isEmpty).toBe(true)
  })

  test("a replayed run's end carries every note of its stderr, the head one its tail lost included -- the stand-in frames stderr as the Mac's supervisor does", async () => {
    const child = new StandInChild({ rules: { jobs: [{ arguments: ['embed', 'ingest'], answer: { stdout: ingestManyOut, stderr: ingestManyErr } }] }, apogee: 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ still: true, start: 0 })
    const job = new JobStarter(child, clock).startJob(StandInChild.binary, ['embed', 'ingest', '--graph', '--', 'fresh one', '/Users/someone/docs/many'], false)
    clock.advance(lines(ingestManyOut).length + 2)
    const end = job.updates.yielded.at(-1)
    expect(end?.case).toBe('ended')
    if (end?.case !== 'ended') return
    const many = lines(ingestManyErr)
    expect(end.end.notes.lines).toEqual([many[0], many[151], many[152]])
    expect(end.end.stderrTail.cut).toBe(true)
  })
})
