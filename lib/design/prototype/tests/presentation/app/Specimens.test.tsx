import { renderToStaticMarkup } from 'react-dom/server'
import { describe, expect, test } from 'vitest'
import { specimens } from '../../../source/presentation/app/Previews/Specimens'
import { boards } from '../../../source/presentation/gallery/Models/Boards'
import { ComponentsPage } from '../../../source/presentation/gallery/Views/ComponentsPage'
import { specimenSections } from '../../../source/presentation/styles/Specimens/Specimen'

describe("the modules' specimens app hands the gallery (design 1q)", () => {
  test('each of the four sections holds real components, from the modules that draw them', () => {
    const by = (section: string) => [...new Set(specimens.filter((specimen) => specimen.section === section).map((specimen) => specimen.module))]
    expect(specimenSections.map((section) => [section, by(section)])).toEqual([
      ['model', ['session', 'sessions', 'styles']],
      ['turn', ['session', 'styles']],
      ['asking', ['session']],
      ['media', ['video', 'compose', 'voice']],
    ])
  })

  test('a specimen’s name is its own within its module', () => {
    const names = specimens.map((specimen) => `${specimen.module}: ${specimen.name}`)
    expect(new Set(names).size).toBe(names.length)
  })

  for (const specimen of specimens) {
    // Its view-model is set up in an effect, as its module's previews are, which markup never runs: this checks the first render only, before that setup.
    // What each draws once set up is e2e/gallery.spec.ts's Components test, which opens all four pages in a browser.
    test(`${specimen.module} · ${specimen.name}: its first render, before its view-model is set up, draws without throwing, with no undefined or NaN`, () => {
      const Render = specimen.render
      expect(renderToStaticMarkup(<Render />)).not.toMatch(/\bundefined\b|\bNaN\b/)
    })
  }

  test('each Components page draws its section’s specimens, by module, and none of another section', () => {
    const turn = renderToStaticMarkup(<ComponentsPage entry={boards.find((entry) => entry.id === 'ComponentsTurn')!} specimens={specimens} />)
    expect(turn).toContain('FOUNDATIONS · COMPONENTS 2 OF 4')
    expect(turn).toContain('Thinking section · live')
    expect(turn).toContain('The answer · markdown')
    expect(turn).not.toContain('Permission card')
    expect(turn).not.toContain('Not ported yet')
  })
})
