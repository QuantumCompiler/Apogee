import { ClipForm } from '../../../business/media/ClipForm'
import { MediaModel } from '../../../business/media/MediaEngine'
import { ClipRecord, MediaLibrary, baseName } from '../../../business/media/MediaRecords'
import { secs, stemOf, tilde } from '../../styles/Media/MediaWords'

/** A LoRA on the form: its file, its sha256 where a record holds it, its strength as typed. */
export interface FormLoRA {
  readonly path: string
  readonly sha256: string | undefined
  readonly scale: string
}

/** What the generate form makes: a new clip, a record run again, or a clip continued. */
export type FormMode = 'new' | 'from' | 'extend'
export type FormPreset = 'draft' | 'final' | 'custom'
/** How the seed is set: drawn by the CLI, given, or held from the record. */
export type SeedMode = 'draw' | 'given' | 'held'

/**
 * The generate form, as the person fills it (design 1k): the old
 * prototype's `cf` (`main.html` at `b375fac9`, "video and voice" and "video:
 * the generate form"), every field as typed. It becomes the Mac's
 * `ClipForm` -- the command a person types -- through `VideoForm.clipForm`.
 */
export interface VideoForm {
  readonly mode: FormMode
  /** The record's clip, run again or continued. */
  readonly from: string | undefined
  readonly prompt: string
  readonly image: string | undefined
  /** The store handle of its video model. */
  readonly pipeline: string
  readonly preset: FormPreset
  readonly steps: string
  readonly dur: string
  readonly size: string
  readonly fps: string
  readonly seedMode: SeedMode
  readonly seed: string
  readonly loras: readonly FormLoRA[]
  /** Whether the record's LoRAs were changed. */
  readonly loraEdited: boolean
  /** The file to write, as typed; `undefined` is the name the form picks. */
  readonly out: string | undefined
}

/** What the form is checked and named against: the store's video models, the default, the folder's names, this Mac's platform. */
export interface FormContext {
  readonly models: readonly MediaModel[]
  readonly defaultModel: string | undefined
  /** Every name the clips folder holds or a run is writing. */
  readonly taken: readonly string[]
  readonly platform: string
}

/** Where clips are made: `~/Movies/Apogee`, as the Mac's `outputFolder` (the stand-in's home). */
export const moviesFolder = '/Users/someone/Movies/Apogee'
/** This Mac's platform, as a record names it. */
export const thisPlatform = 'macos-arm64'

// `tilde` and `secs` are styles' since Compose said a record's paths and seconds too (design 1l), and `stemOf`, a clip's name stem from its prompt, since Voice names its speeches so (1m).
export { secs, stemOf, tilde }

/** The old prototype's frame count: `duration` at `fps`, rounded to the pipelines' 4k+1. */
export function framesOf(duration: number, fps: number): number {
  const n = Math.round(duration * fps)
  return 4 * Math.max(0, Math.round((n - 1) / 4)) + 1
}

/** Seconds as `%g` writes them, the CLI's `seconds_text`. */
function secondsText(seconds: number): string {
  return String(Number(seconds.toPrecision(6)))
}


/** A record's settings, or the form's new-clip base: what a flag is passed against. */
interface Base {
  readonly dur: number
  readonly fps: number
  readonly size: string
  readonly steps: number
}

const sizeOf = (record: ClipRecord) => `${record.width ?? 0}x${record.height ?? 0}`
const lora = (count: number) => `${count} LoRA${count === 1 ? '' : 's'}`

