# CLI-59a · Paired devices: the harness server admits only the person's devices

| Field | Value |
|---|---|
| **Type** | ✨ Feature |
| **Project** | CLI |
| **Status** | 🟢 Ready |
| **Fix version** | v0.2.3 |
| **Component** | `serve` (the harness plane, TLS) and `apogee devices` |
| **Labels** | `remote-mode` `pairing` `tls` `secrets` |
| **Epic** | Track 59 · the harness server |
| **Blocked by** | — |
| **Blocks** | [59b](machine-mode-over-the-network.md), [59d](reads-and-commands-over-the-network.md), [59f](paired-apps-with-scopes.md), [62e](../v0.2.4/connections-and-activity.md), [62g](../v0.2.4/administering-local-and-marked-devices.md), [62h](../v0.2.4/local-admin-reads.md) |
| **Reporter** | The user's vision (2026-11-07), specced from the Apple-platforms spike and brainstorm |
| **Created** | 2026-11-07 |

---

## Summary

`serve` today has an inference plane that is unauthenticated by design, one admin token per install, and no encryption on the wire, so there is no way to know a request comes from one of the person's own devices. This item delivers pairing: a code or QR from the server's terminal traded once for a per-device token, devices listed and revoked by name, TLS with a pinned self-signed or supplied certificate on every non-loopback bind, secrets `0600` and never logged, and a new harness plane that admits paired devices only, while the inference plane stays unchanged for stock clients.

## Description

### User story

**As** a person running Apogee on a strong machine of my own and using it from my other desktops and phones,
**I want** that server to admit only my paired devices, each with its own token I can list and revoke by name, over a connection nobody else can read,
**so that** every remote item in tracks 59 and 60, and every server-facing GUI item, can rest on knowing a request comes from one of my own devices.

### Background

The user's vision, 2026-11-07: Apogee as a private, self-hosted harness that feels like Claude on every device the person owns. A strong machine (a large, finely tuned local model on, say, 512 GB of RAM) serves the person's own desktops and phones, the way the frontier harnesses (Claude Code, Codex, Gemini CLI, Ollama, vLLM) serve theirs, but with local models. The server admits **the person and their devices only**. There are no customer accounts, no billing and no metering (paid features are a someday, not now). Every remote item in tracks 59 and 60, and every server-facing GUI item, rests on one thing this item builds: knowing that a request comes from one of the person's own devices, over a connection nobody else can read.

