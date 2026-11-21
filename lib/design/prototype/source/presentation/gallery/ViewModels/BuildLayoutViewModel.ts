import { textSizes, tokens } from '../../styles/Tokens/Tokens'
import type { ColorName, TypeStep } from '../../styles/Tokens/Tokens'
import { keyRows, layoutWords, regions, screenStates, spaceRows, windowDiagram, windowLabels } from '../Models/layout'
import type { KeyRow, WindowPart } from '../Models/layout'
import { nativeControls, nativeControlsHead } from '../Models/nativeControls'
import type { NativeControl } from '../Models/nativeControls'
import { typePage } from './TypePage'

/**
 * A line of the key map, its words and its keys (`⇧⌘F`), as Settings ›
 * Keyboard lists it: `app` hands the gallery the shell's, so the gallery
 * never imports the shell.
 */
export interface KeyLine {
  readonly label: string
  readonly keys: string
}

/**
 * The tokens the layout page reads (design 1r): the layout sizes by their
 * path, the spacing, the control heights, the radii, the type sizes at
 * Medium, the motion and the modal scrim -- the type page's reading of
 * tokens.json where it has one (`typePage()`), `styles`' tokens for the rest.
 */
export interface LayoutTokens {
  readonly layout: ReadonlyArray<{ readonly name: string; readonly px: number }>
  readonly spacing: readonly number[]
  readonly controls: ReadonlyArray<{ readonly name: string; readonly px: number }>
  readonly radii: ReadonlyArray<{ readonly name: string; readonly value: string }>
  readonly sizes: ReadonlyArray<{ readonly step: TypeStep; readonly px: number }>
  readonly motion: { readonly chevron: string; readonly pulse: string; readonly caret: string }
  /** The modal scrim, a color function with its alpha (`rgba(0,0,0,0.45)`). */
  readonly scrim: string
}

/** The tokens as tokens.json holds them. */
export function layoutTokens(): LayoutTokens {
  const page = typePage()
  const medium = textSizes.indexOf('m')
  return {
    layout: page.layout,
    spacing: page.spacing,
    controls: page.controls,
    radii: Object.entries(tokens.radiusPx).map(([name, value]) => ({ name, value: String(value) })),
    sizes: page.sizes.map((size) => ({ step: size.step, px: size.px[medium] ?? 0 })),
    motion: { chevron: tokens.motion.chevron, pulse: tokens.motion.pulse, caret: tokens.motion.caret },
    scrim: tokens.fixed.modalScrim,
  }
}

/** A part of the window, at the diagram's scale, in px. */
export interface WindowBox {
  readonly part: WindowPart
  readonly x: number
  readonly y: number
  readonly width: number
  readonly height: number
  readonly title: string
  readonly note?: string
  readonly surface: ColorName
  readonly line: ColorName
}

/** One type size, as the page lists it: its variable, its sample and its size at Medium. */
export interface TypeRowFace {
  readonly step: TypeStep
  readonly variable: string
  readonly sample: string
  readonly px: string
}

export interface BuildLayoutFace {
  readonly words: { readonly [Word in keyof typeof layoutWords]: string }
  readonly window: { readonly scale: number; readonly width: number; readonly height: number; readonly boxes: readonly WindowBox[] }
  readonly regions: ReadonlyArray<{ readonly name: string; readonly size: string; readonly words: string }>
  readonly keys: ReadonlyArray<{ readonly words: string; readonly keys: string }>
  readonly states: ReadonlyArray<{ readonly name: string; readonly words: string }>
  readonly controls: { readonly head: NativeControl; readonly rows: readonly NativeControl[] }
  readonly type: readonly TypeRowFace[]
  readonly space: ReadonlyArray<{ readonly name: string; readonly words: string }>
}

/** A placeholder in the page's words: `{layout:sidebar}`, `{key:Search sessions}`. */
const placeholder = /\{(layout|control|radius|motion|spacing|scrim|key):([^}]+)\}/g

/** A motion's duration as words: `1.4s ease-in-out, …` is `1.4 s`. */
function duration(motion: string): string {
  const found = /^(\d*\.?\d+)(ms|s)\b/.exec(motion)
  if (!found) throw new Error(`the motion "${motion}" starts with no duration`)
  return `${found[1]} ${found[2]}`
}

/** How dark a scrim is: `rgba(0,0,0,0.45)` is `45%`. */
function darkness(scrim: string): string {
  const alpha = /^rgba?\(([^)]*)\)$/.exec(scrim.replace(/\s+/g, ''))?.[1]?.split(',')[3]
  if (alpha === undefined) throw new Error(`the scrim "${scrim}" has no alpha`)
  return `${Math.round(Number(alpha) * 100)}%`
}

/**
 * The layout page (design 1r): the old BuildLayout board, its sizes read from
 * the tokens and its keys from the key map `app` hands it, so neither is
 * written twice. The window diagram is drawn from the same sizes at three
 * quarters. It says where a key row and the key map disagree. Plain
 * TypeScript: the view draws it.
 */
export class BuildLayoutViewModel {
  constructor(
    readonly keyMap: readonly KeyLine[],
    readonly tokens: LayoutTokens = layoutTokens(),
    readonly rows: readonly KeyRow[] = keyRows,
  ) {}

