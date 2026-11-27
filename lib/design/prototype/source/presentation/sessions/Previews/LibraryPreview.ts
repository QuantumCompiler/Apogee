import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { ApogeeClient } from '../../../business/session/ApogeeClient'
import type { ChatLaunch } from '../../../business/session/ChatLaunch'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../infrastructure/process/StandInChild'
import { sessionSamples, streamingRules } from '../../session/Previews/SessionPresets'
import { SessionLibraryViewModel } from '../ViewModels/SessionLibraryViewModel'
import { withLibraryRules } from './LibraryRules'

/** How a preview's library is set up: its rules, the session it opens and what that session writes, how far in it stands, and what the person does then. */
export interface LibraryPreviewOptions {
  /** The stand-in's rules: the Streaming session's and the library's unless named. */
  readonly rules?: Rules
  readonly apogee?: ApogeeMode
  /** The session opened; Streaming's (a new chat) unless named, none with `unopened`. */
  readonly launch?: ChatLaunch
  readonly unopened?: boolean
  /** What the open session is handed before the clock starts: an attach, its turns. */
  readonly before?: (library: SessionLibraryViewModel) => void
  /** Start ticks: how far in the frame stands. */
  readonly ticks?: number
  /** What the person does once it stands. */
  readonly then?: (library: SessionLibraryViewModel) => void | Promise<void>
}

/** The Streaming session's turns, as the composition writes them (`AppViewModel.scenarioTurns.streaming`; a preview may not import `app`). */
const streamingTurns = ['Where do fetches start in this repo, and what cancels them?', 'So one slow host takes the whole batch down. I want the other fetches to finish and just log the timeouts. What’s the idiomatic fix on 3.12?']

/** The Streaming session's setup, as `chat` sets it up: the folder attached, its two turns. */
export function streamingSetup(library: SessionLibraryViewModel): void {
  library.current.attach('.')
  for (const text of streamingTurns) library.current.writeAhead(text)
}

/**
 * A library for a preview (design 1g), the Mac app's `SessionsPreview.library()`:
 * the stand-in answering the library's reads (`libraryRules`) and the
 * Streaming session, a still clock, the session opened and its turns written
 * before the clock starts. A place's preview may not import `app`, so it names
 * the mode rules' recording itself (1d).
 */
export function useLibraryPreview(options: LibraryPreviewOptions = {}): SessionLibraryViewModel {
  const [model] = useState(() => {
    const streaming = streamingRules('chat')
    const rules = options.rules ?? withLibraryRules({ chats: streaming.chats, reads: streaming.reads, commands: streaming.commands })
    const child = new StandInChild({ rules, apogee: options.apogee ?? 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ start: options.ticks ?? 1, still: true })
    const library = new SessionLibraryViewModel(new ApogeeClient(child, clock), new ReadRunner(child), new JobStarter(child, clock), clock, sessionSamples)
    return { library, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      await model.library.start()
      if (!options.unopened) {
        await model.library.replace(options.launch ?? {})
        ;(options.before ?? (options.launch ? () => {} : streamingSetup))(model.library)
      }
      model.clock.start()
      await new Promise((resolve) => setTimeout(resolve, 0))
      await model.library.inspectorAppeared()
      await options.then?.(model.library)
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.library
}