The shape of the design. This item delivers **pairing**. The server prints a short code, or a QR code, on its own terminal. A device trades it, once, for a token of its own. Devices are listed and revoked by name. Every non-loopback bind speaks TLS: by default a self-signed certificate that the device pins at pairing, or a certificate the person supplies. (An operator who terminates TLS in a proxy of their own can keep today's plain HTTP with `--tls off`; the harness plane is then not mounted, so a device credential never crosses plain HTTP off the machine.) The secrets are `0600` and never logged. A new **harness plane** admits paired devices only: every route registered behind the device gate, this item's under `/v1/harness/`, and the siblings' where they name them (59b's `GET /v1/machine` upgrade). The OpenAI-compatible inference plane keeps its behavior for stock clients. This item ships the gate and one route behind it (the calling device's own view). What runs behind the gate is the siblings': sessions and their events ([59b](machine-mode-over-the-network.md)), the reads and commands ([59d](reads-and-commands-over-the-network.md)).

### Evidence

**W1 — no authenticated remote harness.** Re-measured 2026-11-07 against the branch binary (`apogee 0.2.0 (4f92316d)`), a scratch `APOGEE_HOME` and the `mock` backend:

1. **The inference plane is open by design.** `bind_refusal` passes any non-loopback host once `--allow-remote` is given, and its refusal says why: "a non-loopback bind exposes the inference plane -- unauthenticated by design, for OpenAI-client compatibility -- to the network" (`presentation/httpserver/serve.cpp:97-103`). The flag table puts it the same way: "Put it behind your own access control" ([http-api.md](../../../reference/cli/http-api.md):25). Probed on loopback with no header at all, `GET /v1/models` answered `200` and `POST /v1/chat/completions` answered `200` with `"mock response"`. A started server's own note says the same (`presentation/cli/serve_cmd.cpp:373-377`).
2. **The only credential is one token per install.** The admin plane's bearer is a single 64-hex secret beside the config (`presentation/httpserver/admin_auth.h:9-31`; [http-api.md](../../../reference/cli/http-api.md):248-263). It is checked before routing (`presentation/httpserver/mux.cpp:494-498`). Probed on `GET /v1/admin/backends`: no header gives `401` (`authentication_error`); the token as a query string gives `401` too; the token as `Authorization: Bearer` gives `200`; the file is `-rw-------` and 65 bytes. The same token on `GET /v1/harness/device` gives `404`: there is no device-facing plane at all. Whoever holds it holds the whole plane. There is no notion of *which* device is calling, no list of holders, and no way to shut out one phone without changing the token for all of them.
3. **Nothing on the wire is encrypted.** cpp-httplib is fetched with every TLS option forced off (`cmake/ApogeeDependencies.cmake:262-266`), by a recorded rule: "Plain HTTP only: a deployment terminates TLS in front of it, which is where certificates belong" (`:40-51`; the sentence itself is `:48-51`). The reference says the same: "It does not terminate TLS" ([http-api.md](../../../reference/cli/http-api.md):1518). Probed: `curl -k https://127.0.0.1:<port>/health` failed the handshake (`LibreSSL SSL_connect: SSL_ERROR_SYSCALL`). On a non-loopback bind today, the admin bearer and every prompt and answer cross the network in clear.

## Acceptance criteria

- [ ] **Given** a sandbox home with the mock backend, **when** `apogee devices pair --name phone --json` runs, **then** it prints a code, the addresses, the port and the pin; **and when** a scripted client posts the code's proof to `POST /v1/pair`, **then** it receives an `apd_` token exactly once, and the same proof a second time is refused.
- [ ] **Given** `phone` is paired, **when** `GET /v1/harness/device` is called with that token, **then** it answers `phone`'s own view; without it, or with a one-byte-off or query-string token, every `/v1/harness/` path is `401`; and after `apogee devices revoke phone`, the next request is `401`.
- [ ] **Given** `phone` is paired, **when** `apogee devices list` runs and `GET /v1/admin/devices` is read, **then** both show `phone` with its paired and last-seen times, and neither carries a token or a hash.
- [ ] **Given** `apogee serve --bind 0.0.0.0 --allow-remote`, **when** it starts, **then** it prints `listening on https://…` and the pin; `curl -k --pinnedpubkey` with that pin gets `200` from `/v1/models`, a wrong pin fails, and plain `http://` fails. **Given** `--tls off`, **then** it serves plain HTTP and says the harness plane is not mounted.
- [ ] **Given** `--tls-cert`/`--tls-key`, **when** `serve` starts, **then** it serves the supplied certificate; **given** a group-readable key, **then** it is refused with `chmod 600` named.
- [ ] **Given** the device registry and the TLS key exist, **when** `apogee check` runs, **then** it shows both as private (`0600`), and `--fix` tightens a loosened one.
- [ ] **Given** no registry and no TLS identity present, **when** `chat` and `complete` run, **then** they work. The full suite is green on all five targets.
- [ ] Optional live pass, said when skipped: the user runs `apogee-dev serve --bind 0.0.0.0 --allow-remote` on the dev channel, pairs from a second machine on the LAN with curl, and reaches `/v1/harness/device` over TLS.

## Constraints

- **Remote mode is an add-on (the anti-goal).** Nothing here may make standalone use need a server, a network, a pairing or an account. `chat`, `complete`, `execute`, `task`, `acp`, `mcp serve` and the Mac app over pipes never read the device registry or the TLS identity, and never need them. Neither file exists until the person runs `apogee devices pair` or serves on a non-loopback bind; until then `check` says "not generated yet" as an Ok row, as it does for the admin token today (`presentation/cli/check.cpp:1293-1306`). A fresh install with no keys and no models still passes `check` (SPEC → Success criteria).
- **Serving = server** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Interactive turns never open a listening socket). Only `serve` owns a port, and `httpserver/serve.cpp` stays the only object that may reference `listen`/`accept`. `cli.no_listen_symbols` walks every library the binary links, so the TLS library is scanned too. That library's own socket helpers call `listen` and `accept` (mbedTLS's `net_sockets`, OpenSSL's accept BIO), so they must be compiled out, never added to the allow-list. `devices pair` is an ordinary command: it writes a file and opens no socket.
- **Secrets are unleakable by construction** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md) → Secrets are unleakable by construction). A device token is returned **once**: in the body of the exchange response, to the caller that proved the code, over TLS or on loopback. The server stores only its SHA-256, so no later route, listing, event or log line *can* return it. `DeviceView` (name, kind, paired, last seen) structurally lacks the hash. The pairing code and the TLS private key get the same treatment. Every file is written `0600` through `harness::write_file_atomically(…, /*private_mode=*/true)`. Tokens are header-only (`Authorization: Bearer`): a query-string token is refused even when correct, and comparisons are constant-time (`constant_time_equal`, `admin_auth.h:36-37`). The request logger prints method, path and status only (`serve.cpp:172-176`). That stays true, and no device name, token or code ever goes into a path or a query.
- **Stock clients keep their behavior.** The inference plane's routes, bodies, errors and no-auth stay byte-identical for a stock OpenAI client. The one visible change is that a remote bind now speaks `https://`, which is a dated revision of the recorded "Plain HTTP only" (see the Decision log).
- **Older versions keep working** ([ADR 0006](../../../adrs/cli/backwards-compatibility.md)). The new routes, files and flags are additive. Revising a behavior named in a record (TLS on a remote bind) is done as a dated revision in [http-api.md](../../../reference/cli/http-api.md) and a release-notes line, with the old shape one named flag away (`--tls off`).
- **Parity** ([ADR 0002](../../../adrs/cli/mode-parity.md); [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md)). `devices list|revoke` answer from the CLI (`--json`), on the admin plane and in the shell's classification table (`cli/tui_parity.cpp`). `devices pair` is CLI-only by design: a pairing code is never minted over HTTP (see Open questions), and the table carves that out with its reason.
- **The layers** ([ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0008](../../../adrs/cli/module-map.md)). The registry and the identity sit in Data. The gate and the routes sit in `httpserver` (Presentation). The verb sits in `cli`. A new module is declared in `cmake/modules.cmake`, and its links point down.
- **UTF-8 where it enters** (CLI CLAUDE.md). A device name offered over the wire is mended and bounded before it is stored or printed.
- **The dev channel** ([CLAUDE.md](../../../assistant/cli/CLAUDE.md#development-runs-on-the-dev-channel)). Any live pass runs `apogee-dev serve` over `~/.apogee-dev` and never touches the release `apogee` or `~/.apogee`.

## Technical approach

| File | Change |
|---|---|
| `cmake/ApogeeDependencies.cmake` | The TLS library, fetched and never found, at an exact tag, with its socket module off. Its note says why the recorded "Plain HTTP only" rule changed. httplib gets that library's backend switch; every other optional integration stays off. |
| `cmake/modules.cmake` | A Data module `tls` (`source/data/tls/identity.h/.cpp`). It loads a supplied certificate and key, or generates the self-signed identity, and computes the pin. `httpserver` and `cli` link it, and nothing else does. |
| `source/data/secrets/devices.h/.cpp` | The device registry over `<config dir>/devices.json` (`0600`). It holds paired devices (name, kind, paired at, last seen, token hash) and pending pairings (code hash, expiry, attempts left). Every write holds an advisory lock beside the file (`devices.json.lock`, the way `lock_config_file` guards the config, `data/contracts/config_edit.h:493-501`) and lands through `write_file_atomically(…, true)` (`:478-479`), since `devices pair` and a running server write the same file. It is read fresh on change (by mtime), so a revocation made in another process applies at the next request. |
| `source/presentation/httpserver/device_auth.h/.cpp` | The harness-plane gate, run before routing exactly as the admin gate is (`mux.cpp:494-498`), so an unauthenticated probe gets `401` on every `/v1/harness/` path. It is a registration helper, not only a prefix check, so a sibling's route outside the prefix (59b's `GET /v1/machine`) sits behind the same gate. It also owns the exchange, `POST /v1/pair`: unauthenticated, code in the body, single use, attempt-limited. |
| `source/presentation/httpserver/admin_devices.h/.cpp` | `GET /v1/admin/devices` (views only) and `DELETE /v1/admin/devices/{name}`. |
| `source/presentation/httpserver/mux.cpp` | The rows for `/v1/pair`, `GET /v1/harness/device` (the caller's own view, the first route behind the gate) and the admin rows. The harness plane's routes are mounted only where TLS or loopback carries them. |
| `source/presentation/httpserver/serve.h/.cpp` | `ServeOptions` gains the TLS identity. The listener is httplib's TLS server when the identity is present, and `on_listening` reports the scheme. |
| `source/presentation/cli/serve_cmd.cpp` | `--tls auto\|on\|off`, `--tls-cert PATH`, `--tls-key PATH`. The start notes print `listening on https://…`, the pin, the pairing line (`apogee devices pair --port N`) and, under `--tls off` on a remote bind, that the harness plane is not mounted and why. |
| `source/presentation/cli/devices_cmd.h/.cpp` | `apogee devices pair [--name NAME] [--address HOST]… [--port N] [--no-qr] [--json]`, `devices list [--json]` and `devices revoke NAME`. Each has help and completion ([ADR 0007](../../../adrs/cli/tab-completion.md)). |
| `source/presentation/cli/check.cpp` | `check_secrets` gains rows for the device registry and the TLS key (`0600`, or `skipped` on Windows), and `--fix` tightens both. |
| `source/presentation/cli/tui_parity.cpp` | The `devices` places classified. |

The tests named with these files are under Testing.

**Reference.**
- In-house precedents:
  - The admin plane's token and gate (CLI MILESTONES → Milestone U): per-install, `0600`, header-only, constant-time, a gate before routing.
  - The credential store's views with no key (`secrets/store.h`).
  - 57e's private writes (`write_file_atomically`).
  - The bind policy's fail-closed rule (`serve.h:36-38`).
  - The pinned httplib v0.56.0, which builds a TLS server against OpenSSL, mbedTLS or wolfSSL. Its mbedTLS backend sets its own send and receive callbacks (`mbedtls_ssl_set_bio` in its `httplib.h`), so mbedTLS's socket module is never needed. Checked in the fetched copy (`_deps/httplib-src/httplib.h`): it includes `<mbedtls/net_sockets.h>` (`:479`) but calls no `mbedtls_net_*` function, wiring its own callbacks at `:20388-20389`. The header alone brings no symbol, and `cli.no_listen_symbols` is the proof.
  - curl's `--pinnedpubkey sha256//<base64>` form, which is what the pin is written in.
- Siblings:
  - [59b](machine-mode-over-the-network.md), the machine mode on the harness plane, gated on this item.
  - [59c](many-sessions-at-once.md), many sessions at once.
  - [59d](reads-and-commands-over-the-network.md), the reads and commands, gated on this item.
  - [59e](tasks-outlive-the-connection.md), tasks that outlive the connection.
  - [60b](../v0.2.4/device-tools.md), a connected desktop's own tools, which arrive over a paired connection.
  - GUI [7a](../../darwin/v0.2.4/app-connection-local-or-server.md), the app's side of pairing: scan or type, the pin kept with the token in the Keychain.
  - GUI [8c](../../darwin/v0.2.5/iphone-and-ipad-distribution.md), getting the app onto iPhone and iPad.

**Docs when it ships:** [http-api.md](../../../reference/cli/http-api.md) gets TLS and the pin, pairing, the harness plane's gate, the admin device routes, and a dated revision of "What this server does not do". The commands reference gets `devices`. CLI CLAUDE.md's secrets sub-rules get "a device token is stored hashed, returned once", and its socket sub-rules get "the TLS library's socket module is compiled out".

## Testing

Hermetic: a temp `APOGEE_HOME`, the mock backend, handlers driven through `Mux::dispatch` with no port wherever a port is not the point.

- **`tests/data/secrets/devices_test.cpp`** — round trip, `0600` after every write, hash only, a revoke seen by a second reader, an expired or used code refused, the attempt limit, a corrupt file read as empty with a warning and never overwritten. This is the registry test that pins hash-only storage and `0600`.
- **`tests/data/tls/identity_test.cpp`** — generation, reuse, a supplied pair loaded, a key readable beyond its owner refused, the pin stable across loads.
- **`tests/presentation/httpserver/device_auth_test.cpp`** — the gate matrix: `401` on every harness path, before routing; a one-byte-off token, the query-string refusal, a revoked token, and the admin token *not* admitted as a device.
- **`tests/presentation/httpserver/pair_route_test.cpp`** — the exchange, a second use refused, a wrong code burning an attempt, a name de-duplicated and mended.
- **`tests/presentation/httpserver/admin_devices_test.cpp`** — the views carry no hash; revoke by name.
- **`tests/presentation/cli/devices_cmd_test.cpp`** — the `pair` output and `--json`, `list`, `revoke`, and an unknown name.
- **`leak_test.cpp`** (existing, extended) — gains a distinctive device token, a pairing code and a TLS private key, then searches every listing, `check` row, admin response, log line (with `-v`) and bus event for each of them.
- **`check_test.cpp`** (existing) — extended.
- **`cli.serve_tls`** (a shell case, `tests/scripts/sh/serve_tls.sh`) — runs serve on loopback with `--tls on`. `curl -k --pinnedpubkey sha256//…` (so the pin alone is the trust) reaches `/v1/models` and, with a paired token, `/v1/harness/device`. A wrong pin fails, and plain `http://` fails.
- **`cli.http_api_conformance`** — covers the new routes; a conformance case shows the inference plane's responses are byte-identical with and without devices paired, and over `--tls on`.
- **`cli.no_listen_symbols`** — covers the new library; fails if the TLS library brings `listen`/`accept` along.
- **`cli.install_parity`** — stays green: neither file is seeded.
- **The anti-goal** — a test drives `chat` and `complete` with the registry and the TLS directory absent, and then with both unreadable. Both still answer.

## Open questions

| Question | Status | Answer |
|---|---|---|
| Which TLS library? | `[default]` | **mbedTLS 3.6 (LTS)**, fetched at an exact tag and never found, and built with `MBEDTLS_NET_C` off. httplib is built with `CPPHTTPLIB_MBEDTLS_SUPPORT`. It is the same library on all five targets, keeping the rule that `serve` "must not gain a TLS stack on one machine and not another" (`ApogeeDependencies.cmake:44-46`). OpenSSL and wolfSSL are the alternatives httplib takes. OpenSSL is not on macOS as a library to build against, and wolfSSL's licence is GPL-or-commercial. |
| What does `--tls` default to, and what do its values do? | `[default]` | `--tls auto` is the default. It means TLS on a non-loopback bind and plain HTTP on loopback. `--tls on` forces TLS on loopback (the tests' path). `--tls off` on a remote bind serves exactly what it serves today, for an operator who terminates TLS in a proxy in front. In that case the harness plane is **not mounted** and the start note says so, so a device token never crosses plain HTTP off the machine. |
| What is the self-signed identity, and where does it live? | `[default]` | ECDSA P-256 with a 10-year validity and the host name as its subject. It lives in `<config dir>/tls/identity-key.pem` (`0600`) and `identity-cert.pem`, created at the first TLS serve. Deleting it makes a new one, and every device then pairs again (the start note says so when it regenerates). There is no rotation verb in this item. |
| What is the pin? | `[default]` | The SHA-256 of the certificate's public key (SPKI), written `sha256/<base64>`, the form curl's `--pinnedpubkey` checks. A renewal on the same key keeps every pairing. |
| How is a supplied certificate given and checked? | `[default]` | From `--tls-cert`/`--tls-key` or the config's `serve.tls_cert`/`serve.tls_key`. `serve` refuses a key readable beyond its owner, naming `chmod 600`. The pairing payload carries that certificate's pin too. A device that finds the chain trusted by its system checks the pin once, at pairing (the device side is GUI 7a's). |
| What is the pairing code, and how is the exchange proved? | `[default]` | The typed code is 10 characters of Crockford base32 (about 50 bits), shown as `XXXXX-XXXXX`. It is valid for 10 minutes, used once, and burned after 5 wrong tries. The QR code carries a 128-bit secret in its place, since nobody types it. The exchange's proof is bound to the certificate the device saw: the device sends `HMAC-SHA256(code, pin)`, and the server checks it against its own pin, so a relay presenting a different certificate fails. When the code is typed by hand, the device shows the pin's first groups for the person to compare with the server's start line. |
| What does the QR code carry, and how is it drawn? | `[default]` | Compact JSON, not a URL: `{"apogee_pair":1,"addresses":[…],"port":N,"pin":"sha256/…","secret":"…"}`. The terminal QR is drawn with Nayuki's QR Code generator (MIT), fetched pinned. It is drawn only on a terminal; `--no-qr` or a pipe prints the code and the address lines only. Whether a custom URL scheme should let the Camera open the app is the iPhone app's call (GUI [9a](../../darwin/ios/v0.2.5/iphone-app.md)). |
| Which addresses and port does `devices pair` offer, and does it need the server running? | `[default]` | `devices pair` lists the machine's non-loopback addresses (a `platform` helper over `getifaddrs`, or `GetAdaptersAddresses` on Windows). `--address HOST` replaces them, for a VPN name or a proxy. `--port` defaults to 8080, serve's own default, and the start note prints the exact `devices pair --port N` line. Pairing does not need the server running: the pending code waits in the registry until it expires. |
| Who names a device, and how are names kept unique? | `[default]` | The operator names the device (`--name "Taylor's iPhone"`). Without a name, the device's own offered name is used, mended, cut to 64 characters and de-duplicated with " (2)". Names are unique, ignoring case, and are what `revoke` takes. |
| What does a device token look like, and how often is `last_seen` written? | `[default]` | A device token is 32 random bytes in hex with an `apd_` prefix, so a leaked one is recognizable. `last_seen` is kept in memory and written at most once a minute per device. |
| Where are list, revoke and pairing served? | `[default]` | List and revoke are served on the admin plane (views only). **A pairing code is never minted over HTTP.** It is a live credential, so it appears only on the server's own terminal, or on a local front end over pipes (`devices pair --json`). |
| Can the inference plane be put behind the device gate? | `[default]` | `--devices-only` is in scope and off by default. It puts the inference plane behind the same device gate, and a stock OpenAI client then sends a device token as its API key (its usual `Authorization: Bearer`). `devices pair --print-token --name NAME` mints one for a script on the server's own terminal and prints it once, as `--print-admin-token` does. |
| What happens to requests when a token is revoked? | `[default]` | When a request's token is revoked, the request gets `401` and so does every request after it. |
| Does an open stream from a revoked device end at its next event? | `(consumed decision)` | Belongs to [59b](machine-mode-over-the-network.md). |
| May a paired device also reach admin routes, and which of them? | `(consumed decision)` | [59d](reads-and-commands-over-the-network.md)'s. Until it decides, the admin plane keeps its one token, and a device token opens it nowhere. |
| Where does the device keep its token and pin (the Keychain), and how does it check the pin? | `(consumed decision)` | GUI [7a](../../darwin/v0.2.4/app-connection-local-or-server.md)'s. |

## Decision log

| Date | Decision | Whose call |
|---|---|---|
| 2026-11-07 | Specced from the Apple-platforms spike and brainstorm; the six calls below are the user's | — |
| 2026-11-07 | Apogee is a private, self-hosted harness for the person's own devices: no customer accounts, billing or metering | the user |
| 2026-11-07 | Remote mode is an add-on, never the only way the application runs. The CLI alone and the Mac app over pipes stay complete and the default | the user |
| 2026-11-07 | iPhone and iPad are clients of the person's server only. The Mac and the CLI stay standalone | the user |
| 2026-11-07 | Where the work happens: the server's own tools by default, and a connected desktop offers its own files and shell, so a Mac pointed at a server and asked to change a project on the Mac touches the Mac's files ([60b](../v0.2.4/device-tools.md)). Those tools arrive over a connection this item admits | the user |
| 2026-11-07 | The approved SPEC amendment ([SPEC.md](../../../assistant/cli/SPEC.md) → Core surfaces, item 4, and Principles): `serve` becomes the harness server, and paired devices are authenticated | the user |
| 2026-11-07 | v0.2.5 is the target where macOS, iPadOS and iOS fully work, reached in successive versions | the user |
| 2026-11-07 | First in v0.2.3, track 59, ungated. 59b and 59d are gated on it, and through them every server-facing GUI item (7a onward). v0.2.3 is the server's release, and the app learns to connect in v0.2.4 | the spec |
| 2026-11-07 | The inference plane keeps its behavior for stock clients. The harness plane always requires a paired device, on loopback too, so a test and a deployment exercise the same gate | the spec |
| 2026-11-07 | LAN discovery (Bonjour) is left out of this item (a recorded default for later). A device is given the address by hand (or reads it from the QR code) and pairs | default |
| 2026-11-07 | This revises a recorded behavior, "Plain HTTP only … a deployment terminates TLS in front of it" (`ApogeeDependencies.cmake:48-51`; [http-api.md](../../../reference/cli/http-api.md):1518). Every non-loopback bind now speaks TLS by default. Under ADR 0006 the revision is dated in the reference and the release notes, and the old shape is kept behind `--tls off` | the spec |

## Out of scope

- LAN discovery (Bonjour), a recorded default for later.
- Accounts, billing and metering. The server admits one person's devices.
- What runs behind the gate (59b, 59d).
- Every device-side piece: scanning, the Keychain, the pin check (GUI 7a).
- A TLS rotation verb.
- Mutual TLS or client certificates.
- Pairing over the network without the server's terminal.

## Links

- **Blocked by:** — (earmarked for v0.2.3, track 59, ungated; the first of the track) · **Blocks:** [59b](machine-mode-over-the-network.md), [59d](reads-and-commands-over-the-network.md), [59f](paired-apps-with-scopes.md), [62e](../v0.2.4/connections-and-activity.md), [62g](../v0.2.4/administering-local-and-marked-devices.md), [62h](../v0.2.4/local-admin-reads.md)
- **Relates to:** [59c](many-sessions-at-once.md), [59e](tasks-outlive-the-connection.md), [60b](../v0.2.4/device-tools.md), GUI [7a](../../darwin/v0.2.4/app-connection-local-or-server.md), GUI [8c](../../darwin/v0.2.5/iphone-and-ipad-distribution.md), GUI [9a](../../darwin/ios/v0.2.5/iphone-app.md); [ADR 0001](../../../adrs/cli/layer-enforcement.md), [ADR 0002](../../../adrs/cli/mode-parity.md), [ADR 0006](../../../adrs/cli/backwards-compatibility.md), [ADR 0007](../../../adrs/cli/tab-completion.md), [ADR 0008](../../../adrs/cli/module-map.md), [ADR 0010](../../../adrs/cli/the-shell-is-a-mode.md); [SPEC.md](../../../assistant/cli/SPEC.md) (Core surfaces, item 4); CLI MILESTONES → Milestone U (the admin plane)
- **Revises:** the recorded "Plain HTTP only" rule (`ApogeeDependencies.cmake:48-51`; [http-api.md](../../../reference/cli/http-api.md):1518)
