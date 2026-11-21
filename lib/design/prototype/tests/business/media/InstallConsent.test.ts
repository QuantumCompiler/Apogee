import { describe, expect, test } from 'vitest'
import refusedErr from '@recorded/business/media/Fixtures/generate-refused-runtime.err?raw'
import refusedM29 from '@recorded/business/media/Fixtures/generate-refused-runtime-m29.jsonl?raw'
import installFailedErr from '@recorded/business/media/Fixtures/install-video-failed.err?raw'
import peftErr from '../../../fixtures/business/media/generate-refused-peft-handmade.err?raw'
import peftJsonl from '../../../fixtures/business/media/generate-refused-peft-handmade.jsonl?raw'
import { InstallConsent } from '../../../source/business/media/InstallConsent'
import { JSONValue } from '../../../source/data/protocol/JSONValue'

/** The `error` event's message a stream ends on, as the M29 binary writes it. */
const streamError = (text: string) => JSONValue.string(JSONValue.object(JSONValue.parse(text.trim().split('\n').at(-1) ?? ''))?.message)

describe('the install a refusal names, waiting on a yes (the Mac’s InstallConsent)', () => {
  test('before M29: read from the run’s stderr, the line that named it, in the CLI’s words', () => {
    const consent = InstallConsent.fromFailure('exited with status 1', refusedErr)
    expect(consent).toEqual({ arguments: ['train', 'setup', '--with', 'video'], said: "apogee video generate: torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video" })
    expect(consent && InstallConsent.command(consent)).toBe('apogee train setup --with video')
    expect(consent && InstallConsent.set(consent)).toBe('video')
  })

  test('since M29: read from the stream’s own error first, its words alone', () => {
    expect(InstallConsent.fromRun(streamError(refusedM29), 'x', refusedErr)?.said).toBe("torch and diffusers are not installed in Apogee's Python environment -- apogee train setup --with video")
  })

  test('the LoRA refusal (video_generate.cpp:696), hand-made: the stream’s error, and stderr’s line before M29', () => {
    expect(InstallConsent.fromRun(streamError(peftJsonl), 'x', peftErr)).toEqual({ arguments: ['train', 'setup', '--with', 'video'], said: 'a LoRA loads through peft, which the video runtime does not have yet -- apogee train setup --with video' })
    expect(InstallConsent.fromFailure('x', peftErr)?.said).toBe('apogee video generate: a LoRA loads through peft, which the video runtime does not have yet -- apogee train setup --with video')
  })

  test('a failure of any other kind asks for nothing', () => {
    expect(InstallConsent.fromRun(undefined, 'exited with status 1', "apogee video: --engine torch: 'x' is an MLX video model")).toBeUndefined()
    expect(InstallConsent.fromRun('cancelled -- nothing was written', 'x', '')).toBeUndefined()
  })

  test('a failed install names no other install, so nothing is asked next', () => {
    expect(InstallConsent.afterFailed(['train', 'setup', '--with', 'video'], 'apogee train: pip install …', installFailedErr)).toBeUndefined()
    expect(InstallConsent.afterFailed(['train', 'setup', '--with', 'voice'], undefined, 'rebuild it: apogee train setup --rebuild --with voice')?.arguments).toEqual(['train', 'setup', '--rebuild', '--yes', '--with', 'voice'])
  })

  test('one install at a time: while another runs, Install starts nothing and says why; a rebuild also waits for a run on the environment', () => {
    const consent = { arguments: ['train', 'setup', '--with', 'video'], said: '' }
    expect(InstallConsent.waits(consent, 'apogee bootstrap', undefined)).toBe('apogee bootstrap is already running: press Install again once it has finished')
    expect(InstallConsent.waits(consent, undefined, 'apogee video generate')).toBeUndefined()
    const rebuild = { arguments: ['train', 'setup', '--rebuild', '--yes', '--with', 'voice'], said: '' }
    expect(InstallConsent.waits(rebuild, undefined, 'apogee video generate')).toBe("apogee video generate is using Apogee's Python environment: press Rebuild again once it has finished")
    expect(InstallConsent.heldBack(rebuild, 'apogee bootstrap')).toBe('apogee bootstrap is already running: once it has finished, apogee train setup --rebuild --yes --with voice rebuilds the environment')
  })
})
