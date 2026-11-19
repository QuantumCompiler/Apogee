/**
 * The styles module (design 1e): the Presentation layer's floor, leaning on no
 * sibling, the Mac app's `PresentationStyles`. It holds the tokens, typed
 * (`Tokens/`, the only door to tokens.json's generated module); the
 * appearance as one value and the old prototype's theme machinery over it
 * (`Appearance/`), with the theme service; the observation every view-model
 * extends (`Observation/`); the stylesheets (`Stylesheet.ts`: the fonts,
 * the generated variables and the helmet's shared classes); and the common
 * components (`Components/`, design 1u): the markdown and code a model's
 * answer draws (`MarkdownText`, over `MarkdownDocument` and `CodeLexer`),
 * under the Mac app's names. A component a second module wants is promoted
 * here, never copied.
 */
export const name = 'Styles'

export * from './Tokens/Tokens'
export * from './Appearance/Appearance'
export { ThemeService } from './Appearance/ThemeService'
export { Observable } from './Observation/Observable'
export { CodeLexer } from './Components/CodeLexer'
export type { CodeKind, CodeRun } from './Components/CodeLexer'
export { MarkdownDocument, MarkdownLink } from './Components/MarkdownDocument'
export type { MarkdownBlock, MarkdownSpan } from './Components/MarkdownDocument'
export { MarkdownText } from './Components/MarkdownText'
