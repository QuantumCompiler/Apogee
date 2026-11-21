import recipeShow from '@recorded/business/reads/Fixtures/task-recipe-show.json?raw'
import { describe, expect, test } from 'vitest'
import { TaskRecipe } from '../../../source/business/reads/Tasks'
import { ReadDocument } from '../../../source/data/protocol/ReadDocument'
import { NewTaskForm } from '../../../source/presentation/tasks/ViewModels/NewTaskForm'

const digest = TaskRecipe.read(ReadDocument.from(recipeShow) as ReadDocument) as TaskRecipe
const goal = 'Make tests/test_parsers.py pass on Python 3.12'

describe('the New task sheet’s command line and “The same run as typing”, ported line for line from the old section (design 1p)', () => {
  test('as the sheet opens: --tools, writing files granted (the old default), the suite and machine mode; the goal an ellipsis until written', () => {
    const form = NewTaskForm.opened('fast')
    expect(form.command).toBe('apogee task run "…" --tools --allow write_file --suite fast --output-format stream-json')
    expect(form.cantStart).toBe(true)
  })

  test('the goal, quoted as the old line quotes it, its double quotes escaped; the words run are the line’s, unquoted', () => {
    const form = NewTaskForm.opened('fast').with({ goal: ' say "hi" ' })
    expect(form.command).toBe('apogee task run "say \\"hi\\"" --tools --allow write_file --suite fast --output-format stream-json')
    expect(form.arguments).toEqual(['task', 'run', 'say "hi"', '--tools', '--allow', 'write_file', '--suite', 'fast', '--output-format', 'stream-json'])
    expect(form.cantStart).toBe(false)
  })

  test('each flag: --require quoted, --require-file plain, --rounds only off 8, writes and commands by their grants, the suite named', () => {
    const form = NewTaskForm.opened('fast').with({ goal, require: '0 failed', requireFile: 'REPORT.md', rounds: '16', writes: 'none', cmds: 'allow', suite: 'vast' })
    expect(form.command).toBe(`apogee task run "${goal}" --tools --require "0 failed" --require-file REPORT.md --rounds 16 --allow run_command --suite vast --output-format stream-json`)
    expect(form.arguments).toEqual(['task', 'run', goal, '--tools', '--require', '0 failed', '--require-file', 'REPORT.md', '--rounds', '16', '--allow', 'run_command', '--suite', 'vast', '--output-format', 'stream-json'])
    expect(NewTaskForm.opened('fast').with({ goal, rounds: '8' }).command).not.toContain('--rounds')
  })

  test('the newtask frame’s line: both granted', () => {
    expect(NewTaskForm.opened('fast').with({ goal, writes: 'allow', cmds: 'allow' }).command).toBe(`apogee task run "${goal}" --tools --allow write_file --allow run_command --suite fast --output-format stream-json`)
  })

  test('a recipe: --recipe and its input shell-quoted, the grants it suggests given only when ticked, in its order; writes and commands left out', () => {
    const form = NewTaskForm.opened('fast').withRecipe(digest).with({ input: 'docs', grants: ['write_file'] })
    expect(form.command).toBe('apogee task run --recipe digest-folder --input docs --allow write_file --suite fast --output-format stream-json')
    expect(form.arguments).toEqual(['task', 'run', '--recipe', 'digest-folder', '--input', 'docs', '--allow', 'write_file', '--suite', 'fast', '--output-format', 'stream-json'])
    expect(NewTaskForm.opened('fast').withRecipe(digest).with({ input: 'my docs' }).command).toBe("apogee task run --recipe digest-folder --input 'my docs' --suite fast --output-format stream-json")
    expect(NewTaskForm.opened('fast').withRecipe(digest).command).toBe("apogee task run --recipe digest-folder --input '…' --suite fast --output-format stream-json")
  })

  test('picking a recipe clears the input and the grants and puts the rounds back to 8, as the old onRecipe does', () => {
    const form = NewTaskForm.opened('fast').with({ input: 'x', grants: ['write_file'], rounds: '32' }).withRecipe(digest)
    expect([form.input, form.grants, form.rounds]).toEqual(['', [], '8'])
  })

  test('“The same run as typing”: the goal rendered and shell-quoted, the recipe’s checks first, the typed ones after, --tools, its rounds, the grants, the suite', () => {
    const form = NewTaskForm.opened('fast').withRecipe(digest).with({ input: 'docs', grants: ['write_file'], require: 'done', requireFile: 'NOTES.md' })
    const rendered = TaskRecipe.renderedGoal(digest, 'docs') as string
    expect(form.expansion).toBe(`apogee task run '${rendered}' --require-file DIGEST.md --require done --require-file NOTES.md --tools --rounds 8 --allow write_file --suite fast`)
    expect(NewTaskForm.opened('fast').with({ goal }).expansion).toBe('')
  })

  test('Start is off with no goal, or with a recipe that takes input and has none', () => {
    expect(NewTaskForm.opened('fast').with({ goal: '   ' }).cantStart).toBe(true)
    expect(NewTaskForm.opened('fast').withRecipe(digest).cantStart).toBe(true)
    expect(NewTaskForm.opened('fast').withRecipe(digest).with({ input: ' docs ' }).cantStart).toBe(false)
    expect(NewTaskForm.opened('fast').withRecipe({ ...digest, input: '', takesInput: false }).cantStart).toBe(false)
  })

  test('with no suite read (no apogee), the line names none rather than an empty --suite', () => {
    expect(NewTaskForm.opened('').with({ goal: 'x' }).command).toBe('apogee task run "x" --tools --allow write_file --output-format stream-json')
    expect(NewTaskForm.opened('').with({ goal: 'x' }).arguments).not.toContain('--suite')
  })

  test('the old shq: a word of the safe characters bare, any other single-quoted with its quotes escaped', () => {
    expect([NewTaskForm.shq('docs/a_b.md'), NewTaskForm.shq("it's"), NewTaskForm.shq('two words')]).toEqual(['docs/a_b.md', "'it'\\''s'", "'two words'"])
  })
})
