import ingestUnregistrableErr from '@recorded/business/jobs/Fixtures/embed-ingest-unregistrable.err?raw'
import deleteKept from '../../../../fixtures/business/reads/delete-kept-llama-q4-handmade.txt?raw'
import { SaidCard } from '../Components/SaidCard'

const frame = { width: 720, padding: 20, background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)' } as const

/** The card a delete leaves (design 1h): a kept backend and its two doors, in the CLI's words. */
export const KeptBackend = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <SaidCard title="Deleted Llama3.2-1B-Instruct/gguf/8d51e6c04a27" lines={deleteKept.trimEnd().split('\n')} dismiss={() => {}} />
  </div>
)

/** A run's notes (GUI M22): the recorded ingest into `my notes`, whose stderr is its one note, as the CLI printed it. */
export const RunNotes = () => (
  <div className="ap" data-theme="dark" style={frame}>
    <SaidCard title="What the ingest said" lines={ingestUnregistrableErr.trimEnd().split('\n')} dismiss={() => {}} />
  </div>
)
