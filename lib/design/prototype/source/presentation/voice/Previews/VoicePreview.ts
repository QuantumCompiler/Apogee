import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { ChildRegistry } from '../../../business/jobs/ChildRegistry'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../infrastructure/process/StandInChild'
import type { Folders } from '../../../infrastructure/process/StandInFolders'
import { MediaPreferences } from '../../styles/Media/MediaPreferences'
import { Playback } from '../../styles/Media/Playback'
import { VoiceViewModel } from '../ViewModels/VoiceViewModel'
import { sampleText, voiceFolders, voiceWorld } from './VoiceWorlds'

/** How a preview's place is set up: its answers, its folder, its mode, the voice the window chose, what the person does once it has read, how far in a run stands, and what is done once the clock has played. */
export interface VoicePreviewOptions {
  readonly rules?: Rules
  readonly folders?: Folders
  readonly apogee?: ApogeeMode
  /** The window's chosen voice (`af_heart` unless named). */
  readonly voice?: string
  /** What the person does once the place has read, before the clock starts. */
  readonly then?: (voice: VoiceViewModel) => void | Promise<void>
  /** Start ticks: how far in a run stands. */
  readonly ticks?: number
  /** What the person does once the clock has started and what it played has landed: each act, then how many ticks it plays before the next. */
  readonly script?: ReadonlyArray<readonly [(voice: VoiceViewModel) => void | Promise<void>, number]>
}

/** Lets what the clock played land: each replayed job's `for await` takes a turn. */
async function settle(): Promise<void> {
  for (let round = 0; round < 4; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/**
 * The Voice place for a preview (design 1m), the Mac app's `PreviewSupport`
 * over its documents: the stand-in answering the Voice world's recordings and
 * `~/Music/Apogee`'s records, a still clock, the place read, the speak card
 * holding the old frame's sample, the person's act, then the clock started.
 * A place's preview may not import `app`, so it names the mode rules'
 * recording itself (1d).
 */
export function useVoicePreview(options: VoicePreviewOptions = {}): VoiceViewModel {
  const [model] = useState(() => {
    const apogee = options.apogee ?? 'installed'
    const child = new StandInChild({ rules: options.rules ?? voiceWorld(), apogee, folders: options.folders ?? voiceFolders() }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ start: options.ticks ?? 0, still: true })
    const voice = new VoiceViewModel(new ReadRunner(child), new JobStarter(child, clock, new ChildRegistry()), child.folders, clock, new Playback(() => clock.ticks), new MediaPreferences(options.voice ?? 'af_heart'))
    voice.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
    return { voice, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      await model.voice.appeared()
      model.voice.setText(sampleText)
      await options.then?.(model.voice)
      model.clock.start()
      for (const [act, ticks] of options.script ?? []) {
        await settle()
        await act(model.voice)
        for (let tick = 0; tick < ticks; tick += 1) {
          model.clock.advance(1)
          await settle()
        }
      }
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.voice
}
