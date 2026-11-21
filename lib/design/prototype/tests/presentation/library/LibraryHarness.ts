import { ChildRegistry } from '../../../source/business/jobs/ChildRegistry'
import { JobStarter } from '../../../source/business/jobs/SupervisedJob'
import { ReadRunner } from '../../../source/business/reads/ReadRunner'
import { Clock } from '../../../source/infrastructure/clock/Clock'
import { StandInChild } from '../../../source/infrastructure/process/StandInChild'
import type { ApogeeMode, Rules } from '../../../source/infrastructure/process/StandInChild'
import type { Folders } from '../../../source/infrastructure/process/StandInFolders'
import { modeRules } from '../../../source/presentation/app/Previews/ModeRules'
import { libraryPreviewRules, withLibraryFolders } from '../../../source/presentation/library/Previews/LibraryWorlds'
import type { LibraryPreset } from '../../../source/presentation/library/Previews/LibraryWorlds'
import { LibraryViewModel } from '../../../source/presentation/library/ViewModels/LibraryViewModel'

/** Lets what the stand-in answered land: each awaited read, and each replayed job's `for await`, takes a turn. */
export async function settle(): Promise<void> {
  for (let round = 0; round < 6; round += 1) await new Promise((resolve) => setTimeout(resolve, 0))
}

/** The Library place over the stand-in (design 1n): a preset's world, a still clock tests advance by hand, and the doors it speaks through, recorded. */
export function libraryPlace(options: { readonly preset?: LibraryPreset; readonly apogee?: ApogeeMode; readonly rules?: Rules; readonly folders?: Folders } = {}) {
  const apogee = options.apogee ?? 'installed'
  const child = new StandInChild({ rules: options.rules ?? libraryPreviewRules(options.preset ?? 'library'), apogee, folders: options.folders ?? withLibraryFolders() }, modeRules)
  const clock = new Clock({ still: true })
  const library = new LibraryViewModel(new ReadRunner(child), new JobStarter(child, clock, new ChildRegistry()), child.folders, clock)
  const said: string[] = []
  const sheets: string[] = []
  const asked: string[] = []
  library.say = (text) => said.push(text)
  library.present = (kind) => sheets.push(kind)
  library.dismissSheet = () => sheets.push('dismissed')
  library.ask = (kind) => asked.push(kind)
  library.connect(child.locateBinary().case === 'found' ? StandInChild.binary : undefined)
  return { library, child, clock, said, sheets, asked }
}
