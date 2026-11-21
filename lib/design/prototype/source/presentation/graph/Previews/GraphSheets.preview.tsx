import { SheetHost } from '../../shell/Views/Hosts'
import { useObserved } from '../../styles/Observation/useObserved'
import type { GraphViewModel } from '../ViewModels/GraphViewModel'
import { DedupeSheet, SavePanel } from '../Views/GraphSheets'
import { useGraphPreview } from './GraphPreview'
import type { GraphPreviewOptions } from './GraphPreview'

/** The save panel and the dedupe sheet in the shell's sheet host, framed as the app root frames them. */
function Frames({ model }: { readonly model: GraphViewModel }) {
  useObserved(model)
  const panel = model.savePanel()
  const dedupe = model.dedupeSheet()
  return (
    <>
      {panel.shown ? (
        <SheetHost
          title="Save As"
          width={460}
          right={[
            { label: 'Cancel', run: () => model.cancelSave() },
            { label: 'Save', kind: 'btn-p', run: () => void model.save(), disabled: !panel.canSave },
          ]}
        >
          <SavePanel model={model} />
        </SheetHost>
      ) : null}
      {dedupe.shown ? (
        <SheetHost
          title={dedupe.title}
          width={560}
          right={
            dedupe.merges
              ? [
                  { label: 'Cancel', run: () => model.dismissDedupe() },
                  { label: 'Merge them', kind: 'btn-p', run: () => model.confirmDedupe(), destructive: true },
                ]
              : [{ label: 'Done', kind: 'btn-p', run: () => model.dismissDedupe(), disabled: !dedupe.done }]
          }
        >
          <DedupeSheet model={model} />
        </SheetHost>
      ) : null}
    </>
  )
}

function Sheet({ options }: { readonly options: GraphPreviewOptions }) {
  const model = useGraphPreview(options)
  return (
    <div className="ap" data-theme="dark" style={{ position: 'relative', height: 900, background: 'var(--surface-window)', color: 'var(--text-primary)' }}>
      <Frames model={model} />
    </div>
  )
}

const save: GraphPreviewOptions = { then: (graph) => graph.exportAs('html') }
/** The save panel: Export as HTML page…, the CLI's own name offered in Documents, the command Save runs. */
export const SavePanelOffered = () => <Sheet options={save} />

const empty: GraphPreviewOptions = {
  then: (graph) => {
    graph.saveReport()
    graph.setSaveFile('')
  },
}
/** The save panel with no name: Save off. */
export const SavePanelEmpty = () => <Sheet options={empty} />

const nothing: GraphPreviewOptions = { then: (graph) => graph.planDedupe() }
/** Dedupe on demo: the dry run names nothing to merge, Done alone. */
export const DedupeNothingToMerge = () => <Sheet options={nothing} />

const offering: GraphPreviewOptions = {
  then: async (graph) => {
    await graph.show('prose')
    await graph.planDedupe()
  },
}
/** Dedupe on notes: the dry run names a group, Merge them offered. */
export const DedupeOffering = () => <Sheet options={offering} />

const merging: GraphPreviewOptions = { ...offering, script: [[(graph) => graph.confirmDedupe(), 1]] }
/** Merging: the merge supervised, Done off. */
export const DedupeMerging = () => <Sheet options={merging} />

const merged: GraphPreviewOptions = { ...offering, script: [[(graph) => graph.confirmDedupe(), 4]] }
/** Merged: what the merge printed, the CLI's note to find communities again. */
export const DedupeMerged = () => <Sheet options={merged} />
