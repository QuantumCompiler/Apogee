import { Observable } from '../Observation/Observable'

/**
 * The window's media preferences, held once and shared (design 1l): the
 * voice new speech and new compositions start from, and whether new
 * compositions are captioned. 1i held them as the Settings window's own
 * preferences (`SettingsPreferences.voice` and `.captions`, from
 * `preferences-handmade.json`); Compose reads them too, so they are promoted
 * here on their second reader, as the theme service sits beside it: the
 * composition root builds it once from the preferences and hands it to
 * Settings, which shows and changes both, to Compose, whose new composition
 * starts from both, and to Voice (1m), whose "Use this voice for
 * compositions" changes the voice and an open new composition's with it.
 * Nothing persists across a reload (1i's default). The Mac app has no
 * counterpart: its `Preferences` keeps only the binary override.
 */
export class MediaPreferences extends Observable {
  /** Who an open new composition is: what Use this voice for compositions reaches besides the default. */
  private readonly drafts = new Set<(voice: string) => void>()

  constructor(
    private chosen: string,
    private captioned = true,
  ) {
    super()
  }

  /** The voice new speech and new compositions start from (`af_heart`). */
  get voice(): string {
    return this.chosen
  }

  /** Whether a new composition starts captioned (Settings' Caption new compositions, the old `mediaCaptions`). */
  get captions(): boolean {
    return this.captioned
  }

  /**
   * Settings' Voice for new speech: the voice a fresh speech or composition
   * starts from, heard by every place reading it. An open new composition
   * keeps its own, as the old `onDefaultVoice` sets the default alone.
   */
  chooseVoice(voice: string): void {
    if (voice === this.chosen) return
    this.chosen = voice
    this.changed()
  }

  /** Settings' Caption new compositions: a fresh composition starts so; an open one keeps its own box. */
  toggleCaptions(): void {
    this.captioned = !this.captioned
    this.changed()
  }

  /**
   * Voice's "Use this voice for compositions" (1m): the voice new speech and
   * compositions start from, and an open new composition's too, as the old
   * button sets `defaultVoice` and `nc.voice` together.
   */
  useForCompositions(voice: string): void {
    this.chosen = voice
    for (const draft of [...this.drafts]) draft(voice)
    this.changed()
  }

  /** Calls `listener` with the voice each Use this voice for compositions names: how an open new composition takes it. Returns what stops it. */
  onUseForCompositions(listener: (voice: string) => void): () => void {
    this.drafts.add(listener)
    return () => {
      this.drafts.delete(listener)
    }
  }
}
