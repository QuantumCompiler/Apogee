import codeAnswer from '@recorded/data/protocol/Fixtures/code-answer.jsonl?raw'
import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import { MarkdownText } from '../Components/MarkdownText'
import { ModelGlyph } from '../Components/ModelGlyph'
import type { Specimen } from '../Specimens/Specimen'

/**
 * `styles`' specimens for the gallery's Components pages (design 1q): its
 * shared components a place does not draw as a whole -- a model's kind glyph,
 * and the answer's markdown and code over the recordings -- with no frame or
 * theme of their own. The media pieces Video, Compose and Voice share (the
 * player, the phase row, the install card and prompt, the play button) are
 * those places' specimens, drawn from the faces their view-models build over
 * the recordings, since `styles` reaches no place's view-model.
 */

/** An answer's text, its deltas joined as the transcript joins them. */
function answerOf(recording: string): string {
  return recording
    .split('\n')
    .filter((line) => line.trim() !== '')
    .map((line) => JSON.parse(line) as { readonly type: string; readonly text?: string })
    .filter((event) => event.type === 'answer_delta')
    .map((event) => event.text ?? '')
    .join('')
}

export const specimens: readonly Specimen[] = [
  {
    name: 'Model kind glyph · local, cloud API, subscription',
    section: 'model',
    module: 'styles',
    item: '1h',
    render: () => (
      <div style={{ display: 'flex', gap: 16 }}>
        <ModelGlyph kind="local" label="local" />
        <ModelGlyph kind="api" label="cloud API, billed" />
        <ModelGlyph kind="subscription" label="subscription" />
      </div>
    ),
  },
  { name: 'The answer · markdown', section: 'turn', module: 'styles', item: '1u', render: () => <MarkdownText text={answerOf(markdownAnswer)} /> },
  { name: 'The answer · code in every language', section: 'turn', module: 'styles', item: '1u', render: () => <MarkdownText text={answerOf(codeAnswer)} /> },
]
