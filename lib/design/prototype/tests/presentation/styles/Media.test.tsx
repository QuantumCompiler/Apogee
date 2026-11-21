import installDone from '@recorded/business/media/Fixtures/install-video-done.txt?raw'
import installFailedErr from '@recorded/business/media/Fixtures/install-video-failed.err?raw'
import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { InstallCard } from '../../../source/presentation/styles/Components/InstallCard'
import { InstallConsentSheet } from '../../../source/presentation/styles/Components/InstallConsentSheet'
import { MediaPlayer } from '../../../source/presentation/styles/Components/MediaPlayer'
import { PhaseRow } from '../../../source/presentation/styles/Components/PhaseRow'
import { installSheetWords } from '../../../source/presentation/styles/Media/Install'
import { clockText, Playback } from '../../../source/presentation/styles/Media/Playback'

/** A clock the tests move by hand: its ticks, as the stand-in's clock counts them. */
function ticking() {
  let now = 0
  return { now: () => now, advance: (n: number) => (now += n) }
}

describe('the player: a clip or a composition played off the clock, 0.4 s of it a tick, the old prototype’s `player` and `seek` (design 1k)', () => {
  test('toggle plays from where it stands; each tick moves it 0.4 s; toggle again pauses there', () => {
    const clock = ticking()
    const playback = new Playback(clock.now)
    expect(playback.position('clip:a', 5)).toBe(0)
    playback.toggle('clip:a', 5)
    clock.advance(3)
    expect(playback.position('clip:a', 5)).toBeCloseTo(1.2)
    expect(playback.playing('clip:a', 5)).toBe(true)
    playback.toggle('clip:a', 5)
    clock.advance(3)
    expect(playback.position('clip:a', 5)).toBeCloseTo(1.2)
    expect(playback.playing('clip:a', 5)).toBe(false)
  })

  test('it stops at the clip’s end, and plays again from the start', () => {
    const clock = ticking()
    const playback = new Playback(clock.now)
    playback.toggle('clip:a', 2)
    clock.advance(10)
    expect(playback.position('clip:a', 2)).toBe(2)
    expect(playback.playing('clip:a', 2)).toBe(false)
    playback.toggle('clip:a', 2)
    expect(playback.position('clip:a', 2)).toBe(0)
    expect(playback.playing('clip:a', 2)).toBe(true)
  })

  test('seek plays from where it is put; hold stands paused there; another key’s position is its start', () => {
    const clock = ticking()
    const playback = new Playback(clock.now)
    playback.seek('proj:b', 9.6)
    clock.advance(1)
    expect(playback.position('proj:b', 30)).toBeCloseTo(10)
    playback.hold('clip:a', 2.1)
    expect(playback.position('clip:a', 5)).toBeCloseTo(2.1)
    expect(playback.playing('clip:a', 5)).toBe(false)
    expect(playback.position('proj:b', 30)).toBe(0)
  })

  test('the face: the old player’s words -- its time, its share, its label paused and playing', () => {
    const clock = ticking()
    const playback = new Playback(clock.now)
    playback.hold('clip:a', 2.1)
    const paused = playback.face('clip:a', 5, { width: 1280, height: 704, dims: '1280×704 · 24 fps', right: '121 frames', pausedLabel: '5 s · 121 frames', playingLabel: (at) => `frame ${Math.floor(at * 24) + 1} of 121` })
    expect(paused).toMatchObject({ aspect: '1280 / 704', playing: false, pct: 42, time: '0:02.1 / 0:05.0', label: '5 s · 121 frames', toggleLabel: 'Play', right: '121 frames' })
    playback.toggle('clip:a', 5)
    const playing = playback.face('clip:a', 5, { width: 1280, height: 704, pausedLabel: '', playingLabel: (at) => `frame ${Math.floor(at * 24) + 1} of 121` })
    expect(playing).toMatchObject({ playing: true, label: 'frame 51 of 121', toggleLabel: 'Pause' })
    expect(clockText(65.25)).toBe('1:05.3')
  })
})

describe('the shared media components, drawn (design 1k: promoted to styles for Video, Compose and Voice)', () => {
  test('MediaPlayer: the frame, its play button paused, its time and its bar', () => {
    const playback = new Playback(() => 0)
    playback.hold('clip:a', 2.1)
    const markup = renderToStaticMarkup(<MediaPlayer face={playback.face('clip:a', 5, { width: 1280, height: 704, dims: '1280×704 · 24 fps', badge: 'draft · 16 steps', pausedLabel: '5 s · 121 frames' })} toggle={() => {}} />)
    expect(markup).toContain('aria-label="Play"')
    expect(markup).toContain('0:02.1 / 0:05.0')
    expect(markup).toContain('width:42%')
    expect(markup).toContain('draft · 16 steps')
  })

  test('PhaseRow: done, running with its bar, pending and stopped, each said to a screen reader', () => {
    const markup = ['done', 'running', 'pending', 'stopped'].map((status) => renderToStaticMarkup(<PhaseRow phase={{ label: 'Denoise', status: status as 'done', detail: 'step 38 of 50', bar: status === 'running' ? 76 : undefined }} />))
    expect(markup[0]).toContain('aria-label="done"')
    expect(markup[1]).toContain('aria-label="running"')
    expect(markup[1]).toContain('width:76%')
    expect(markup[2]).toContain('aria-label="not started"')
    expect(markup[3]).toContain('aria-label="stopped"')
  })

  test('InstallCard: Stop while it runs, Dismiss after, a failure in the error color with its lines', () => {
    const running = renderToStaticMarkup(<InstallCard install={{ state: 'running', headline: 'Installing the video set', detail: installDone.split('\n')[1] ?? '', command: 'apogee train setup --with video' }} act={() => {}} />)
    expect(running).toContain('>Stop<')
    expect(running).toContain('class="pulse"')
    const failed = renderToStaticMarkup(<InstallCard install={{ state: 'failed', headline: 'The video set did not install', detail: installFailedErr.trim(), command: 'apogee train setup --with video' }} act={() => {}} />)
    expect(failed).toContain('>Dismiss<')
    expect(failed).toContain('var(--status-error)')
    expect(failed).toContain('-webkit-line-clamp:8')
  })

  test('InstallConsentSheet and its words: the refusal, what installing does, the command; a rebuild asked in its own words', () => {
    const words = installSheetWords('video')
    expect(words).toEqual({ title: 'Install what video needs?', body: "This downloads and installs Python packages into Apogee's own environment, once. Nothing is installed unless you say yes; the install runs here, with Stop, and what was refused runs again once it is in.", confirm: 'Install', cancel: 'Not now' })
    expect(installSheetWords('voice', 'It replaces it.')).toMatchObject({ title: "Rebuild Apogee's Python environment?", body: 'It replaces it.', confirm: 'Rebuild' })
    const markup = renderToStaticMarkup(<InstallConsentSheet said="apogee video generate: a LoRA loads through peft" body={words.body} command="apogee train setup --with video" />)
    expect(markup).toContain('a LoRA loads through peft')
    expect(markup).toContain('apogee train setup --with video')
  })
})
