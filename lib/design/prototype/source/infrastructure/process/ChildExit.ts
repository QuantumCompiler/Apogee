/** How a child ended: the Mac app's `ChildExit` (`InfrastructureProcess`, `RunningChild.swift:4-28`). */
export interface ChildExit {
  /** `exited`: it returned `status` from its main. `signalled`: signal `status` ended it. */
  readonly reason: 'exited' | 'signalled'
  readonly status: number
}

export const ChildExit = {
  exited(status: number): ChildExit {
    return { reason: 'exited', status }
  },

  signalled(status: number): ChildExit {
    return { reason: 'signalled', status }
  },

  succeeded(exit: ChildExit): boolean {
    return exit.reason === 'exited' && exit.status === 0
  },

  /** `exited with status 1`, `ended by signal 15`: as the Mac app says it. */
  description(exit: ChildExit): string {
    return exit.reason === 'exited' ? `exited with status ${exit.status}` : `ended by signal ${exit.status}`
  },
}
