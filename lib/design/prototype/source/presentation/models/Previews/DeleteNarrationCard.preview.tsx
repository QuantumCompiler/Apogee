import deleteKept from '../../../../fixtures/business/reads/delete-kept-llama-q4-handmade.txt?raw'
import { DeleteNarrationCard } from '../Views/DeleteNarrationCard'

/** The card a delete leaves (design 1h): a kept backend and its two doors, in the CLI's words. */
export const KeptBackend = () => (
  <div className="ap" data-theme="dark" style={{ width: 720, padding: 20, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)' }}>
    <DeleteNarrationCard title="Deleted Llama3.2-1B-Instruct/gguf/8d51e6c04a27" text={deleteKept.trimEnd()} dismiss={() => {}} />
  </div>
)
