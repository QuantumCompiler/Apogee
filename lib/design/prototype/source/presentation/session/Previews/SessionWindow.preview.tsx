import chatUnderSuite from '@recorded/data/protocol/Fixtures/chat-under-suite-m29.jsonl?raw'
import codeAnswer from '@recorded/data/protocol/Fixtures/code-answer.jsonl?raw'
import markdownAnswer from '@recorded/data/protocol/Fixtures/markdown-answer.jsonl?raw'
import newUnsent from '@recorded/data/protocol/Fixtures/new-unsent.jsonl?raw'
import privateUnsent from '@recorded/data/protocol/Fixtures/private-unsent.jsonl?raw'
import setSuiteRefused from '@recorded/data/protocol/Fixtures/set-suite-refused.jsonl?raw'
import stoppedTurn from '@recorded/data/protocol/Fixtures/stopped-turn.jsonl?raw'
import executePlay from '../../../../fixtures/data/protocol/execute-play-summarize-verify.jsonl?raw'
import type { SessionViewModel } from '../ViewModels/SessionViewModel'
import { SessionWindow } from '../Views/SessionWindow'
import { scenario } from './ScenarioStreams'
import { useSessionPreview } from './SessionPreview'

/**
 * The session's window on its own (design 1u), one preview per state a
 * person sees differently, each over the stand-in replaying a recorded
 * stream: the four presets' frames, a turn at each stage, the menus, and
 * the binary missing or refusing.
 */
const frame = { position: 'relative', width: 1184, height: 760, display: 'flex', flexDirection: 'column', background: 'var(--surface-window)', color: 'var(--text-primary)', fontFamily: 'var(--font-ui)', fontSize: 'var(--t-body)', lineHeight: 1.5 } as const

function Window({ model }: { readonly model: SessionViewModel }) {
  return (
    <div className="ap" data-theme="dark" style={frame}>
      <SessionWindow model={model} />
    </div>
  )
}

const play = '/play summarize-verify The board met on 14 October. It agreed to move the release to 3 November, and Ana Ruiz will own the checklist.'
const playLaunch = { execute: { suite: 'coding' }, folder: '~/code/ingest' }

/** `chat:new`: a new chat, unsent -- the empty state. */
export const NewSession = () => <Window model={useSessionPreview(newUnsent, { ticks: 2 })} />

/** `chat:private`: private, unsent -- what is and is not kept, the CLI's notice under it. */
export const PrivateSession = () => <Window model={useSessionPreview(privateUnsent, { launch: { privately: true }, ticks: 3 })} />

/** Under `monitor`: a session not launched, its empty state. */
export const NotLaunched = () => <Window model={useSessionPreview(newUnsent, { unlaunched: true })} />

/** `chat:play`: the captured execute session, its play finished: the banner, the launch's notices, the stages, the answer. */
export const Played = () => <Window model={useSessionPreview(executePlay, { launch: playLaunch, sends: [play], ticks: 15 })} />

/** The play mid-stage: its card live, the second stage asking. */
export const Playing = () => <Window model={useSessionPreview(executePlay, { launch: playLaunch, sends: [play], ticks: 9 })} />

/** A turn sent, nothing back yet: the pending line, Stop in the composer. */
export const Pending = () => (
  <Window
    model={useSessionPreview(markdownAnswer, {
      ticks: 1,
      then: (model) => {
        model.setDraft('How do I keep the batch going?')
        model.submit()
      },
    })}
  />
)

/** Thinking: the turn's activity open, live, the pending line under it. */
export const Thinking = () => <Window model={useSessionPreview(markdownAnswer, { sends: ['How do I keep the batch going?'], ticks: 2 })} />

/** The answer streaming, its python fence still open. */
export const Answering = () => <Window model={useSessionPreview(markdownAnswer, { sends: ['How do I keep the batch going?'], ticks: 45 })} />

/** markdown-answer.jsonl answered: every block drawn, the model in its footer. */
export const Answered = () => <Window model={useSessionPreview(markdownAnswer, { sends: ['How do I keep the batch going?'], ticks: 200 })} />

/** code-answer.jsonl answered: each fence coloured by its language. */
export const CodeAnswered = () => <Window model={useSessionPreview(codeAnswer, { sends: ['Show me a retry in every language you know.'], ticks: 400 })} />

