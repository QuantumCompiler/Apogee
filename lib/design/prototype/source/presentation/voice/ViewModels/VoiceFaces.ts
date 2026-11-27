import type { MediaPlayerFace } from '../../styles/Media/Playback'

/** One row of the speeches list: a speech, or the run speaking now. */
export interface SpeechRowFace {
  /** The WAV's path; the run's own while it speaks. */
  readonly id: string
  readonly file: string
  readonly text: string
  /** `af_heart · 1.0× · 0.1 s`, or `speaking · af_heart`. */
  readonly meta: string
  readonly running: boolean
  readonly current: boolean
}

/** A voice chip: its name, the CLI's default marked, the one picked. */
export interface VoiceChipFace {
  readonly id: string
  /** `default` on the CLI's own; empty on the rest. */
  readonly hint: string
  /** `am_adam, a male voice`, by Kokoro's convention. */
  readonly title: string
  readonly current: boolean
}

/** The voices of one language, as the model lists them: `AMERICAN ENGLISH · 2`. */
export interface VoiceGroupFace {
  readonly title: string
  readonly voices: readonly VoiceChipFace[]
}

/** The speak card: the old `vox` form's every word. */
export interface SpeakCardFace {
  readonly text: string
  /** `118 / 4,000`. */
  readonly count: string
  readonly countColor: string
  readonly sentencesText: string
  readonly groups: readonly VoiceGroupFace[]
  readonly speed: number
  /** `1.0×`. */
  readonly speedText: string
  readonly speedChanged: boolean
  /** Save as: as typed, else the card's own name for the file. */
  readonly out: string
  readonly errors: readonly string[]
  /** The command, as a person types it. */
  readonly command: string
  readonly cantSpeak: boolean
  readonly speaking: boolean
  /** `speaking 2 sentences as af_heart on the cpu`. */
  readonly speakingText: string
  /** Under Speak: what it speaks on, or why it cannot, or a failed run's words. */
  readonly note: string
  readonly noteColor: string
}

/** One of the waveform's 96 bars: its height (a share of the row) and its color. */
export interface WaveBarFace {
  readonly height: number
  readonly color: string
}

/** One row of the sentence table. */
export interface SentenceRowFace {
  /** `1`. */
  readonly n: string
  readonly text: string
  /** `0.05 s`. */
  readonly starts: string
  readonly lasts: string
  /** Being spoken now. */
  readonly lit: boolean
}

/** The selected speech's three actions, as the old buttons. */
export type SpeechAction = 'settings' | 'compositions' | 'finder'

/** The selected speech: its head, waveform, player, sentences, record and command. */
export interface SpeechPageFace {
  readonly shown: boolean
  readonly file: string
  readonly meta: string
  readonly actions: ReadonlyArray<{ readonly action: SpeechAction; readonly label: string; readonly kind: 'btn' | 'btn-g' }>
  readonly bars: readonly WaveBarFace[]
  readonly player: MediaPlayerFace
  /** `16-bit mono WAV · 24 kHz · 0.1 s`. */
  readonly format: string
  readonly sentences: readonly SentenceRowFace[]
  readonly measured: string
  /** The record's file name, `hello-pytorch.wav.json`. */
  readonly record: string
  readonly facts: ReadonlyArray<{ readonly label: string; readonly value: string; readonly mono: boolean }>
  readonly command: string
}

/** Everything the Voice place draws, as words and numbers: the view draws only this. */
export interface VoiceFace {
  /** The header's model line: `Kokoro-82M · PyTorch`, or why there is none. */
  readonly modelLine: string
  readonly modelTitle: string
  readonly canOpenModel: boolean
  readonly list: readonly SpeechRowFace[]
  /** `Nothing spoken yet.` with no speech and no run; empty otherwise. */
  readonly empty: string
  /** What the library passed over, and why. */
  readonly skipped: readonly string[]
  readonly card: SpeakCardFace
  readonly speech: SpeechPageFace
}
