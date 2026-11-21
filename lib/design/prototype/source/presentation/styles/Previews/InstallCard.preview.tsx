import installDone from '@recorded/business/media/Fixtures/install-video-done.txt?raw'
import installFailedErr from '@recorded/business/media/Fixtures/install-video-failed.err?raw'
import { InstallCard } from '../Components/InstallCard'
import type { InstallCardFace } from '../Components/InstallCard'

const command = 'apogee train setup --with video'
/** install-video-done.txt's line starting `prefix`: what the card says, from the recording. */
const line = (prefix: string) => installDone.split('\n').find((text) => text.startsWith(prefix)) ?? ''

function Card({ install }: { readonly install: InstallCardFace }) {
  return (
    <div className="ap" data-theme="dark" style={{ width: 264, padding: 10 }}>
      <InstallCard install={install} act={() => undefined} />
    </div>
  )
}

/** Installing the video set: the line pip is on, the bar, Stop (the Mac's "Installing the video set"). */
export const Installing = () => <Card install={{ state: 'running', headline: 'Installing the video set', detail: line('installing'), command }} />

/** It failed, in the CLI's words: stderr's apogee line and pip's under it (install-video-failed.err). */
export const Failed = () => <Card install={{ state: 'failed', headline: 'The video set did not install', detail: installFailedErr.trim(), command }} />

/** Installed: its last line. */
export const Installed = () => <Card install={{ state: 'installed', headline: 'Installed the video set', detail: line('python env'), command }} />

/** Stopped before it finished: the line it was on. */
export const Stopped = () => <Card install={{ state: 'stopped', headline: "The video set's install was stopped before it finished", detail: line('installing'), command }} />
