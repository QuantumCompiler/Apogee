// The pane's drawing (the render hook's answer for `{ component: 'Pane',
// requestId: 'apogee' }`): the lines words.ts makes, each a Text with its
// buttons after it, from the elements the surface's table gives. A press
// runs here, in the mod: Read again, the promotion's pick, and promote and
// rollback, each only after its shown-command confirmation (confirm.ts).
import type { EngineInterface, RenderElement, RenderInput } from 'claude-code'

import { sessionBinary } from '../../business/client/client'
import type { FoundBinary } from '../../business/client/client'
import { promotePlan, rollbackPlan } from '../../business/commands/plan'
import { dropPromotion, pickPromotion, readPane, shownPane } from '../../business/pane/pane'
import type { DiscoveryOptions } from '../../infrastructure/discovery/discovery'
import { confirmAndRun } from '../commands/confirm'
import { absenceText } from '../notices/absence'
import { NOT_LOOKED_TEXT, glanceText, paneLines } from './words'
import type { PaneAct } from './words'

/** Draws the pane: the absence, a widget's glance, or every line with its buttons. */
export async function drawPane($: EngineInterface, e: RenderInput<'Pane'>, discovery: DiscoveryOptions): Promise<RenderElement> {
  const { Box, Button, Text } = $.ui.resolve(e)
  const binary = await sessionBinary($)
  if (binary === undefined) {
    return <Text>{NOT_LOOKED_TEXT}</Text>
  }
  if (binary.kind === 'not-found') {
    return <Text>{absenceText(binary.skipped, discovery)}</Text>
  }
  const shown = await shownPane($)
  if (e.props.placement === 'widget') {
    return <Text>{glanceText(shown)}</Text>
  }

  return (
    <Box flexDirection="column">
      {paneLines(binary, shown).map(line => (
        <Box flexDirection="row" gap={1} marginTop={line.tone === 'heading' ? 1 : 0} paddingLeft={(line.indent ?? 0) * 2}>
          <Text bold={line.tone === 'heading' || line.tone === 'subheading'} dimColor={line.tone === 'dim'}>
            {line.text}
          </Text>
          {(line.buttons ?? []).map(button => (
            <Button key={button.key} label={button.label} onPress={() => act($, binary, button.act)} />
          ))}
        </Box>
      ))}
    </Box>
  )
}

/** What the pane draws when its drawing failed: that it did, and where to look. */
export function drawPaneFault($: EngineInterface, e: RenderInput<'Pane'>): RenderElement {
  const { Text } = $.ui.resolve(e)

  return <Text>Apogee's pane stopped before it drew; `claude --debug` says why.</Text>
}

/** Runs a press: its work goes on past the press, and an end that cannot be heard has nobody to tell. */
function act($: EngineInterface, binary: FoundBinary, what: PaneAct): void {
  pressed($, binary, what).catch(() => undefined)
}

async function pressed($: EngineInterface, binary: FoundBinary, what: PaneAct): Promise<void> {
  if (what.kind === 'reread') return readPane($, binary)
  if (what.kind === 'pick') return pickPromotion($, what.run)
  if (what.kind === 'unpick') return dropPromotion($)
  if (what.kind === 'promote') await dropPromotion($)
  const plan = what.kind === 'promote' ? promotePlan(what.run, what.backend) : rollbackPlan(what.backend)
  if (plan.kind !== 'action') {
    $.ui.toast(plan.kind === 'refused' ? `Not run: ${plan.reason}` : 'Not run.')

    return
  }
  // The pane is read back when the job ends (confirm.ts): the documents again, never a mirror of what the command said.
  $.ui.toast(await confirmAndRun($, binary, plan.action, plan.args))
}
