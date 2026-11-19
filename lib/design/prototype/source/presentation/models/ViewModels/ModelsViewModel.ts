/**
 * The models module's stub view-model: its name, and the two hooks the
 * composition wires (design 1e) -- the tab the monitor's "Open models
 * library" picks, the old prototype's `modelsTab`, and `appeared()`, called
 * each time Models comes into view -- until 1h ports it.
 */
export class ModelsViewModel {
  readonly name = 'Models'
  /** The tab shown: the old prototype's first, Backends, unless something picks another. */
  tab: 'backends' | 'store' = 'backends'
  /** How many times Models came into view. */
  appearances = 0

  /** Stored weights, as the monitor's "Open models library" opens it. */
  showStore(): void {
    this.tab = 'store'
  }

  appeared(): void {
    this.appearances += 1
  }
}
