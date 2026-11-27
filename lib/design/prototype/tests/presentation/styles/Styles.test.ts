import { expect, test } from 'vitest'
import * as styles from '../../../source/presentation/styles/Styles'

test("the module's front names it, and holds the tokens, the appearance machinery, the theme service and the observation primitive (design 1e)", () => {
  expect(styles.name).toBe('Styles')
  expect(styles.themeIds).toEqual(['dark', 'light', 'contrast'])
  expect(typeof styles.rootVars).toBe('function')
  expect(typeof styles.ThemeService).toBe('function')
  expect(typeof styles.Observable).toBe('function')
})

test("its common components (design 1u): the markdown and code an answer draws, and the lexer that colours a fence", () => {
  expect(typeof styles.MarkdownText).toBe('function')
  expect(typeof styles.MarkdownDocument.parse).toBe('function')
  expect(typeof styles.CodeLexer.runs).toBe('function')
})
