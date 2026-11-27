import { ReadOutcome } from '../../../business/reads/ReadRunner'

/**
 * Where one of the Library's lists stands: not read yet, read, no apogee, or
 * a failed read -- said where the list is drawn, in the CLI's words (GUI 4h).
 * The Mac app's `LibraryListing` (`LibraryViewModels.swift:175-195`). A
 * failed refresh keeps the rows read before; it is never silently kept.
 */
export type LibraryListing = { readonly case: 'notRead' } | { readonly case: 'read' } | { readonly case: 'missing' } | { readonly case: 'failed'; readonly message: string }

export const LibraryListing = {
  notRead: { case: 'notRead' } as LibraryListing,
  read: { case: 'read' } as LibraryListing,
  missing: { case: 'missing' } as LibraryListing,

  /** The line drawn with the list of `what`; `undefined` when there is nothing to say. */
  text(listing: LibraryListing, what: string): string | undefined {
    switch (listing.case) {
      case 'notRead':
      case 'read':
        return undefined
      case 'missing':
        return `Apogee isn't installed, so there are no ${what} to list.`
      case 'failed':
        return `The ${what} could not be read: ${listing.message}`
    }
  },

  /** A list read: its document's rows, or why there are none. */
  of(outcome: ReadOutcome): LibraryListing {
    return outcome.case === 'document' ? LibraryListing.read : { case: 'failed', message: ReadOutcome.failure(outcome) ?? 'apogee printed nothing to read' }
  },
}

/** A path's folder and file name, as the folders hold a file. */
export function splitPath(path: string): { readonly folder: string; readonly name: string } {
  const at = path.lastIndexOf('/')
  return at < 0 ? { folder: '', name: path } : { folder: path.slice(0, at), name: path.slice(at + 1) }
}
