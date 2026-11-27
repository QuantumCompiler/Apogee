import { ConfirmHost } from '../../shell/Views/Hosts'
import { useObserved } from '../../styles/Observation/useObserved'
import type { TasksViewModel } from '../ViewModels/TasksViewModel'

/**
 * The `taskRefused` alert (the old confirm kind): a start or resume the CLI
 * refused before it began, its stderr's last line as the CLI wrote it,
 * `apogee task:` kept, and OK alone -- in the shell's confirm host. `ok`
 * is the composition's: the host closed, the refusal dismissed.
 */
export function TaskRefusedAlert({ model, ok }: { readonly model: TasksViewModel; readonly ok: () => void }) {
  useObserved(model)
  const words = model.confirmWords('taskRefused')
  if (!words) return null
  return <ConfirmHost title={words.title} body={words.body} confirm={{ label: words.confirm, run: ok }} />
}
