// Documents nobody recorded, hand-made in the CLI's exact shape; each names
// the source of its shape. A recorded one replaces its hand-made one.

/**
 * `apogee train rollback tuned`'s stdout, hand-made in the shape train.cpp
 * prints it (lib/src/cli/source/presentation/cli/train.cpp, the rollback
 * callback: `rolled back <key>: v<from> -> v<to>`, the weights, the note).
 */
export const ROLLBACK_TUNED_HANDMADE =
  'rolled back tuned: v2 -> v1\n  gguf: /Users/someone/.apogee/models/tiny/gguf/cf368ccf9b45/tuned-v1.gguf\n  nothing was deleted; v2 stays on disk\n'

/**
 * `apogee train rollback nightly-model`'s refusal on stderr, hand-made in the
 * shape fail_user prints it (train.cpp: "no version history for backend ...").
 */
export const ROLLBACK_NO_HISTORY_HANDMADE =
  "error: no version history for backend 'nightly-model' -- 'apogee train promote <run> --as nightly-model' creates the first version\n"

/**
 * The served `complete` tool as `tools/list` lists it where the config lists
 * `complete` under each of `backends`' `mcp_serve:`, each held to its bound
 * (served_max_tokens: 1024, or the entry's own max_tokens when lower),
 * hand-made in the shape mcp_serve.cpp builds it (served_complete,
 * complete_description: the description naming each backend's bound, the
 * prompt/backend/max_tokens schema whose maximum is the widest bound,
 * readOnlyHint false).
 */
export function servedCompleteToolHandmade(backends: readonly { name: string; bound: number }[]) {
  const named = backends.map(backend => `${backend.name} (at most ${backend.bound} tokens)`).join(', ')

  return {
    annotations: { readOnlyHint: false },
    description:
      "Hand a prompt to one of Apogee's local models and answer with its reply. One call is the one-shot `apogee complete -m <backend> \"<prompt>\"` runs: the backend's own system prompt and sampling, no tools, no retrieval. backend may name: " +
      named +
      ". Each runs on this machine and is unmetered by its own provider's word, so a call spends this machine's time, never money. max_tokens asks for fewer tokens than the backend's bound, never more, and a reply that reaches its bound ends there. Offered because Apogee's config lists complete under these backends' mcp_serve.",
    inputSchema: {
      properties: {
        backend: { description: 'The backend that answers: one of those this tool names', enum: backends.map(backend => backend.name), type: 'string' },
        max_tokens: {
          description: "The most tokens the reply may run to, at most the backend's bound (the bound when omitted)",
          maximum: Math.max(0, ...backends.map(backend => backend.bound)),
          minimum: 1,
          type: 'integer',
        },
        prompt: { description: 'What the model is asked, as apogee complete takes its prompt', type: 'string' },
      },
      required: ['prompt', 'backend'],
      type: 'object',
    },
    name: 'complete',
  }
}

/** The served `complete` where the config lists it under q4 alone, whose entry sets no max_tokens. */
export const SERVED_COMPLETE_TOOL_HANDMADE = servedCompleteToolHandmade([{ name: 'q4', bound: 1024 }])

/** A one-shot completion's stdout on a pipe: the answer alone (complete.cpp: no decoration off a terminal). */
export const COMPLETE_ANSWER_HANDMADE = 'The capital of France is Paris.\n'

/**
 * `models list --output-format json` with an MLX backend, an Ollama one and a
 * cloud one, hand-made in the recorded documents' row shape
 * (models-list-adapters.json) with the types models.cpp groups as local.
 */
export const MODELS_LIST_MIXED_HANDMADE = JSON.stringify({
  object: 'list',
  data: [
    { backend: 'gemma-mlx', backend_type: 'mlx', model: 'mlx-community/gemma-3-4b-it-4bit', section: 'local', state: 'ok', type: 'model' },
    { backend: 'ollama', backend_type: 'ollama-cli', model: 'gpt-oss:120b-cloud', section: 'local', state: 'installed', type: 'model' },
    { backend: 'claude', backend_type: 'claude-cli', model: '', section: 'cloud', state: 'installed', type: 'model' },
    { backend: 'gemma-mlx', backend_type: 'mlx', model: 'second row of the same backend', section: 'local', state: 'ok', type: 'model' },
  ],
})

