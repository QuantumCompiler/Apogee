import suitesModelsStatus from '@recorded/business/reads/Fixtures/suites-models-status.json?raw'
import chatsDelete from '../../../../fixtures/business/reads/chats-delete-handmade.txt?raw'
import infoCmake from '../../../../fixtures/business/reads/chats-info-cmake-handmade.json?raw'
import infoCrash from '../../../../fixtures/business/reads/chats-info-crash-handmade.json?raw'
import infoEmbedModels from '../../../../fixtures/business/reads/chats-info-embedmodels-handmade.json?raw'
import infoKv from '../../../../fixtures/business/reads/chats-info-kv-handmade.json?raw'
import infoPg from '../../../../fixtures/business/reads/chats-info-pg-handmade.json?raw'
import infoPlay from '../../../../fixtures/business/reads/chats-info-play-handmade.json?raw'
import infoPr from '../../../../fixtures/business/reads/chats-info-pr-handmade.json?raw'
import infoQuant from '../../../../fixtures/business/reads/chats-info-quant-handmade.json?raw'
import infoRetry from '../../../../fixtures/business/reads/chats-info-retry-handmade.json?raw'
import infoScraper from '../../../../fixtures/business/reads/chats-info-scraper-handmade.json?raw'
import infoStandup from '../../../../fixtures/business/reads/chats-info-standup-handmade.json?raw'
import infoTaskgroup from '../../../../fixtures/business/reads/chats-info-taskgroup-handmade.json?raw'
import infoWal from '../../../../fixtures/business/reads/chats-info-wal-handmade.json?raw'
import chatsList from '../../../../fixtures/business/reads/chats-list-handmade.json?raw'
import chatsRecallForget from '../../../../fixtures/business/reads/chats-recall-forget-handmade.txt?raw'
import chatsRecall from '../../../../fixtures/business/reads/chats-recall-handmade.json?raw'
import chatsTitle from '../../../../fixtures/business/reads/chats-title-handmade.txt?raw'
import modelsStatusCoding from '../../../../fixtures/business/reads/models-status-coding-handmade.json?raw'
import modelsStatusOff from '../../../../fixtures/business/reads/models-status-off-handmade.json?raw'
import type { Rule, Rules } from '../../../infrastructure/process/StandInChild'

/**
 * The thirteen saved chats every screen's sidebar lists (design 1g): the old
 * `chatDefs()`'s titles across Today, Yesterday, Previous 7 days and Earlier
 * against the stand-in's fixed now, on the CLI's ids -- the seven the presets
 * open carry their captures' chat ids, the recalled one its memory event's --
 * hand-made in `chats list`'s exact shape (`chats-list-handmade.json`).
 */
const infos: readonly string[] = [infoTaskgroup, infoRetry, infoWal, infoCrash, infoPlay, infoPr, infoKv, infoStandup, infoEmbedModels, infoPg, infoCmake, infoQuant, infoScraper]

/** A `chats info` answer's chat: the id its document names. */
function idOf(info: string): string {
  return (JSON.parse(info) as { id: string }).id
}

/**
 * The Sessions place's reads and changes, as the stand-in answers them on
 * every preset (design 1g): `chats list`, each listed chat's `chats info`,
 * `chats recall`, the roles (`models status`, plain for a resumed chat the
 * CLI has not spoken of, under `coding` for an execute session, and under
 * `off` for one the CLI says runs under no suite), and
 * `chats title`, `chats delete` and `chats recall --forget --yes`. Each
 * answer is hand-made in the CLI's shape, or a recording.
 */
export function libraryRules(): Required<Pick<Rules, 'reads' | 'commands'>> {
  return {
    reads: [
      { arguments: ['chats', 'list'], answer: { stdout: chatsList } },
      { arguments: ['chats', 'recall'], answer: { stdout: chatsRecall } },
      { arguments: ['models', 'status'], answer: { stdout: suitesModelsStatus } },
      { arguments: ['models', 'status', '--suite', 'coding'], answer: { stdout: modelsStatusCoding } },
      { arguments: ['models', 'status', '--suite', 'off'], answer: { stdout: modelsStatusOff } },
      ...infos.map((info): Rule => ({ arguments: ['chats', 'info', idOf(info)], answer: { stdout: info } })),
    ],
    commands: [
      { arguments: ['chats', 'title'], answer: { stdout: chatsTitle } },
      { arguments: ['chats', 'delete'], answer: { stdout: chatsDelete } },
      { arguments: ['chats', 'recall', '--forget', '--yes'], answer: { stdout: chatsRecallForget } },
    ],
  }
}

/** `rules` with the library's beside them, the preset's own first: a preset's rule of the same arguments is answered by the library's, the later. */
export function withLibraryRules(rules: Rules): Rules {
  const library = libraryRules()
  return { ...rules, reads: [...(rules.reads ?? []), ...library.reads], commands: [...(rules.commands ?? []), ...library.commands] }
}
