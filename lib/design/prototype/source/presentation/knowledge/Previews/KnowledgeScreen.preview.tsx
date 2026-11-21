import { useState } from 'react'
import type { KnowledgeTab } from '../ViewModels/KnowledgeFaces'
import { KnowledgeScreen } from '../Views/KnowledgeScreen'
import { collectionQuestion, recordedRawRules, recordsQuestion, rejectedRecord } from './KnowledgeWorlds'
import { useKnowledgePreview } from './KnowledgePreview'
import type { KnowledgePreviewOptions } from './KnowledgePreview'

function Screen({ options, tab }: { readonly options: KnowledgePreviewOptions; readonly tab: KnowledgeTab }) {
  const place = useKnowledgePreview(options)
  const [shown, setShown] = useState<KnowledgeTab>(tab)
  return (
    <div className="ap" data-theme="dark" style={{ position: 'relative', display: 'flex', flexDirection: 'column', height: 900, background: 'var(--surface-window)', color: 'var(--text-primary)' }}>
      <KnowledgeScreen records={place.records} collections={place.collections} tab={shown} pickTab={setShown} />
    </div>
  )
}

const listed: KnowledgePreviewOptions = {}
/** The records listed: the three knowledge-list.json holds, none open. */
export const RecordsListed = () => <Screen options={listed} tab="records" />

const answered: KnowledgePreviewOptions = {
  then: async ({ records }) => {
    records.setQuestion(recordsQuestion)
    await records.ask()
  },
}
/** A question answered: knowledge-query.json's order and scores, 3 found · lexical. */
export const QuestionAnswered = () => <Screen options={answered} tab="records" />

const open: KnowledgePreviewOptions = {
  then: async ({ records }) => {
    records.setQuestion(recordsQuestion)
    await records.ask()
    records.select(rejectedRecord)
  },
}
/** `knowledge`: the question answered and the rejected record open, its raw conversation offered. */
export const RecordOpen = () => <Screen options={open} tab="records" />

const raw: KnowledgePreviewOptions = {
  then: async ({ records }) => {
    records.select(rejectedRecord)
    await records.showRaw()
  },
}
/** Its raw conversation shown: knowledge info --raw, after the CLI's marker. */
export const RawShown = () => <Screen options={raw} tab="records" />

const recordedRaw: KnowledgePreviewOptions = {
  rules: recordedRawRules(),
  then: async ({ records }) => {
    records.select('kr-20261026T142411Z-2e8272')
    await records.showRaw()
  },
}
/** The one raw conversation the CLI recorded, of its own record: knowledge-list-one.json and knowledge-info-raw.txt. */
export const RecordedRawShown = () => <Screen options={recordedRaw} tab="records" />

const rawFailed: KnowledgePreviewOptions = {
  then: async ({ records }) => {
    records.select('kr-20261021T045954Z-0084c5')
    await records.showRaw()
  },
}
/** A record whose conversation nobody recorded: the stand-in's line, in place. */
export const RawNotRecorded = () => <Screen options={rawFailed} tab="records" />

const rejected: KnowledgePreviewOptions = { then: ({ records }) => records.setBranch('rejected') }
/** The branch filter on Rejected: the rejected record alone. */
export const BranchRejected = () => <Screen options={rejected} tab="records" />

const none: KnowledgePreviewOptions = { then: ({ records }) => records.setBranch('superseded') }
/** A branch with none: Superseded, said. */
export const BranchWithNone = () => <Screen options={none} tab="records" />

const missing: KnowledgePreviewOptions = { apogee: 'missing' }
/** No apogee: the list empty and no words, as the old Records tab has no missing state. */
export const RecordsNoApogee = () => <Screen options={missing} tab="records" />

const typo: KnowledgePreviewOptions = { apogee: 'config-typo' }
/** A config typo: the CLI's refusal where the list stands. */
export const RecordsConfigTypo = () => <Screen options={typo} tab="records" />

const collections: KnowledgePreviewOptions = {}
/** The collections read: the old three, notes' page shown as the old one shows it. */
export const CollectionsRead = () => <Screen options={collections} tab="collections" />

const searched: KnowledgePreviewOptions = {
  then: async ({ collections: place }) => {
    await place.select('notes')
    place.setQuestion(collectionQuestion)
    await place.search()
  },
}
/** `knowledge:collections`: notes searched, collection-query.txt's lines as printed. */
export const CollectionSearched = () => <Screen options={searched} tab="collections" />

const mixed: KnowledgePreviewOptions = { then: ({ collections: place }) => place.select('mixed') }
/** `knowledge:mixed`: a collection whose vectors come from two models, its Vectors line in embed info's words. */
export const MixedVectors = () => <Screen options={mixed} tab="collections" />

const collectionsMissing: KnowledgePreviewOptions = { apogee: 'missing' }
/** No apogee: no collections, no page and no words, as the old Collections tab has no missing state. */
export const CollectionsNoApogee = () => <Screen options={collectionsMissing} tab="collections" />