/**
 * `models status --output-format json` with the chat role on a backend and
 * the utility role as given, hand-made in the recorded shape
 * (models-status-chat-on-embedding.json: `fields` of `field`/`value`, the
 * label as printed; a utility models.utility names is printed as its key,
 * models.cpp's role loop, and one it does not as `(unset -- the chat's own backend)`).
 */
export function modelsStatusHandmade(chat: string, utility = "(unset -- the chat's own backend)"): string {
  return JSON.stringify({
    fields: [
      { field: 'chat', value: chat },
      { field: 'embedding', value: `${chat}   (via models.default)` },
      { field: 'utility', value: utility },
    ],
  })
}

/**
 * `train status --output-format json` with one backend's active version and
 * the runs running, hand-made in the recorded shape (train-status-cycle.json).
 */
export function trainStatusHandmade(backend: string, version: number, running: readonly string[] = []): string {
  return JSON.stringify({
    active_pipeline: null,
    cycle: null,
    cycle_active: false,
    latest_pipeline: null,
    pipelines: 0,
    recent_runs: [],
    running,
    runs: running.length,
    versions: [{ active_version: version, backend, kept: version, total: version }],
  })
}

/**
 * `train versions --output-format json` with two ledgers of more than one
 * version, hand-made in the recorded document's shape
 * (train-versions-adapters.json) with the fields http-api.md's
 * `GET /v1/admin/training/versions` lists: tuned's GGUF v1 (the recorded
 * one) and v2, v2 active; helper's MLX v1, pruned by retain_versions, and
 * v2, active, scored on its quantized base.
 */
export const TRAIN_VERSIONS_TWO_HANDMADE = JSON.stringify({
  data: [
    {
      active_version: 2,
      backend_name: 'tuned',
      versions: [
        {
          eval_passed: true,
          eval_score: 1.0,
          gguf_path: '/Users/someone/.apogee/models/tiny/gguf/cf368ccf9b45/tuned-v1.gguf',
          promoted_at: '2026-10-26T06:56:37Z',
          run_id: '20261026-065637',
          version: 1,
        },
        {
          eval_passed: true,
          eval_score: 0.75,
          gguf_path: '/Users/someone/.apogee/models/tiny/gguf/0d1e2f3a4b5c/tuned-v2.gguf',
          promoted_at: '2026-10-27T09:15:00Z',
          run_id: '20261027-091500',
          version: 2,
        },
      ],
    },
    {
      active_version: 2,
      backend_name: 'helper',
      versions: [
        { mlx_path: '/Users/someone/.apogee/models/helper/mlx/1a2b3c4d5e6f', promoted_at: '2026-10-19T10:30:00Z', pruned_at: '2026-10-20T09:00:00Z', run_id: '20261019-101500', version: 1 },
        {
          eval_passed: false,
          eval_score: 0.5,
          eval_scored_on: 'helper-Q4_K_M.gguf (Q4_K_M)',
          mlx_path: '/Users/someone/.apogee/models/helper/mlx/6f5e4d3c2b1a',
          promoted_at: '2026-10-20T11:30:00Z',
          run_id: '20261020-111500',
          version: 2,
        },
      ],
    },
  ],
  object: 'list',
})

/** `train versions --output-format json` with no ledger: http-api.md's `{"object": "list", "data": [...]}`, data `[]`. */
export const TRAIN_VERSIONS_EMPTY_HANDMADE = '{"data":[],"object":"list"}\n'

/** `models list --output-format json` with nothing configured and nothing stored: the recorded shape (models-list-adapters.json), data `[]`. */
export const MODELS_LIST_EMPTY_HANDMADE = '{"data":[],"folded":0,"object":"list"}\n'
