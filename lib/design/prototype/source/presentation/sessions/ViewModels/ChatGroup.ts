import type { ChatSummary } from '../../../business/reads/Chats'

/** A date group's title, in the design's order. */
export type ChatGroupTitle = 'Today' | 'Yesterday' | 'Previous 7 days' | 'Earlier'

/** A date group of the session list: the Mac app's `ChatGroup` (`SessionLibraryViewModel.swift:1440`). */
export interface ChatGroup {
  readonly title: ChatGroupTitle
  readonly chats: readonly ChatSummary[]
}

const day = 86_400_000

/**
 * Whole calendar days from `updated` to `now`, each day a UTC day: the
 * stand-in's "now" is a fixed instant in UTC (design 1d), so a group never
 * moves with the viewer's zone. `undefined` for a stamp that does not parse.
 */
function daysBefore(updated: string | undefined, now: Date): number | undefined {
  if (updated === undefined) return undefined
  const at = Date.parse(updated)
  if (Number.isNaN(at)) return undefined
  return Math.floor(now.getTime() / day) - Math.floor(at / day)
}

const months = ['Jan', 'Feb', 'Mar', 'Apr', 'May', 'Jun', 'Jul', 'Aug', 'Sep', 'Oct', 'Nov', 'Dec'] as const
const weekdays = ['Sun', 'Mon', 'Tue', 'Wed', 'Thu', 'Fri', 'Sat'] as const

export const ChatGroup = {
  order: ['Today', 'Yesterday', 'Previous 7 days', 'Earlier'] as readonly ChatGroupTitle[],

  /** The group `chat` falls in against `now`: today (or later), yesterday, within the week, or earlier; a row with no readable date is Earlier. */
  of(chat: ChatSummary, now: Date): ChatGroupTitle {
    const days = daysBefore(chat.updated, now)
    if (days === undefined) return 'Earlier'
    if (days <= 0) return 'Today'
    if (days === 1) return 'Yesterday'
    if (days <= 7) return 'Previous 7 days'
    return 'Earlier'
  },

  /** `chats` in the design's groups, in its order, empty groups left out, each keeping the list's order. */
  grouping(chats: readonly ChatSummary[], now: Date): ChatGroup[] {
    return ChatGroup.order.flatMap((title) => {
      const rows = chats.filter((chat) => ChatGroup.of(chat, now) === title)
      return rows.length === 0 ? [] : [{ title, chats: rows }]
    })
  },

  /**
   * When a row last changed, as the list shows it: the time today or
   * yesterday (`14:03`, the old prototype's 24-hour form), the weekday within
   * the week (`Tue`), the date before that (`Oct 2`); empty with no date.
   */
  when(chat: ChatSummary, now: Date): string {
    const days = daysBefore(chat.updated, now)
    if (days === undefined || chat.updated === undefined) return ''
    const at = new Date(chat.updated)
    if (days <= 1) return `${String(at.getUTCHours()).padStart(2, '0')}:${String(at.getUTCMinutes()).padStart(2, '0')}`
    if (days <= 7) return weekdays[at.getUTCDay()] ?? ''
    return `${months[at.getUTCMonth()] ?? ''} ${at.getUTCDate()}`
  },
}
