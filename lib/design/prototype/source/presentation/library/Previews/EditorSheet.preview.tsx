import { useObserved } from '../../styles/Observation/useObserved'
import { SheetHost } from '../../shell/Views/Hosts'
import type { LibraryViewModel } from '../ViewModels/LibraryViewModel'
import { EditorSheet } from '../Views/EditorSheet'
import { useLibraryPreview } from './LibraryPreview'
import type { LibraryPreviewOptions } from './LibraryPreview'
import { editorFiles, refusedSpec, typedLine } from './LibraryWorlds'

/** The editor in the shell's sheet host, framed as the app root frames it: its title, the file's path, and its buttons. */
function Frame({ model }: { readonly model: LibraryViewModel }) {
  useObserved(model)
  const editor = model.editorFace()
  if (!editor.shown) return null
  return (
    <SheetHost
      title={editor.title}
      subtitle={editor.path}
      subtitleMono
      width={720}
      left={editor.restore.shown ? [{ label: 'Put it back as it was', run: () => void model.restore(), disabled: !editor.restore.enabled }] : []}
      right={[
        { label: 'Close', run: () => model.requestClose() },
        { label: 'Save', kind: 'btn-p', run: () => void model.save(), disabled: !editor.canSave },
      ]}
    >
      <EditorSheet model={model} />
    </SheetHost>
  )
}

function Editor({ options }: { readonly options: LibraryPreviewOptions }) {
  const model = useLibraryPreview(options)
  return (
    <div className="ap" data-theme="dark" style={{ position: 'relative', height: 900, background: 'var(--surface-window)', color: 'var(--text-primary)' }}>
      <Frame model={model} />
    </div>
  )
}

const opened: LibraryPreviewOptions = { then: (library) => library.agents.edit('prompts/triage.txt') }
/** The triage prompt the binary scaffolded, opened: Save off until something changes. */
export const PromptOpened = () => <Editor options={opened} />

const typed: LibraryPreviewOptions = {
  then: (library) => {
    library.agents.edit('prompts/triage.txt')
    library.setEditorText(editorFiles.triage + typedLine)
  },
}
/** `library:unsaved`'s sheet: a line typed, Save on; Close would ask first. */
export const PromptTyped = () => <Editor options={typed} />

const saving: LibraryPreviewOptions = {
  then: (library) => {
    library.agents.edit('prompts/triage.txt')
    library.setEditorText(editorFiles.triage + typedLine)
  },
  script: [[(library) => library.save(), 2]],
}
/** Mid-save: read-only, dimmed, its busy line saying it waits for apogee's word. */
export const PromptSaving = () => <Editor options={saving} />

const saved: LibraryPreviewOptions = { ...saving, script: [[(library) => library.save(), 4]] }
/** The save stood: the agents' word that the CLI checks an agent when it runs, and Put it back as it was offered. */
export const PromptSaved = () => <Editor options={saved} />

const refused: LibraryPreviewOptions = {
  preset: 'library:refused',
  then: (library) => {
    library.pick('symphonies')
    library.pickRow('notes-digest')
    library.symphonies.edit()
    library.setEditorText(refusedSpec(editorFiles.notesDigest))
  },
  script: [[(library) => library.save(), 4]],
}
/** `library:refused`'s sheet: notes-digest saved with an unknown key, the CLI's refusal under it, Put it back as it was offered. */
export const SpecRefused = () => <Editor options={refused} />

const specSaved: LibraryPreviewOptions = {
  then: (library) => {
    library.pick('symphonies')
    library.pickRow('notes-digest')
    library.symphonies.edit()
    library.setEditorText(editorFiles.notesDigest.replace('Summarize then check.', 'Summarize, then check.'))
  },
  script: [[(library) => library.save(), 4]],
}
/** A spec saved that apogee reads: "Saved, and apogee reads it." */
export const SpecSaved = () => <Editor options={specSaved} />