/** stopped-turn.jsonl: a turn answered, then one stopped mid-answer. */
export const Stopped = () => <Window model={useSessionPreview(stoppedTurn, { sends: ['Which port does the dev server use?', 'When is the launch?'], ticks: 14 })} />

/** A chat under a suite, the CLI's word on it in the header (M29), and its admission notice. */
export const UnderSuite = () => <Window model={useSessionPreview(chatUnderSuite, { sends: ['hello'], ticks: 12 })} />

/** A notice between turns: a `set` the session refused, in the CLI's words. */
export const NoticeBetweenTurns = () => <Window model={useSessionPreview(setSuiteRefused, { sends: ['hello'], ticks: 12 })} />

/** The model menu open: the chat backends by kind, each with the CLI's words. */
export const ModelMenu = () => <Window model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.toggleMenu('model') })} />

/** The thinking menu open: the modes, the effort row and when a change takes. */
export const ThinkingMenu = () => <Window model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.toggleMenu('think') })} />

/** The inspector closed: the main column takes the window's width. */
export const InspectorClosed = () => <Window model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.toggleInspector() })} />

/** `/` typed: the commands. */
export const SlashCommands = () => <Window model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.setDraft('/') })} />

/** `/play ` typed in an execute session: the symphonies it can play. */
export const PlayCommands = () => (
  <Window
    model={useSessionPreview(executePlay, {
      launch: playLaunch,
      ticks: 6,
      then: (model) => {
        model.plays = [
          { name: 'summarize-verify', description: 'Summarize a passage, then check the summary against the passage and correct it.' },
          { name: 'digest', description: 'Summarize and check a passage by playing summarize-verify, then card it with a title, tags and a one-line summary.' },
        ]
        model.setDraft('/play ')
      },
    })}
  />
)

/** `@` typed: the folder's files. */
export const MentionFiles = () => <Window model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.setDraft('Look at @src') })} />

/** The attach menu open. */
export const AttachMenu = () => <Window model={useSessionPreview(newUnsent, { ticks: 2, then: (model) => model.toggleMenu('attach') })} />

/** Attached: a chip for each path sent, each removable where the session takes detach. */
export const Attached = () => (
  <Window
    model={useSessionPreview(newUnsent, {
      ticks: 2,
      then: (model) => {
        model.attach('~/code/ingest/')
        model.attach('notes/wal-design.md')
      },
    })}
  />
)

/** No binary found: how to install it. */
export const Missing = () => <Window model={useSessionPreview(newUnsent, { apogee: 'missing' })} />

/** A config the CLI cannot read: the launch refused, in its words. */
export const ConfigTypo = () => <Window model={useSessionPreview(newUnsent, { apogee: 'config-typo', ticks: 2 })} />

// MARK: - 1v: the scenarios and states, each at its preset's frame

/** `chat` (and `monitor` behind its overlay): Streaming, three-quarters through the reasoning. */
export const Streaming = () => <Window model={useSessionPreview(...scenario.streaming('chat'))} />

/** `chat:compacting`'s open session: Streaming at its starting frame. */
export const StreamingEarlier = () => <Window model={useSessionPreview(...scenario.streaming('start'))} />

/** `chat:retry`: Permission, its card open. */
export const Permission = () => <Window model={useSessionPreview(...scenario.permission())} />

/** `chat:wal`: Sources, citation 1 open. */
export const Sources = () => <Window model={useSessionPreview(...scenario.sources())} />

/** `chat:crash`: Error, vast didn't load. */
export const ErrorCard = () => <Window model={useSessionPreview(...scenario.error())} />

/** `chat:pr`: Question, Claude asking. */
export const Question = () => <Window model={useSessionPreview(...scenario.question())} />

/** `chat:restarting`: moved to the suite pair, the child being stopped, an attach held, the inspector closed. */
export const Restarting = () => <Window model={useSessionPreview(...scenario.restarting())} />

/** `chat:notsent`: a turn its closed child never took, the session back. */
export const NotSent = () => <Window model={useSessionPreview(...scenario.notSent())} />

/** `chat:ended`: the child dead mid-answer, Session ended, two lines held for Start again. */
export const Ended = () => <Window model={useSessionPreview(...scenario.ended())} />
