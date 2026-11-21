import { Observable } from '../Observation/Observable'

/** How fast a clip plays on the prototype's clock: 0.4 s of it a tick, the old `player`'s rate. */
export const secondsPerTick = 0.4

/** `m:ss.s`, the old prototype's `clockText`: 65.25 is `1:05.3`. */
export function clockText(seconds: number): string {
  const minutes = Math.floor(seconds / 60)
  return `${minutes}:${(seconds - minutes * 60).toFixed(1).padStart(4, '0')}`
}

/** What a player draws: the old prototype's `player()` face (`main.html` at `b375fac9`, "media: shared"). */
export interface MediaPlayerFace {
  /** The frame's aspect ratio, `width / height`. */
  readonly aspect: string
  /** Top left: the frame's size and rate. */
  readonly dims: string
  /** Top right: a draft's badge. */
  readonly badge: string
  readonly playing: boolean
  /** How far in, 0 to 100, to a tenth. */
  readonly pct: number
  /** `0:02.1 / 0:05.0`. */
  readonly time: string
  /** After the bar: the frames, or the scenes. */
  readonly right: string
  /** Marks on the bar, each a share 0 to 100: a composition's scene starts. */
  readonly marks: readonly number[]
  /** Under the play button: what it is paused, the frame it is on playing. */
  readonly label: string
  /** A caption over the picture, when one is said now; empty for none. */
  readonly caption: string
  readonly toggleLabel: 'Play' | 'Pause'
}

/** What the owner says of the medium: its frame, and its words. */
export interface MediaPlayerOptions {
  readonly width: number
  readonly height: number
  readonly dims?: string
  readonly badge?: string
  readonly right?: string
  /** Each a time in seconds. */
  readonly marks?: readonly number[]
  readonly pausedLabel?: string
  readonly playingLabel?: (at: number) => string
  readonly caption?: (at: number) => string
}

interface PlayState {
  readonly key: string | undefined
  readonly on: boolean
  /** Where it stood when last started or stopped, in seconds. */
  readonly base: number
  /** The tick it was started or stopped at. */
  readonly t0: number
}

/**
 * The one player of the app's media (design 1k, promoted to `styles` for
 * Video, Compose and Voice): the old prototype's `play` state and its
 * `player` and `seek`. It decodes no real video: one thing plays at a time,
 * by its key (`clip:<id>`, `proj:<id>`), and its position is computed from
 * the clock's ticks at 0.4 s of medium a tick, so a still frame stands still.
 * Its owner hands it the clock's tick count and redraws on each tick while
 * something plays.
 */
export class Playback extends Observable {
  private state: PlayState = { key: undefined, on: false, base: 0, t0: 0 }

  constructor(private readonly now: () => number) {
    super()
  }

  /** The key playing or paused now, if any. */
  get key(): string | undefined {
    return this.state.key
  }

  /** Whether something is playing: what its owner redraws on each tick for. */
  get on(): boolean {
    return this.state.on
  }

  /** Where `key` stands, in seconds, at most `duration`; 0 for any other key. */
  position(key: string, duration: number): number {
    const state = this.state
    if (state.key !== key) return 0
    const at = state.on ? state.base + (this.now() - state.t0) * secondsPerTick : state.base
    return Math.min(at, duration)
  }

  /** Whether `key` is playing: started, and not yet at its end. */
  playing(key: string, duration: number): boolean {
    return this.state.key === key && this.state.on && this.position(key, duration) < duration
  }

  /** Play or pause `key`: paused where it stands, or played from there -- from the start once it has ended. */
  toggle(key: string, duration: number): void {
    const at = this.position(key, duration)
    if (this.playing(key, duration)) this.set({ key, on: false, base: at, t0: this.now() })
    else this.set({ key, on: true, base: at >= duration ? 0 : at, t0: this.now() })
  }

  /** Play `key` from `at` seconds: a scene's or a sentence's start. */
  seek(key: string, at: number): void {
    this.set({ key, on: true, base: at, t0: this.now() })
  }

  /** `key` paused at `at` seconds: where a preset's frame stands. */
  hold(key: string, at: number): void {
    this.set({ key, on: false, base: at, t0: this.now() })
  }

  /** What the player draws for `key`, `duration` seconds long. */
  face(key: string, duration: number, options: MediaPlayerOptions): MediaPlayerFace {
    const at = this.position(key, duration)
    const playing = this.playing(key, duration)
    const share = (seconds: number) => (duration > 0 ? Math.round((seconds / duration) * 1000) / 10 : 0)
    return {
      aspect: `${options.width} / ${options.height}`,
      dims: options.dims ?? '',
      badge: options.badge ?? '',
      playing,
      pct: share(at),
      time: `${clockText(at)} / ${clockText(duration)}`,
      right: options.right ?? '',
      marks: (options.marks ?? []).map(share),
      label: playing && options.playingLabel ? options.playingLabel(at) : (options.pausedLabel ?? ''),
      caption: options.caption?.(at) ?? '',
      toggleLabel: playing ? 'Pause' : 'Play',
    }
  }

  private set(state: PlayState): void {
    this.state = state
    this.changed()
  }
}
