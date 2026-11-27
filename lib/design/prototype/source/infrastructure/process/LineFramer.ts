/**
 * A child's output, cut into lines: the Mac app's `LineFramer`
 * (`InfrastructureProcess`, `LineFramer.swift`), for a recording that arrives
 * whole. Each line comes without its newline (a `\r\n` ending loses its `\r`
 * too), and a last line with no newline is a line; the empty end after a
 * final newline is not.
 */
export const LineFramer = {
  lines(text: string): string[] {
    if (text === '') return []
    const lines = text.split('\n').map((line) => (line.endsWith('\r') ? line.slice(0, -1) : line))
    if (text.endsWith('\n')) lines.pop()
    return lines
  },
}
