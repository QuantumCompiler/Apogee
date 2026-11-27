import refusedErr from '@recorded/business/media/Fixtures/generate-refused-runtime.err?raw'
import { InstallConsentSheet } from '../Components/InstallConsentSheet'
import { installSheetWords } from '../Media/Install'

/** The sheet's content as the shell's host frames it, its title above. */
function Sheet({ title, said, body, command }: { readonly title: string; readonly said: string; readonly body: string; readonly command: string }) {
  return (
    <div className="ap card" data-theme="dark" style={{ width: 580, padding: '18px 20px', display: 'flex', flexDirection: 'column', gap: 12, fontSize: 'var(--t-small)' }}>
      <h2 style={{ margin: 0, fontSize: 'var(--t-h2)', fontWeight: 600 }}>{title}</h2>
      <InstallConsentSheet said={said} body={body} command={command} />
    </div>
  )
}

const video = installSheetWords('video')

/** Install what video needs: the torch-and-diffusers refusal, as the Mac's preview shows it (generate-refused-runtime.err). */
export const InstallVideo = () => <Sheet title={video.title} said={refusedErr.trim()} body={video.body} command="apogee train setup --with video" />

/** A rebuild the CLI's M19 names: its cost in its own words. */
export const Rebuild = () => {
  const words = installSheetWords('voice', "This replaces Apogee's own Python environment with one on a Python every runtime runs on, downloading every set it held again -- several gigabytes, once. If anything fails, the environment that was there is put back. Nothing is replaced unless you say yes.")
  return <Sheet title={words.title} said="apogee train: the Python environment at /Users/someone/.apogee/training/venv is on Python 3.14.5, and the 'voice' set runs on >=3.10,<3.13 -- rebuild it on a Python that fits: apogee train setup --rebuild --with voice" body={words.body} command="apogee train setup --rebuild --yes --with voice" />
}
