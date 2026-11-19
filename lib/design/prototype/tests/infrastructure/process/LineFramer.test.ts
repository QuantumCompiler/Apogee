import { expect, test } from 'vitest'
import { LineFramer } from '../../../source/infrastructure/process/LineFramer'

test('a recording is cut into lines as the Mac app\'s framer cuts a pipe: no newline kept, a \\r\\n ending loses its \\r, an empty line inside kept', () => {
  expect(LineFramer.lines('a\nb\n')).toEqual(['a', 'b'])
  expect(LineFramer.lines('a\r\n\nb')).toEqual(['a', '', 'b'])
  expect(LineFramer.lines('')).toEqual([])
  expect(LineFramer.lines('\n')).toEqual([''])
})
