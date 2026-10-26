# Config verbs finish their job: roles by kind, a default when none, one provider, allowed hosts

**What / why.** The macOS app's 4k and M4 (shipped 2026-10-26) reached four config verbs that stop short. Each was reproduced again on `apogee 0.2.0 (4b828e91)` over sandbox homes on 2026-10-26:
1. **`config set-default <embedding mlx entry>` is accepted.**
   - **What happens:** `config set-default Emb-4bit` answers `models.default = Emb-4bit` and exits 0, and `check` then says `ok default_backend Emb-4bit`. The chat role now points at a backend whose every chat is refused. A voice entry is refused (`cannot take a role: … kind voice`).
   - **Also slipping:** `set-default-utility Emb-4bit`, and the reverse, `set-default-embedding Chat-4bit`.
   - **The cause:** the role gate doesn't know the role. `point_role` (`presentation/cli/config_cmd.cpp:1467–1474`) calls `require_backend_exists` (`:83–108`), which calls `kind_refusal` (`operations/backend_fields.cpp:266–273`), which asks `MlxKind::registers()`. That is `backend() || embeds()` (`data/modelstore/mlx_info.cpp:522–524`), widened in 53h so the scan could register embedding sets. 55d reused it as the role gate, and its decision ("registers() is false for them") holds for drafter, voice and video, not embedding.
   - **The app** narrows its own Roles chat picker since M4, but the CLI still takes it.
2. **`config add-backend` never sets `models.default`.** On a fresh home, `config add-backend mock --type mock` leaves `models.default` empty, and `complete hi` then fails `no models.default is set`. `providers scan --register` says `models.default set to 'claude' (none was set)` (28b). The cause is `config_cmd.cpp:1017–1023`. Two other writers must move with it:
   - `config scan --register` (`:779–781`), pinned byte for byte to add-backend (`config_cmd_test.cpp:622–636`);
   - the admin twin, `admin_config.cpp:333–336`.
3. **No verb registers one scanned provider.** `providers --help` lists only `scan`, which registers every provider at once (`scan --register`). `providers register claude` answers `A subcommand is required`, exit 106. `register_provider` (`presentation/cli/providers_cmd.cpp:222–242`, its header calling it "scan --register for one provider") runs only from the TUI's `r` key (`tui_doctor.cpp:178–188`). The app's Settings composes `config add-backend <id> --type <type>` in its place, so it neither sets a default nor fetches the roster, as `r` does.
4. **`config get tools.allowed_hosts` says `no such config key`,** even after `config add-allowed-host docs.python.org` writes the host. Completion lacks the key too. `config_cmd.cpp`'s `fixed_reads()` (`:150`) has no row for it.

Finding 1 comes before finding 2: once `add-backend` can set a default, an embedding entry must never become it.

**Core constraint(s).**
- **A role is the kind's to take.** `default_embedding` takes what embeds; every other role takes what chats. The one gate (55d's) learns the role, and the CLI, the admin twin and the TUI's workbench share it.
- **A default set on someone's behalf is said,** as 28b says it, and set only when none was set and only to an entry that chats.
- **One registration path.** `providers register <id>` runs `register_provider`, the same code as the TUI's `r` and `scan --register` for one, with the same words (`render_registration`, `provider_offer.cpp:168–208`).
- **ADR 0007:** every new verb, value and key is completable in the same change.
- **Recorded behaviour changes are dated revisions** (ADR 0006's records clause). `add-backend` setting a default revises its silence.

**Seam + files.**
- **`operations/backend_fields.{h,cpp}`:** `role_refusal(name, backend, field)` beside `kind_refusal`. It is used by `require_backend_exists`/`point_role` (`config_cmd.cpp:83–108`, `:1467–1474`) and the admin twin (`admin_config.cpp:841–846`); `tui_workbench.cpp:43` gets it free.
- **`config_cmd.cpp:1017–1023`, `:779–781`** and **`admin_config.cpp:333–336`:** the default set when none is, said, for an entry that chats.
- **`presentation/cli/providers_cmd.cpp:252–356`:** `providers register <id>` bound to `register_provider`, with completion of the scanned ids and its JSON from `scan --register`'s lines (`:347–351`). `httpserver/parity_test.cpp:159` gains its carve-out row.
- **`config_cmd.cpp:150–172`:** a `tools.allowed_hosts` row in `fixed_reads()` beside `tools.disabled`, one per line as 55g's lists print.

**Reference.**
- 55d's kind gate, and `MlxEmbedProvider::chat_refusal` (`data/backends/mlx_embed.cpp:99–104`): the words a role refusal takes.
- 28b's "set only when none was set, said" (`provider_offer.cpp:136–142`, `:161–163`, `:197–200`).
- 55g's one-per-line list reads.

**Decisions made** (dated):
- 2026-10-26 — **Opened from the macOS app's 4k and M4 records** (the user's call: "spec them into the backlog"; placed in CLI Maintenance the same day).
- 2026-10-26 — **`graph delete` keeping its graph's config entry is not in this item.** It is recorded design in three places: the verb's help ("config entries and chunks stay", `graph.cpp:1776–1779`), the admin twin (`admin_graph.cpp:706–710`, `http-api.md:1001–1004`), and the 2026-09-19 `knowledge-graph-global` record. Its own words name the second door (`apogee config delete-graph <name>`).

**Open calls:**
- [default: `config add-backend`, `config scan --register` and the admin twin set `models.default` when none is set and the new entry chats, saying `models.default set to '<name>' (none was set)`. A dated revision of their silence.]
- [default: `providers register <id>` registers one scanned provider as the TUI's `r` does, roster fetch included on the user's word as M13 rules, and takes `--output-format json` for the app.]

**Guardrail(s).**
- **The bug becomes a fixture.** Each role refuses an entry of the wrong kind, and `default_embedding` accepts an embedding entry (`models_mlx_test.cpp:3020–3100`'s `[kind]` role loop, `config_cmd_test.cpp`, `admin_config_test.cpp`, `tui_workbench_test.cpp`).
- **`add-backend` and `scan --register` stay byte-equal** with the default set (`config_cmd_test.cpp:598–636`), `provider_offer_test.cpp:186`, and the admin twin's case.
- **`providers register` and the TUI's `r` write the same bytes** (`tui_doctor_test.cpp:235`); `cli.providers_lifecycle`, `cli.provider_surfacing` and `cli.shell_completion`; the HTTP parity row.
- **`config get tools.allowed_hosts`** after an add, with completion (`config_cmd_test.cpp:687`, `complete_sources_test.cpp:299/340`, `cli.config_lifecycle`).

**Acceptance criteria:**
- [ ] No role points at an entry of a kind it can't use, and the embedding role takes an embedding entry.
- [ ] Adding the first chat backend makes it the default, said.
- [ ] One scanned provider registers by `providers register <id>`, as the TUI's `r` does.
- [ ] `config get tools.allowed_hosts` lists the allowed hosts.
- [ ] `lib/scripts/cicd.sh --test` passes with zero warnings, and the changed files pass the MinGW scan.

**Scope note.** CLI Maintenance, claimable by name, no gate. The macOS app's Settings registers through `config add-backend` today, and its Roles narrow on their own (M4). Adopting `providers register` is a later GUI item, not gated.
