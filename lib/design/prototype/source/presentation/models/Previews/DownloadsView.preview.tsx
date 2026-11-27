import { useObserved } from '../../styles/Observation/useObserved'
import type { ModelsViewModel } from '../ViewModels/ModelsViewModel'
import type { DownloadsViewModel } from '../ViewModels/DownloadsViewModel'
import { DownloadForm } from '../ViewModels/DownloadsViewModel'
import { QuantizeForm } from '../ViewModels/StoreForms'
import { DownloadSheet, DownloadsView } from '../Views/DownloadsView'
import { handles } from './ModelsWorlds'
import { useModelsPreview } from './ModelsPreview'

/** Jobs and the download sheet on their own (design 1h): one preview per state a person sees differently. */
const frame = { position: 'relative', width: 1100, height: 520, display: 'flex', background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const
const sheet = { width: 580, padding: 20, display: 'flex', flexDirection: 'column', gap: 14, background: 'var(--surface-card)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-small)' } as const

function Jobs({ model }: { readonly model: DownloadsViewModel }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <DownloadsView model={model} />
    </div>
  )
}

/** A pull a quarter into its snapshot's big file. */
export const Pulling = () => <Jobs model={useModelsPreview({ world: 'fresh', ticks: 10, then: (models) => void models.downloads.pull(new DownloadForm('meta-llama/Llama-3.2-1B-Instruct')) }).downloads} />

/** The pull done, its log open. */
export const PullDone = () => (
  <Jobs
    model={
      useModelsPreview({
        world: 'fresh',
        ticks: 60,
        then: (models) => {
          const download = models.downloads.pull(new DownloadForm('meta-llama/Llama-3.2-1B-Instruct'))
          if (download) models.downloads.toggleLog(download.id)
        },
      }).downloads
    }
  />
)

/** `models:notran`: a quantization stopped while its first store read ran: Run again, Dismiss. */
export const NotRan = () => (
  <Jobs
    model={
      useModelsPreview({
        then: async (models) => {
          const download = models.downloads.quantize(new QuantizeForm(handles.llamaF16, 'Llama3.2-1B-Instruct', 2479596089))
          if (download) await models.downloads.stop(download)
        },
      }).downloads
    }
  />
)

/** A pull nobody recorded: it never started, said in the stand-in's words. */
export const Failed = () => <Jobs model={useModelsPreview({ world: 'fresh', then: (models) => void models.downloads.pull(new DownloadForm('Qwen/Qwen3-8B')) }).downloads} />

function Sheet({ model }: { readonly model: ModelsViewModel }) {
  useObserved(model)
  return (
    <div className="ap" data-theme="dark" style={sheet}>
      <DownloadSheet model={model} />
    </div>
  )
}

/** `download`: the sheet filled with the recorded pull, GGUF and Q4_K_M. */
export const DownloadGguf = () => <Sheet model={useModelsPreview({ world: 'fresh', then: (models) => models.openDownload('meta-llama/Llama-3.2-1B-Instruct') })} />

/** The sheet on MLX: the snapshot alone, its note. */
export const DownloadMlx = () => <Sheet model={useModelsPreview({ world: 'fresh', then: (models) => (models.openDownload(), models.editDownload((form) => form.setFormat('mlx'))) })} />

/** A reference with no owner: Start waits, the sheet says the form. */
export const DownloadNoOwner = () => <Sheet model={useModelsPreview({ world: 'fresh', then: (models) => models.openDownload('llama') })} />
