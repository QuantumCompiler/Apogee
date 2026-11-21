import { PhaseRow } from '../Components/PhaseRow'

const card = { width: 440, padding: '8px 16px' } as const

/** A run denoising: the pipeline loaded, step 38 of 50, the clip not yet assembled. */
export const Denoising = () => (
  <div className="ap card" data-theme="dark" style={card}>
    <PhaseRow phase={{ label: 'Load the pipeline', status: 'done', detail: 'Wan2.2-TI2V-5B · PyTorch' }} />
    <PhaseRow phase={{ label: 'Denoise', status: 'running', detail: 'step 38 of 50', bar: 76 }} />
    <PhaseRow phase={{ label: 'Assemble the clip', status: 'pending', detail: '121 frames · h264' }} />
  </div>
)

/** Stopped while it denoised: the phase it was on in the warning color. */
export const Stopped = () => (
  <div className="ap card" data-theme="dark" style={card}>
    <PhaseRow phase={{ label: 'Load the pipeline', status: 'done', detail: 'Wan2.2-TI2V-5B · PyTorch' }} />
    <PhaseRow phase={{ label: 'Denoise', status: 'stopped', detail: 'step 12 of 50', bar: 24 }} />
    <PhaseRow phase={{ label: 'Assemble the clip', status: 'pending', detail: '121 frames · h264' }} />
  </div>
)