  /** Everything the page draws. */
  face(): BuildLayoutFace {
    return {
      words: Object.fromEntries(Object.entries(layoutWords).map(([name, text]) => [name, this.fill(text)])) as BuildLayoutFace['words'],
      window: this.window(),
      regions: regions.map((region) => ({ name: region.name, size: this.fill(region.size), words: this.fill(region.words) })),
      keys: this.rows.map((row) => ({ words: row.words, keys: row.lines.map((line) => this.keysOf(line)).join(' · ') })),
      states: screenStates.map((state) => ({ ...state })),
      controls: { head: nativeControlsHead, rows: nativeControls },
      type: this.tokens.sizes.map((size) => ({ step: size.step, variable: `--t-${size.step}`, sample: `Apogee ${size.step}`, px: `${size.px} px` })),
      space: spaceRows.map((row) => ({ name: row.name, words: this.fill(row.words) })),
    }
  }

  /**
   * Where the key rows and the key map disagree, each a sentence (design 1r's
   * guardrail): a line of the map no row names, and a line the page names that
   * the map does not hold.
   */
  keyProblems(): string[] {
    const named = new Set(this.named())
    const found: string[] = []
    for (const label of named) {
      if (!this.keyMap.some((line) => line.label === label)) found.push(`the layout page names the key "${label}", which the key map does not hold`)
    }
    const onRows = new Set(this.rows.flatMap((row) => row.lines))
    for (const line of this.keyMap) {
      if (!onRows.has(line.label)) found.push(`the key map’s line "${line.label}" (${line.keys}) is on no row of Navigation and keys`)
    }
    return found
  }

  /** Every key-map line the page names, in the order it names them: the rows', then the words'. */
  private named(): string[] {
    const inWords = [...Object.values(layoutWords), ...Object.values(windowLabels).flatMap((label) => [label.title, label.note ?? ''])].flatMap((text) =>
      [...text.matchAll(placeholder)].filter((match) => match[1] === 'key').map((match) => match[2] ?? ''),
    )
    return [...this.rows.flatMap((row) => row.lines), ...inWords]
  }

  /** A key-map line's keys, `?` where the map lacks it (and `keyProblems` says so). */
  private keysOf(label: string): string {
    return this.keyMap.find((line) => line.label === label)?.keys ?? '?'
  }

  /** A layout size by its path (`settings.width`). */
  private size(path: string): number {
    const found = this.tokens.layout.find((size) => size.name === path)
    if (!found) throw new Error(`the layout page names the size layout:${path}, which the tokens do not hold`)
    return found.px
  }

  /** One `{kind:name}` read: sizes named together (`a|b`) said once when they agree, each when they differ. */
  private read(kind: string, name: string): string {
    const each = (one: string): string => {
      switch (kind) {
        case 'layout':
          return String(this.size(one))
        case 'control': {
          const found = this.tokens.controls.find((control) => control.name === one)
          if (!found) throw new Error(`the layout page names the control height ${one}, which the tokens do not hold`)
          return String(found.px)
        }
        case 'radius': {
          const found = this.tokens.radii.find((radius) => radius.name === one)
          if (!found) throw new Error(`the layout page names the radius ${one}, which the tokens do not hold`)
          return found.value
        }
        case 'motion':
          return duration(this.tokens.motion[one as keyof LayoutTokens['motion']] ?? '')
        case 'spacing':
          return one === 'step' ? String(this.tokens.spacing[0] ?? '') : this.tokens.spacing.join(', ')
        case 'scrim':
          return darkness(this.tokens.scrim)
        default:
          return this.keysOf(one)
      }
    }
    const values = [...new Set(name.split('|').map(each))]
    return values.join(' / ')
  }

  /** Words with each `{kind:name}` read from the tokens or the key map. */
  private fill(text: string): string {
    return text.replace(placeholder, (_, kind: string, name: string) => this.read(kind, name))
  }

  /**
   * The window at three quarters, from the tokens: the sidebar, the header and
   * the inspector at their sizes; the sidebar's stack (Sessions, the places,
   * the system strip, Settings) and the composer placed by the diagram's own
   * geometry.
   */
  private window(): BuildLayoutFace['window'] {
    const d = windowDiagram
    const { width, height } = d.window
    const sidebar = this.size('sidebar')
    const header = this.size('header')
    const inspector = this.size('inspector')
    const inner = sidebar - 2 * d.inset
    const settingsTop = height - d.bottom - d.settings
    const systemTop = settingsTop - d.gap - d.system
    const placesTop = systemTop - d.gap - d.places
    const content = width - sidebar - inspector
    const frames: ReadonlyArray<readonly [WindowPart, number, number, number, number]> = [
      ['window', 0, 0, width, height],
      ['sidebar', 0, 0, sidebar, height],
      ['sessions', d.inset, d.top, inner, placesTop - d.gap - d.top],
      ['places', d.inset, placesTop, inner, d.places],
      ['system', d.inset, systemTop, inner, d.system],
      ['settings', d.inset, settingsTop, inner, d.settings],
      ['header', sidebar, 0, width - sidebar, header],
      ['content', sidebar, header, content, height - header],
      ['inspector', width - inspector, header, inspector, height - header],
      ['composer', sidebar + d.composerInset, height - d.composerInset - d.composerHeight, content - 2 * d.composerInset, d.composerHeight],
    ]
    const at = (points: number) => Math.round(points * d.scale * 100) / 100
    return {
      scale: d.scale,
      width: at(width),
      height: at(height),
      boxes: frames.map(([part, x, y, w, h]) => {
        const label = windowLabels[part]
        return {
          part,
          x: at(x),
          y: at(y),
          width: at(w),
          height: at(h),
          title: this.fill(label.title),
          ...(label.note === undefined ? {} : { note: this.fill(label.note) }),
          surface: label.surface,
          line: label.line,
        }
      }),
    }
  }
}
