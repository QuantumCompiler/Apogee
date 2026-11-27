import type { BoardEntry } from '../../../source/presentation/gallery/Models/Boards'

/**
 * Whether each preset of the stand-in's table has exactly one frame, each a
 * sentence naming the preset; none when it does. A frame naming a preset the
 * table lacks is said too.
 */
export function framesOf(presets: readonly string[], table: readonly BoardEntry[]): string[] {
  const found: string[] = []
  for (const preset of presets) {
    const frames = table.filter((entry) => entry.start === preset).map((entry) => entry.id)
    if (frames.length === 0) found.push(`the preset ${preset} has no frame in the gallery's board table`)
    if (frames.length > 1) found.push(`the preset ${preset} has ${frames.length} frames: ${frames.join(', ')}`)
  }
  for (const entry of table) {
    if (entry.start !== undefined && !presets.includes(entry.start)) found.push(`${entry.id} names the preset ${entry.start}, which the stand-in's table does not hold`)
  }
  return found
}
