import { describe, expect, test } from 'vitest'
import { ExecuteForm } from '../../../source/presentation/sessions/ViewModels/ExecuteForm'

describe("the execute door's fields (the Mac app's ExecuteForm)", () => {
  test('the door’s line under coding, in ~/code/ingest, orchestrating: cd ~/code/ingest && apogee execute --tools --suite coding --orchestrate', () => {
    const form = { ...ExecuteForm.empty(), suite: 'coding', folder: '~/code/ingest', orchestrate: true }
    expect(ExecuteForm.command(form, true)).toBe('cd ~/code/ingest && apogee execute --tools --suite coding --orchestrate')
    expect(ExecuteForm.launch(form, true)).toEqual({ execute: { suite: 'coding', orchestrate: true }, folder: '~/code/ingest' })
  })

  test('bare: no folder, no suite, its model named with -m, and orchestrate dropped where nothing is there to offer', () => {
    const form = { ...ExecuteForm.empty(), model: ' root ', orchestrate: true }
    expect(ExecuteForm.command(form, false)).toBe('apogee execute --tools -m root')
    expect(ExecuteForm.launch(form, false)).toEqual({ settings: { model: 'root' }, execute: { orchestrate: false } })
  })

  test('a new door: no suite named (the config’s), no model, the home folder, not orchestrating', () => {
    expect(ExecuteForm.empty()).toEqual({ suite: undefined, model: '', folder: undefined, orchestrate: false })
    expect(ExecuteForm.command(ExecuteForm.empty(), true)).toBe('apogee execute --tools')
  })
})
