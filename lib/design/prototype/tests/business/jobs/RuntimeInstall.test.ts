import { describe, expect, test } from 'vitest'
import installDone from '@recorded/business/media/Fixtures/install-video-done.txt?raw'
import installFailed from '@recorded/business/media/Fixtures/install-video-failed.txt?raw'
import installFailedErr from '@recorded/business/media/Fixtures/install-video-failed.err?raw'
import { RuntimeInstall } from '../../../source/business/jobs/RuntimeInstall'
import { JobEnd, JobUpdate } from '../../../source/business/jobs/SupervisedJob'
import { BoundedTail } from '../../../source/infrastructure/process/BoundedTail'
import { ChildExit } from '../../../source/infrastructure/process/ChildExit'

const said = (text: string) => text.split('\n').map((line) => JobUpdate.said(line))
const ended = (status: number, stderr = '', interrupted = false) => JobUpdate.ended(JobEnd.of(ChildExit.exited(status), BoundedTail.of(stderr), interrupted))
const args = ['train', 'setup', '--with', 'video']

function install(updates: readonly JobUpdate[]): RuntimeInstall {
  const runtime = new RuntimeInstall(args)
  for (const update of updates) runtime.apply(update)
  return runtime
}

describe("a runtime install as its card draws it, folded from what it said (the Mac's RuntimeInstall)", () => {
  test('running: its headline, the line it is on, the command as typed', () => {
    const running = install(said(installFailed))
    expect(running.phase).toBe('running')
    expect(running.headline).toBe('Installing the video set')
    expect(running.detail).toBe("installing the 'video' set: torch>=2.6 diffusers>=0.35 transformers>=4.46 accelerate>=1.0 ftfy>=6.1 sentencepiece>=0.2 peft>=0.13 ...")
    expect(running.command).toBe('apogee train setup --with video')
    expect(running.set).toBe('video')
  })

  test('install-video-failed: failed in the CLI’s own words, stderr’s apogee line and pip’s under it', () => {
    const failed = install([...said(installFailed), ended(1, installFailedErr)])
    expect(failed.phase).toBe('failed')
    expect(failed.headline).toBe('The video set did not install')
    expect(failed.failure).toBe("apogee train: pip install for the 'video' set failed (exit code 1):\nERROR: Could not install packages: this sandbox's pip is a stub with no network")
    expect(failed.detail).toBe(failed.failure)
  })

  test('install-video-done: installed, its last line said', () => {
    const done = install([...said(installDone), ended(0)])
    expect(done.phase).toBe('installed')
    expect(done.headline).toBe('Installed the video set')
    expect(done.detail).toBe('python env: at /Users/someone/.apogee/training/venv; sets: video')
    expect(done.failure).toBeUndefined()
  })

  test('stopped before it finished: the line it was on', () => {
    const stopped = install([...said(installFailed), ended(130, '', true)])
    expect(stopped.phase).toBe('stopped')
    expect(stopped.headline).toBe("The video set's install was stopped before it finished")
  })
})
