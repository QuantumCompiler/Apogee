import { describe, expect, test } from 'vitest'
import { AdaptersForm } from '../../../source/presentation/models/ViewModels/BackendAdapters'

describe('the adapters sheet: the whole list set in one command (the Mac’s AdaptersForm, the prototype’s adapters sheet)', () => {
  test('each path, `:<scale>` after it when typed and not 1, joined by commas; empty takes them all off', () => {
    const form = new AdaptersForm('llama3.2-1b-Q4KM', 'llamacpp', [{ path: '/a.lora.gguf', scale: 1 }, { path: '/b.lora.gguf', scale: 0.5 }])
    expect(form.arguments).toEqual(['config', 'set-backend', 'llama3.2-1b-Q4KM', '--adapters', '/a.lora.gguf,/b.lora.gguf:0.5'])
    expect(form.command).toBe('apogee config set-backend llama3.2-1b-Q4KM --adapters /a.lora.gguf,/b.lora.gguf:0.5')
    form.remove(0)
    form.remove(0)
    expect(form.arguments.at(-1)).toBe('')
    expect(form.command).toBe('apogee config set-backend llama3.2-1b-Q4KM --adapters ""')
    expect(form.applyLabel).toBe('Take them all off')
  })

  test('Add a file… adds the prototype’s sample at scale 1; an edited path and scale are taken as typed', () => {
    const form = new AdaptersForm('root', 'llamacpp', [])
    expect(form.applyLabel, 'the old sheet’s word with no row').toBe('Take them all off')
    form.add()
    expect(form.applyLabel).toBe('Apply')
    expect(form.entries).toEqual([{ path: '/Users/someone/Downloads/house-style.lora.gguf', scale: '1' }])
    form.edit(0, { scale: '1.5' })
    form.edit(0, { path: ' /x.gguf ' })
    expect(form.value).toBe('/x.gguf:1.5')
    expect(form.addLabel).toBe('Add a file…')
    expect(form.canAdd).toBe(true)
  })

  test('an mlx entry takes one adapter directory at its trained scale: no scale, one row at most', () => {
    const form = new AdaptersForm('Qwen3-8B-4bit', 'mlx', [])
    expect(form.isMLX).toBe(true)
    expect(form.addLabel).toBe('Add a directory…')
    form.add()
    expect(form.canAdd).toBe(false)
    form.edit(0, { scale: '0.5' })
    expect(form.value).toBe('/Users/someone/.apogee/training/runs/20261021-051826/adapters')
    expect(form.intro).toBe('An mlx entry applies one adapter directory, at the scale it was trained at.')
  })
})
