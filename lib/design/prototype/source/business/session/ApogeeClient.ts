import type { Clock } from '../../infrastructure/clock/Clock'
import type { DiscoveryOutcome, StandInChild } from '../../infrastructure/process/StandInChild'
import type { ChatLaunch } from './ChatLaunch'
import type { ChatSession, ChatStarting } from './ChatSession'
import { MachineSession } from './MachineSession'

/**
 * The prototype's chat starter (design 1d), the Mac app's `ApogeeClient` in
 * name: the binary located by the stand-in's mode, each chat its rule's
 * recording replayed on the clock.
 */
export class ApogeeClient implements ChatStarting {
  constructor(
    readonly child: StandInChild,
    readonly clock: Clock,
  ) {}

  async locateBinary(): Promise<DiscoveryOutcome> {
    return this.child.locateBinary()
  }

  openChat(_binary: string, launch: ChatLaunch = {}): ChatSession {
    return MachineSession.open(this.child, this.clock, launch)
  }
}