export const VideoForm = {
  /** A new clip on `pipeline`: a draft of 16 steps, 5 s at 24 fps, 1280x704, a seed drawn (the old `freshClipForm`). */
  fresh(pipeline: string, prompt = ''): VideoForm {
    return { mode: 'new', from: undefined, prompt, image: undefined, pipeline, preset: 'draft', steps: '16', dur: '5', size: '1280x704', fps: '24', seedMode: 'draw', seed: '', loras: [], loraEdited: false, out: undefined }
  },

  /** The form from a record: run again, every setting and the seed held -- or its clip continued, at the final's 50 steps (the old `formFrom`). */
  from(record: ClipRecord, mode: 'from' | 'extend'): VideoForm {
    const base = { from: record.output, prompt: record.prompt, image: undefined, pipeline: record.model, size: sizeOf(record), fps: String(record.fps ?? 24), out: undefined }
    if (mode === 'extend') return { ...base, mode: 'extend', preset: 'final', steps: '50', dur: '5', seedMode: 'draw', seed: '', loras: [], loraEdited: false }
    return {
      ...base,
      mode: 'from',
      image: record.image,
      preset: record.preset === 'draft' || record.preset === 'final' ? record.preset : 'custom',
      steps: String(record.steps ?? 50),
      dur: String(record.duration ?? 5),
      seedMode: 'held',
      seed: String(record.seed ?? ''),
      loras: record.loras.map((entry) => ({ path: entry.path, sha256: entry.sha256, scale: String(entry.scale ?? 1) })),
      loraEdited: false,
    }
  },

  /** What a flag is passed against: the record's settings run again, a continuation's 5 s at 50 steps in its frame, or the CLI's defaults. */
  base(form: VideoForm, record: ClipRecord | undefined): Base {
    if (record && form.mode === 'from') return { dur: record.duration ?? 5, fps: record.fps ?? 24, size: sizeOf(record), steps: record.steps ?? 50 }
    if (record && form.mode === 'extend') return { dur: 5, fps: record.fps ?? 24, size: sizeOf(record), steps: 50 }
    return { dur: 5, fps: 24, size: '1280x704', steps: 50 }
  },

  /** Why the form cannot run, in the old prototype's words (`clipErrors`); a prompt's absence first. */
  errors(form: VideoForm, context: FormContext): string[] {
    const errors: string[] = []
    const dur = Number(form.dur)
    const fps = Number(form.fps)
    const steps = Number(form.steps)
    if (form.mode === 'new' && form.prompt.trim() === '') errors.push('A prompt is required: say what the clip shows.')
    if (!(dur > 0)) errors.push('Seconds must be above 0.')
    else if (dur > 10) errors.push('A clip is 10 s at most. Longer videos are compositions.')
    if (!(fps >= 4 && fps <= 60)) errors.push('The frame rate is 4 to 60 fps.')
    if (!(steps >= 1 && steps <= 100) || Math.round(steps) !== steps) errors.push('Steps are a whole number from 1 to 100.')
    if (form.loras.length > 4) errors.push('4 LoRAs at most.')
    if (form.loras.some((entry) => !(Number(entry.scale) > 0 && Number(entry.scale) <= 2))) errors.push('A LoRA’s strength is above 0 and at most 2.')
    if (form.seedMode === 'given' && !/^\d{1,10}$/.test(form.seed.trim())) errors.push('A seed is a whole number.')
    const out = form.out?.trim() ?? ''
    if (form.out !== undefined && out === '') errors.push('Name the file to write.')
    if (out !== '' && (context.taken.includes(out) || context.taken.includes(`${out}.json`))) errors.push(`~/Movies/Apogee/${out} exists. A generation never writes over a file; name another.`)
    // Only where the store lists video models: a store that lists none says nothing of the pipeline, and the CLI answers.
    if (context.models.length > 0 && !context.models.some((model) => model.handle === form.pipeline)) errors.push('That pipeline isn’t in the library any more. Pick another, or pull one in Models.')
    return errors
  },

  /** The file the run writes, as the form names it (the old `clipName`): the record's stem, or the prompt's; `-draft` for a draft; never a name taken. */
  clipName(form: VideoForm, record: ClipRecord | undefined, taken: readonly string[]): string {
    let stem = record ? record.name.replace(/\.mp4$/, '').replace(/-draft$/, '').replace(/-\d+$/, '') : stemOf(form.prompt)
    if (form.preset === 'draft') stem += '-draft'
    return MediaLibrary.freeName(taken, stem, 'mp4')
  },

  /** The file the run writes: as typed, trimmed -- empty when the field was emptied, which `errors` refuses -- else the form's name for it (the old `outName`). */
  output(form: VideoForm, record: ClipRecord | undefined, taken: readonly string[]): string {
    return form.out !== undefined ? form.out.trim() : VideoForm.clipName(form, record, taken)
  },

  /** The model the run names with `-m`: none while it is the record's own or the default -- the CLI's own fallbacks. */
  model(form: VideoForm, record: ClipRecord | undefined, context: FormContext): string | undefined {
    if (record && form.mode !== 'new') return form.pipeline === record.model ? undefined : form.pipeline
    return form.pipeline === '' || form.pipeline === context.defaultModel ? undefined : form.pipeline
  },

  /** The form as the Mac's `ClipForm`: every flag it set and nothing else, against its base. */
  clipForm(form: VideoForm, record: ClipRecord | undefined, context: FormContext): ClipForm {
    const base = VideoForm.base(form, record)
    const again = record !== undefined && form.mode !== 'new'
    const clip = new ClipForm()
    if (record && form.mode === 'from') clip.start = { case: 'again', record: ClipRecord.sidecarOf(record) }
    if (record && form.mode === 'extend') clip.start = { case: 'extend', clip: record.output }
    const prompt = form.prompt.trim()
    clip.prompt = again && prompt === record.prompt ? '' : prompt
    if (form.mode === 'new' ? form.image !== undefined : form.mode === 'from' && form.image !== undefined && form.image !== record?.image) clip.image = form.image
    const model = VideoForm.model(form, record, context)
    if (model !== undefined) {
      clip.model = model
      const picked = context.models.find((candidate) => candidate.handle === model)
      if (picked && MediaModel.choosesEngine(picked, context.models)) clip.engine = picked.engine
    }
    const dur = Number(form.dur)
    const fps = Number(form.fps)
    const steps = Number(form.steps)
    if (dur !== base.dur) clip.duration = dur
    if (form.size !== base.size) clip.resolution = form.size
    if (fps !== base.fps) clip.fps = fps
    if (steps !== base.steps) {
      if (form.preset === 'draft' && steps === 16) clip.preset = 'draft'
      else if (form.preset === 'final' && steps === 50) clip.preset = 'final'
      else clip.steps = steps
    }
    if (form.seedMode === 'given') clip.seed = Number(form.seed)
    if (!again || form.mode === 'extend' || form.loraEdited) {
      if (form.loras.length) clip.loras = form.loras.map((entry) => `${entry.path}:${entry.scale}`)
      else if (again && form.mode === 'from' && record.loras.length) clip.noLoRAs = true
    }
    return clip
  },

  /**
   * The command as the old prototype draws it (design 1k's decision): the
   * prompt first and quoted, `--model`, and `-o` last, each path under `~/` --
   * the same flags as the run's argument list (`clipForm`), which the CLI
   * takes in either order.
   */
  command(form: VideoForm, record: ClipRecord | undefined, context: FormContext): string {
    const clip = VideoForm.clipForm(form, record, context)
    const quoted = (text: string) => `"${text.replace(/"/g, '\\"')}"`
    const parts = ['apogee video generate']
    if (clip.start.case === 'new') {
      parts.push(quoted(form.prompt.trim() || '…'))
    } else if (clip.prompt !== '') parts.push(quoted(clip.prompt))
    if (clip.start.case === 'again') parts.push(`--from ${tilde(clip.start.record)}`)
    if (clip.start.case === 'extend') parts.push(`--extend ${tilde(clip.start.clip)}`)
    if (clip.image !== undefined) parts.push(`--image ${tilde(clip.image)}`)
    if (clip.model !== '') parts.push(`--model ${clip.model}`)
    if (clip.engine !== undefined) parts.push(`--engine ${clip.engine}`)
    if (clip.duration !== undefined) parts.push(`--duration ${ClipForm.number(clip.duration)}`)
    if (clip.resolution !== undefined) parts.push(`--resolution ${clip.resolution}`)
    if (clip.fps !== undefined) parts.push(`--fps ${clip.fps}`)
    if (clip.preset !== 'none') parts.push(`--${clip.preset}`)
    if (clip.steps !== undefined) parts.push(`--steps ${clip.steps}`)
    if (clip.seed !== undefined) parts.push(`--seed ${clip.seed}`)
    if (clip.noLoRAs) parts.push('--no-lora')
    for (const entry of clip.loras) parts.push(`--lora ${tilde(entry)}`)
    parts.push(`-o ~/Movies/Apogee/${VideoForm.output(form, record, context.taken) || '…'}`)
    return parts.join(' ')
  },

  /**
   * What the run will say of where it came from, before it runs: the CLI's
   * own `told` lines (`video_generate.cpp`'s `seed_run`) for the form's
   * flags -- what was taken from the record, what was changed, a record made
   * on another platform, a preset's steps.
   */
  told(form: VideoForm, record: ClipRecord | undefined, context: FormContext): string[] {
    const clip = VideoForm.clipForm(form, record, context)
    const told: string[] = []
    const taken: string[] = []
    const changed: string[] = []
    if (record && form.mode !== 'new') {
      const whole = form.mode === 'from'
      if (clip.model === '') taken.push(`model ${record.model}`)
      else if (whole) changed.push(`model ${clip.model}`)
      if (clip.prompt === '') taken.push('the prompt')
      else if (whole) changed.push('the prompt')
      if (clip.resolution === undefined) taken.push(sizeOf(record))
      else if (whole) changed.push(clip.resolution)
      if (clip.fps === undefined) taken.push(`${record.fps ?? 0} fps`)
      else if (whole) changed.push(`${clip.fps} fps`)
      if (whole) {
        if (clip.duration === undefined) taken.push(`${secondsText(record.duration ?? 0)} s`)
        else changed.push(`${secondsText(clip.duration)} s`)
        if (clip.steps === undefined) taken.push(`${record.steps ?? 0} steps`)
        else changed.push(`${clip.steps} steps`)
        if (clip.seed === undefined) taken.push(`seed ${record.seed ?? 0} (held)`)
        else changed.push(`seed ${clip.seed}`)
        if (clip.engine === undefined && clip.model === '') {
          if (record.engine !== 'torch') taken.push(`engine ${record.engine}`)
        } else if (clip.engine !== undefined && clip.engine !== record.engine) changed.push(`engine ${clip.engine}`)
        if (clip.loras.length) changed.push(`${lora(clip.loras.length)} given`)
        else if (clip.noLoRAs) changed.push('no LoRAs')
        else if (record.loras.length) taken.push(`${lora(record.loras.length)} (held)`)
        if (clip.image !== undefined) changed.push('the seed image')
        else if (record.parentContinued !== undefined) taken.push(`the last frame of ${record.parentContinued}`)
        else if (record.image !== undefined) taken.push('the seed image')
        told.push(`from ${ClipRecord.sidecarOf(record)}: ${taken.join(', ')}`)
        const platform = record.device.platform
        if (platform !== undefined && platform !== context.platform) told.push(`recorded on ${platform}${record.device.device ? ` (${record.device.device})` : ''}; this is ${context.platform} -- the same seed may not make the same clip here`)
      } else told.push(`extending ${record.output} from its last frame${taken.length ? `; from its record: ${taken.join(', ')}` : ''}`)
    }
    if (clip.preset !== 'none' && clip.steps === undefined) {
      const steps = clip.preset === 'draft' ? 16 : 50
      if (record && form.mode === 'from') changed.push(`${steps} steps (--${clip.preset})`)
      else told.push(`preset: ${clip.preset} -- ${steps} steps`)
    }
    if (record && form.mode === 'from') told.push(changed.length ? `changed: ${changed.join(', ')}` : 'changed: nothing -- the same run again')
    return told
  },

  /** The record's clip, by its name, for the words that say it. */
  named: baseName,
}
