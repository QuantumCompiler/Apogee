/**
 * The names of the app's icons (design 1q), in the type page's order: first
 * the ones the old TypeSpace board's set names, under its names and in its
 * order, then every other glyph the app draws, by what it shows. Plain
 * TypeScript, so a view-model can list them; the glyphs are `IconSet.tsx`'s.
 */
export const iconNames = [
  // TypeSpace's set, those the app draws (TypeSpace.dc.html's Icons at b375fac9; its website, command, helper, notice and summarized are drawn nowhere).
  'local',
  'api',
  'subscription',
  'file',
  'folder',
  'document',
  'image',
  'audio',
  'video',
  'write',
  'search',
  'memory',
  'private',
  'attach',
  'error',
  // The places, as the sidebar draws them.
  'sessions',
  'models',
  'suites',
  'tasks',
  'graph',
  'camera',
  'voice',
  'knowledge',
  'training',
  'library',
  'review',
  // The window's own: the system strip's buttons, Settings and the theme.
  'monitor',
  'console',
  'settings',
  'theme',
  // The controls.
  'add',
  'close',
  'check',
  'more',
  'disclosure',
  'chevronDown',
  'chevronRight',
  'expandAll',
  'collapseAll',
  'send',
  'inspector',
  'newSession',
  'execute',
  'held',
  'download',
  'remove',
  'reset',
  'replan',
  'drafter',
  // Making media.
  'play',
  'pause',
  'picture',
  'photo',
  'music',
  'captions',
  'installing',
  'installed',
  'installStopped',
] as const

export type IconName = (typeof iconNames)[number]
