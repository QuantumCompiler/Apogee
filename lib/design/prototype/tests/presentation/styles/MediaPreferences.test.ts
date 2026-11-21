import { expect, test } from 'vitest'
import { MediaPreferences } from '../../../source/presentation/styles/Media/MediaPreferences'

test('the chosen voice for new speech and compositions (design 1l, promoted from Settings on its second reader): started from the preferences, changed by one place, heard by every other', () => {
  const preferences = new MediaPreferences('af_heart')
  const heard: string[] = []
  preferences.subscribe(() => heard.push(preferences.voice))
  expect(preferences.voice).toBe('af_heart')
  preferences.chooseVoice('bm_george')
  preferences.chooseVoice('bm_george')
  expect(preferences.voice).toBe('bm_george')
  expect(heard).toEqual(['bm_george'])
})

test('whether new compositions are captioned (design 1l, 2026-11-21): on unless the preferences say otherwise, Settings’ toggle heard by every reader', () => {
  expect(new MediaPreferences('af_heart').captions).toBe(true)
  const preferences = new MediaPreferences('af_heart', false)
  const heard: boolean[] = []
  preferences.subscribe(() => heard.push(preferences.captions))
  expect(preferences.captions).toBe(false)
  preferences.toggleCaptions()
  expect(preferences.captions).toBe(true)
  expect(heard).toEqual([true])
})

test('Use this voice for compositions reaches an open new composition; a voice chosen in Settings does not (the old 7692 sets nc.voice, 6256 only the default)', () => {
  const preferences = new MediaPreferences('af_heart')
  const drafts: string[] = []
  const heard: string[] = []
  preferences.onUseForCompositions((voice) => drafts.push(voice))
  preferences.subscribe(() => heard.push(preferences.voice))
  preferences.chooseVoice('bm_george')
  expect(drafts).toEqual([])
  preferences.useForCompositions('bf_emma')
  expect([preferences.voice, drafts, heard]).toEqual(['bf_emma', ['bf_emma'], ['bm_george', 'bf_emma']])
  // The voice it already holds still reaches an open draft holding another.
  preferences.useForCompositions('bf_emma')
  expect(drafts).toEqual(['bf_emma', 'bf_emma'])
})
