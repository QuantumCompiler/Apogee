import { ChatLaunch } from '../../../business/session/ChatLaunch'
import { MachineProtocol } from '../../../data/protocol/MachineProtocol'

/**
 * The execute door's fields (the CLI's 27s, 41a-41c): `apogee execute` under a
 * suite or bare on one model, opened in a folder, and the suite's symphonies
 * offered to its chat model with `--orchestrate`. The Mac app's
 * `SessionLibraryViewModel.ExecuteForm` (`SessionLibraryViewModel.swift:89`).
 */
export interface ExecuteForm {
  /** `--suite`; `undefined` leaves it to the config: its default suite, else bare on the model. */
  readonly suite: string | undefined
  /** `-m`; empty is the suite's chat member, or the default backend. */
  readonly model: string
  /** The folder it opens in; the home folder when `undefined`. */
  readonly folder: string | undefined
  /** `--orchestrate`, a suite's alone. */
  readonly orchestrate: boolean
}

export const ExecuteForm = {
  empty(): ExecuteForm {
    return { suite: undefined, model: '', folder: undefined, orchestrate: false }
  },

  /** The launch, as typed: orchestrate only where `orchestrates` says a suite is there to offer. */
  launch(form: ExecuteForm, orchestrates: boolean): ChatLaunch {
    const model = form.model.trim()
    return {
      ...(model === '' ? {} : { settings: { model } }),
      execute: { ...(form.suite === undefined ? {} : { suite: form.suite }), orchestrate: form.orchestrate && orchestrates },
      ...(form.folder === undefined ? {} : { folder: form.folder }),
    }
  },

  /** The command exactly as it will run, as the sheet shows it: its machine-mode flags left out, the folder a `cd` before it. */
  command(form: ExecuteForm, orchestrates: boolean): string {
    const machine = ['--output-format', '--input-format', MachineProtocol.streamFormat]
    const words = ChatLaunch.arguments(ExecuteForm.launch(form, orchestrates)).filter((word) => !machine.includes(word))
    const line = ['apogee', ...words].join(' ')
    return form.folder === undefined ? line : `cd ${form.folder} && ${line}`
  },
}
