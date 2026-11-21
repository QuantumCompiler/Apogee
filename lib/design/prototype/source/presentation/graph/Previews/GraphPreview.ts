import { useEffect, useState } from 'react'
import agentsListBadConfig from '@recorded/business/reads/Fixtures/agents-list-bad-config.err?raw'
import { ChildRegistry } from '../../../business/jobs/ChildRegistry'
import { JobStarter } from '../../../business/jobs/SupervisedJob'
import { ReadRunner } from '../../../business/reads/ReadRunner'
import { Clock } from '../../../infrastructure/clock/Clock'
import { StandInChild } from '../../../infrastructure/process/StandInChild'
import type { ApogeeMode } from '../../../infrastructure/process/StandInChild'
import { GraphViewModel } from '../ViewModels/GraphViewModel'
import { graphPlaceRules, graphWorld } from './GraphWorlds'

/** How a preview's place is set up: its mode, what the person does once it has read, and what is done once the clock has started -- each act, then the ticks it plays. */
export interface GraphPreviewOptions {
  readonly apogee?: ApogeeMode
  readonly then?: (graph: GraphViewModel) => void | Promise<void>
  readonly script?: ReadonlyArray<readonly [(graph: GraphViewModel) => void | Promise<void>, number]>
}

async function settle(): Promise<void> {
  for (let round = 0; round < 6; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/**
 * The Code graph place for a preview (design 1o), the Mac app's
 * `GraphPreview` over its documents: the stand-in answering the place's
 * recordings, a still clock, the place's world, the place read, the person's
 * act, then the clock's ticks. It names the mode rules' recording itself, as
 * a place's preview may not import `app` (1d).
 */
export function useGraphPreview(options: GraphPreviewOptions = {}): GraphViewModel {
  const [model] = useState(() => {
    const child = new StandInChild({ rules: graphPlaceRules(), apogee: options.apogee ?? 'installed' }, { configTypoRefusal: agentsListBadConfig })
    const clock = new Clock({ still: true })
    const graph = new GraphViewModel(new ReadRunner(child), new JobStarter(child, clock, new ChildRegistry()))
    graph.useWorld(graphWorld)
    graph.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
    return { graph, clock, setUp: undefined as Promise<void> | undefined }
  })
  useEffect(() => {
    // Once, however often the effect runs (React's StrictMode runs it twice).
    model.setUp ??= (async () => {
      await model.graph.appeared()
      await options.then?.(model.graph)
      for (const [act, ticks] of options.script ?? []) {
        await settle()
        void act(model.graph)
        for (let tick = 0; tick < ticks; tick += 1) {
          model.clock.advance(1)
          await settle()
        }
      }
    })()
    // The preview's options are fixed for its life.
  }, [model, options])
  return model.graph
}
