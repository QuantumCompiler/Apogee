import { useSyncExternalStore } from 'react'
import type { Observable } from './Observable'

/** Draws the calling view again whenever `model` changes: Swift's `@Bindable`, for React (design 1e). */
export function useObserved(model: Observable): number {
  return useSyncExternalStore(model.subscribe, model.snapshot, model.snapshot)
}
