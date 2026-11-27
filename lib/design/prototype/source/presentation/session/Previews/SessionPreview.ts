import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import modelsInfoClaude from '@recorded/business/reads/Fixtures/models-info-switched-claude.json?raw'
import suitesDefaultSuite from '@recorded/business/reads/Fixtures/suites-default-suite.txt?raw'
import suitesModelsInfoRoot from '@recorded/business/reads/Fixtures/suites-models-info-root.json?raw'
import suitesModelsList from '@recorded/business/reads/Fixtures/suites-models-list.json?raw'
import modelsInfoVast from '@recorded/business/reads/Fixtures/suites-models-info-vast.json?raw'
import { ModelInfo, ModelKind, ModelRow } from '../../../business/reads/Models'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { SavedChat } from '../../../business/reads/SavedChat'
import { ApogeeClient } from '../../../business/session/ApogeeClient'
import type { ChatLaunch } from '../../../business/session/ChatLaunch'
import { ReadDocument } from '../../../data/protocol/ReadDocument'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { Answer, ApogeeMode } from '../../../infrastructure/process/StandInChild'
import { SessionViewModel } from '../ViewModels/SessionViewModel'
import { sessionSamples } from './SessionPresets'

/** How a preview's session is set up: its launch, what it sends before the clock starts, how far in it stands, and the binary's mode. */
export interface SessionPreviewOptions {
  readonly launch?: ChatLaunch
  /** Turns written before the clock starts, as a still preset writes its sends. */
  readonly sends?: readonly string[]
  /** How many ticks in the frame stands: the recording's lines played. */
  readonly ticks?: number
  readonly apogee?: ApogeeMode
  /** Not launched at all: a preset that opens no session. */
  readonly unlaunched?: boolean
  /** What the person does once it stands: a menu opened, a draft typed. */
  readonly then?: (model: SessionViewModel) => void
  /** The chat rule's answers, one a launch (design 1v): a recording with its freeze, exit delay or branches; `recording` alone unless named. */
  readonly answers?: readonly [Answer, ...Answer[]]
  /** A saved chat's `chats show` document, read back for a resumed session (1v). */
  readonly earlier?: string
  /** `models list` as recorded: `suites-models-list.json` unless named. */
  readonly models?: string
  /** What the preset does once the session is open, before its sends and the clock: a line, a restart (1v). */
  readonly before?: (model: SessionViewModel) => void | Promise<void>
}

const windows: Readonly<Record<string, number | undefined>> = {
  claude: ModelInfo.window(ReadDocument.from(modelsInfoClaude) as ReadDocument),
  root: ModelInfo.window(ReadDocument.from(suitesModelsInfoRoot) as ReadDocument),
  vast: ModelInfo.window(ReadDocument.from(modelsInfoVast) as ReadDocument),
}

/**
 * A session for a preview (design 1u), the Mac app's `SessionPreview`: the
 * stand-in answering its chat with `recording`, a still clock, and the facts
 * the CLI recorded (`suites-models-list.json`, `models info`'s window for the
 * model its session names, and a new or private chat's default suite,
 * `suites-default-suite.txt`) handed in as the owner hands them.
 * A place's preview may not import `app`, so it names the mode rules'
 * recording itself (1d).
 */
export function useSessionPreview(recording: string, options: SessionPreviewOptions = {}): SessionViewModel {
  const [model] = useState(() => {
    const launch = options.launch ?? {}
    // A resumed chat's earlier turns, as its reader reads them back (`chats show <id>`).
    const earlier = options.earlier
    const reads = earlier !== undefined && launch.resume !== undefined ? [{ arguments: SavedChat.arguments(launch.resume), answer: { stdout: earlier } }] : []
    const child = new StandInChild(
      { rules: { chats: [{ arguments: [launch.execute ? 'execute' : 'chat'], answer: options.answers ?? { stdout: recording } }], reads }, apogee: options.apogee ?? 'installed' },
      { configTypoRefusal: agentsListBadConfig },
    )
    const clock = new Clock({ start: options.ticks ?? 0, still: true })
    const session = new SessionViewModel(new ApogeeClient(child, clock), clock, launch, sessionSamples.listing, sessionSamples.attachChoices, new ReadRunner(child))
    const modelsList = ReadDocument.from(options.models ?? suitesModelsList) as ReadDocument
    session.modelKinds = ModelKind.byBackend(modelsList)
    session.modelRows = ModelRow.list(modelsList)
    session.observer = (event) => {
      if (event.case === 'event' && event.event.kind.case === 'session') session.contextWindow = windows[event.event.kind.session.model]
    }
    return { session, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      if (!options.unlaunched) {
        await model.session.start()
        // The default suite a new or private chat runs under, as the owner reads it once the chat is open.
        const launch = options.launch ?? {}
        if (model.session.connection.case === 'connected' && (options.apogee ?? 'installed') === 'installed' && launch.resume === undefined && launch.execute === undefined) {
          model.session.knowDefaultSuite(suitesDefaultSuite.trim())
        }
        await options.before?.(model.session)
        for (const text of options.sends ?? []) model.session.writeAhead(text)
        model.clock.start()
        // The frame's events land before the person's act.
        await new Promise((resolve) => setTimeout(resolve, 0))
      }
      options.then?.(model.session)
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.session
}
