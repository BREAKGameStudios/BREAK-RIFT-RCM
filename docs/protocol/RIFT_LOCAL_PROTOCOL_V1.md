# RIFT Local Protocol v1

Status: draft for implementation review
Product: RIFT Connection Manager (RCM)
Protocol version: `1`

## 1. Purpose

The RIFT Local Protocol connects a game to the authenticated RIFT Launcher running on the same Windows account. It exposes identity, disposable game tickets, logical multiplayer sessions, matchmaking results, invitations, and pending join context without exposing account tokens or backend infrastructure to the game client.

The protocol coordinates discovery, authorization, and connection handoff. It does not provide sockets, replication, gameplay state, lobbies, maps, teams, `ServerTravel`, `ClientTravel`, listen servers, or dedicated servers.

## 2. Responsibilities and trust boundaries

### Game client

The game client uses an RCM SDK and never receives RIFT access tokens, refresh tokens, Supabase credentials, service-role credentials, or social WebSocket credentials. It does not call the RIFT backend directly.

### RCM client SDK

The SDK discovers the launcher, authenticates local requests, serializes the protocol, ignores stale responses, and exposes engine-native operations and events.

### RIFT Launcher

The launcher owns the authenticated account session, knows which catalog game it launched, validates all local requests, calls the RIFT backend, maintains active host and membership state, and persists pending join context.

### RCM server SDK

A game server may validate a disposable game ticket directly with the public RIFT ticket-validation endpoint. This is the only direct backend operation required by the server-side SDK. The game developer does not implement the HTTP contract directly.

### RIFT Backend

The backend is authoritative for accounts, ownership, friendships, blocks, presence, game identity, sessions, membership, capacity, invitations, expiry, and authorization to receive connection data.

## 3. Discovery

The launcher writes:

```text
%APPDATA%\Rift\launcher.json
```

```json
{
  "protocol_version": 1,
  "port": 12345,
  "secret": "temporary-random-secret"
}
```

Required fields:

- `protocol_version`: integer protocol major version.
- `port`: integer TCP port from `1` through `65535`.
- `secret`: non-empty opaque string.

The file is replaced atomically when the launcher starts listening. The launcher removes only the file that still contains its own port and secret when it stops.

## 4. Local transport and authentication

- The server listens only on `127.0.0.1`.
- The port is dynamically assigned.
- Requests use HTTP/1.1 and JSON encoded as UTF-8.
- Every operation requires `X-Rift-Secret`.
- Game-bound operations also require `X-Rift-Launch-Token`.
- The secret is generated when the launcher starts and is invalid after that launcher instance stops.
- The secret authenticates access to the local launcher; it is not an account token and must never be forwarded remotely.
- The launch token identifies one process launch performed by RIFT. It is not written to `launcher.json`.
- A missing or incorrect secret returns `403`.
- Unsupported paths return `404`.
- Unsupported methods return `405`.

### Active-game identification

When RIFT starts a game, it creates a cryptographically random launch token and injects these environment variables into the game process:

```text
RIFT_LAUNCH_ID=<opaque launch identifier>
RIFT_LAUNCH_TOKEN=<opaque random token>
```

The RCM SDK reads them internally and sends the token as `X-Rift-Launch-Token`. Game code never reads, stores, logs, or configures this value.

The launcher stores an in-memory launch record containing:

- launch identifier;
- hash of the launch token;
- catalog game identifier and slug;
- published build identifier;
- canonical executable path;
- process identifier;
- account identifier;
- start time and process state.

The launcher derives this record from the installed receipt and catalog data before starting the process. The game cannot submit or override its slug. A valid launch token authorizes operations only for the game, account, and process launch to which it was issued.

Child processes intentionally created by the game may inherit the environment. They remain part of the same logical launch. The launcher revokes the launch token when the tracked game process tree ends, the account logs out, or the launcher explicitly terminates the launch context.

The discovery secret proves access to the current local launcher. The launch token proves association with a RIFT-launched game. Both are required for `/session` to issue a game-scoped ticket or expose game and join context, and for all `/sessions` and `/invites` operations.

## 5. Common response and error format

Successful responses use the status documented by each operation. Empty successful responses use `204`.

Errors use:

```json
{
  "error": {
    "code": "stable_machine_code",
    "message": "Human-readable message",
    "details": {}
  }
}
```

Stable local error codes:

| Code | HTTP | Meaning |
|---|---:|---|
| `invalid_request` | 400 | Malformed JSON or invalid field. |
| `unsupported_protocol` | 400 | SDK and launcher have incompatible protocol versions. |
| `local_forbidden` | 403 | Missing or invalid local secret. |
| `invalid_launch` | 403 | Missing, expired, or invalid game launch token. |
| `not_authenticated` | 401 | No authenticated RIFT account. |
| `no_active_game` | 409 | The launcher has no active RIFT-launched game. |
| `wrong_game` | 403 | The operation does not belong to the active game. |
| `game_not_owned` | 403 | Account lacks entitlement. |
| `build_mismatch` | 409 | Active game build cannot join the target session without update or relaunch. |
| `not_friends` | 403 | Invitation target is not an accepted friend. |
| `blocked` | 404 | Interaction is hidden because either side blocked the other. |
| `session_not_found` | 404 | Session does not exist or is not visible. |
| `session_not_joinable` | 409 | Session is closed or unavailable. |
| `session_full` | 409 | Session has no free slots. |
| `already_joined` | 409 | Account is already a participant. |
| `not_joined` | 409 | Account is not a participant. |
| `not_session_host` | 403 | Operation requires the host. |
| `invite_not_found` | 404 | Invitation does not exist or is not visible. |
| `invite_expired` | 409 | Invitation expired. |
| `invite_not_pending` | 409 | Invitation was already resolved. |
| `ticket_invalid` | 400 | Ticket is invalid, expired, consumed, or does not match its expected bindings. |
| `launcher_busy` | 409 | A conflicting local operation is running. |
| `backend_unavailable` | 503 | RIFT backend cannot currently be reached. |

## 6. Identity session and game ticket

### `GET /session`

Returns the current identity and creates a new disposable game ticket when authenticated.

```json
{
  "protocol_version": 1,
  "authenticated": true,
  "public_id": "8MCR-LXFH",
  "username": "sen11k",
  "ticket": "opaque-disposable-ticket",
  "ticket_expires_at": "2026-10-05T20:46:00Z",
  "game": {
    "slug": "overrun-blitzkrieg",
    "build_id": "uuid"
  },
  "join_context": {
    "context_id": "opaque-context-identifier",
    "session_id": "uuid",
    "invitation_id": "uuid",
    "required_build_id": "uuid"
  }
}
```

Required fields are `protocol_version` and `authenticated`. When `authenticated` is true, `public_id` and `username` are required. When a valid launch token is present, `ticket`, `ticket_expires_at`, and `game` are also required. `join_context` is optional. A request without a launch token may retrieve identity but receives no game ticket, game, or join context. A request that supplies an invalid or expired launch token fails with `403 invalid_launch`.

When no account is authenticated, the endpoint returns `200`:

```json
{
  "protocol_version": 1,
  "authenticated": false
}
```

Each authenticated, game-bound call issues a fresh single-use ticket bound to the account, catalog game, build, and launch context. Issuing a fresh ticket may invalidate an older unused ticket for the same account and game. SDK consumers must reject out-of-order responses and must not reuse a submitted ticket.

Calling this endpoint does not consume `join_context`.

### Game-ticket model

A game ticket is an opaque, cryptographically random, short-lived bearer credential. The raw ticket is returned exactly once to the launcher and stored only as a cryptographic hash by the backend.

Every ticket is bound to:

- one account;
- one catalog game;
- one published build;
- one launcher-generated launch identifier;
- one issuance time and expiration time;
- one active game session when the account currently hosts or has joined a session.

The session binding is optional for identity-only game flows. When present, the backend verifies active membership when issuing the ticket and again when validating it.

The default validity is 60 seconds. Deployments may configure a shorter duration. SDKs use `ticket_expires_at` for diagnostics and must request another ticket rather than attempting to refresh or reuse one.

Issuing a new ticket invalidates any older unused ticket for the same account and game. Validation consumes the ticket atomically. Concurrent validations can produce at most one success.

The backend never stores or returns the local discovery secret or launch token. It stores the opaque launch identifier only to bind issuance records and support security auditing.

### Server-side ticket validation

The RCM server SDK validates a ticket through the public backend operation equivalent to:

```http
POST /v1/game/tickets/validate
Content-Type: application/json
```

```json
{
  "ticket": "opaque-disposable-ticket",
  "expected_game_slug": "overrun-blitzkrieg",
  "expected_build_id": "uuid",
  "expected_session_id": "uuid"
}
```

`expected_game_slug` and `expected_build_id` are required and come from trusted server configuration or, for a RIFT-launched listen server, from the active launch context. `expected_session_id` is required for session-bound multiplayer authentication and omitted only for identity-only flows.

Successful validation returns:

```json
{
  "valid": true,
  "public_id": "8MCR-LXFH",
  "username": "sen11k",
  "game_slug": "overrun-blitzkrieg",
  "build_id": "uuid",
  "session_id": "uuid"
}
```

`session_id` is omitted for an identity-only ticket. The backend consumes the ticket only when every expected binding matches. A game mismatch, session mismatch, expired ticket, already consumed ticket, invalid ticket, revoked membership, closed session, or inactive account returns the same public error:

```json
{
  "error": {
    "code": "ticket_invalid",
    "message": "Ticket is invalid, expired, already used, or not valid for this game session."
  }
}
```

The uniform error prevents callers from using validation as an information oracle. Validation responses must never echo the submitted ticket.

Dedicated servers that cannot inherit a launcher context require a future server-credential mechanism. Protocol v1 initially supports RIFT-launched listen servers and server deployments configured with a trusted game identity. A user access token or launcher secret must never be installed on a dedicated server.

### `POST /session/join-context/consume`

Confirms that the active game received the pending context.

Request:

```json
{
  "context_id": "opaque-context-identifier",
  "session_id": "uuid",
  "invitation_id": "uuid"
}
```

`invitation_id` is optional when the player joined without an invitation. The launcher clears the context only when `context_id`, `session_id`, the authenticated account, and the active game all match the stored context. A stale game instance cannot consume a replacement context. Success returns `204`.

### `DELETE /session/join-context`

Cancels the pending context for the authenticated account and active game. The launcher leaves the backend session when appropriate, then clears the local context. Success returns `204`. User confirmation belongs to the launcher or game UI, not to this transport operation.

## 7. Session representation

```json
{
  "session_id": "uuid",
  "game_slug": "overrun-blitzkrieg",
  "build_id": "uuid",
  "host_public_id": "8MCR-LXFH",
  "host_username": "sen11k",
  "map_id": "map-identifier",
  "mode_id": "mode-identifier",
  "current_players": 1,
  "max_players": 10,
  "state": "open",
  "joinable": true,
  "friends_only": true,
  "connection_string": "opaque-value",
  "revision": 1,
  "created_at": "2026-10-05T20:45:00Z",
  "updated_at": "2026-10-05T20:45:00Z"
}
```

`connection_string` is omitted from searches and unauthorized reads. It is returned only after an authorized join or to an authorized participant.

Session fields use these rules:

| Field | Rules |
|---|---|
| `session_id` | UUID generated by the backend; immutable. |
| `game_slug` | Catalog slug derived by RIFT; immutable. |
| `build_id` | Published build used by the host; immutable for the session. |
| `host_public_id` | Public RIFT identity of the host; immutable. |
| `host_username` | Display value; clients must not use it as identity. |
| `map_id` | Optional opaque UTF-8 string, maximum 128 characters. |
| `mode_id` | Optional opaque UTF-8 string, maximum 128 characters. |
| `current_players` | Backend-calculated participant count including the host. |
| `max_players` | Integer from 1 through 1024. |
| `state` | `open` or `closed`. |
| `joinable` | Host-controlled availability, additionally constrained by state and capacity. |
| `friends_only` | When true, new participants must be accepted friends of the host. |
| `connection_string` | Opaque UTF-8 string, 1 through 2048 characters; authorized responses only. |
| `revision` | Positive integer incremented by every successful session mutation. |
| `created_at`, `updated_at` | UTC RFC 3339 timestamps generated by the backend. |

All identifiers are strings in JSON. Clients treat UUIDs and opaque identifiers as case-insensitive only where explicitly documented; UUID values should be preserved as returned. Timestamps are UTC RFC 3339.

### Session invariants

The backend enforces the following invariants independently of launcher or SDK behavior:

- An account may host at most one open session for a given game.
- An account may belong to at most one active session for a given game.
- Creating a host session is idempotent for the same account and active game. When an open hosted session already exists, the operation returns that session instead of creating a duplicate.
- Joining a session performs an atomic membership transition. Any previous non-host membership for the same game is removed before the new membership is inserted.
- If the account currently hosts another open session for the same game, joining a different session closes the hosted session, invalidates its pending invitations, notifies its participants, and then inserts the new membership in the same logical operation.
- A failed transition leaves the previous valid membership unchanged.
- Session capacity includes the host.
- Closing a session removes its active memberships after marking the session closed and invalidating pending invitations and join contexts.

These rules prevent one account from appearing in multiple active matches for the same game and make retries converge on one authoritative state.

## 8. Hosting

### `POST /sessions/host`

Creates a host session for the active game. The launcher and backend derive the game from the process launched by RIFT; the request cannot select a game slug.

```json
{
  "max_players": 10,
  "connection_string": "opaque-value",
  "map_id": "map-identifier",
  "mode_id": "mode-identifier",
  "joinable": true,
  "friends_only": true
}
```

All fields are required when creating a host except `map_id` and `mode_id`, which may be empty. `connection_string` is opaque and must not be parsed or rewritten. Creating a new session returns `201`; returning the caller's existing compatible hosted session returns `200`.

### `GET /sessions/host`

Returns the caller's active hosted session for the active game, or `404 session_not_found`.

### `PATCH /sessions/host`

Updates provided host fields. Omitted fields remain unchanged. The host may update capacity, connection string, map, mode, joinability, and friends-only policy. The request must contain at least one mutable field. `max_players` cannot be reduced below `current_players`. Success returns `200` with the updated session and incremented revision.

```json
{
  "max_players": 12,
  "connection_string": "opaque-value",
  "map_id": "map-identifier",
  "mode_id": "mode-identifier",
  "joinable": true,
  "friends_only": true
}
```

### `DELETE /sessions/host`

Closes the hosted session, makes it non-joinable, expires pending invitations, and clears related join contexts. Success returns `204`.

## 9. Finding and reading games

### `GET /sessions`

Returns visible open sessions for the active game. The game slug is never accepted as a client-selected filter. Protocol v1 uses exact build compatibility: a running game sees only sessions whose `build_id` matches its active build.

Optional query parameters:

| Parameter | Meaning |
|---|---|
| `map_id` | Exact opaque map identifier. |
| `mode_id` | Exact opaque mode identifier. |
| `available_only` | Boolean; defaults to true. |
| `limit` | Integer from 1 through 100; defaults to 25. |
| `cursor` | Opaque pagination cursor returned by the previous response. |

```json
{
  "sessions": [],
  "next_cursor": "opaque-cursor"
}
```

`next_cursor` is omitted when no next page exists. Results use stable backend ordering and cursors; clients must not construct cursors. Search results omit `connection_string`. A friends-only session is visible only to the host, existing participants, and accepted friends of the host.

### `GET /sessions/{session_id}`

Returns `200` with a visible session. `connection_string` is included only when the account is an authorized participant or host. A session hidden by access policy returns `404 session_not_found`.

## 10. Joining and leaving

### `POST /sessions/{session_id}/join`

The backend validates game identity, entitlement, visibility, capacity, state, and existing membership. Joining the session performs the atomic membership transition defined by the session invariants. Repeating the operation for the already active target session returns the current authorized context rather than creating another membership. Success returns an authorized connection context:

```json
{
  "session": {},
  "join_context": {
    "context_id": "opaque-context-identifier",
    "session_id": "uuid",
    "required_build_id": "uuid"
  }
}
```

The launcher persists this as `pending_join_context` before responding to the SDK.

### `POST /sessions/leave`

Removes the account from its current non-host membership for the active game and clears matching pending context. Success returns `204`. A host must close its hosted session instead.

## 11. Invitations

Invitation representation:

```json
{
  "invitation_id": "uuid",
  "session_id": "uuid",
  "game_slug": "overrun-blitzkrieg",
  "game_title": "OVER•RUN: BLITZKRIEG",
  "inviter_public_id": "8MCR-LXFH",
  "inviter_username": "sen11k",
  "recipient_public_id": "TARGET-ID",
  "recipient_username": "friend",
  "status": "pending",
  "expires_at": "2026-10-05T21:00:00Z",
  "created_at": "2026-10-05T20:45:00Z"
}
```

Statuses are `pending`, `accepted`, `declined`, `cancelled`, and `expired`.

Invitation identifiers are backend-generated UUIDs. Display names are snapshots for presentation and never replace public IDs as identity. The default invitation lifetime is 15 minutes and may be shortened by backend policy.

### `GET /invites`

Returns invitations visible to the authenticated account. By default, returns pending incoming and outgoing invitations for the active game.

Optional query parameters are `direction=incoming|outgoing|all`, `status`, `limit` from 1 through 100, and an opaque `cursor`.

```json
{
  "invites": [],
  "next_cursor": "opaque-cursor"
}
```

`next_cursor` is omitted when no next page exists. Connection strings are never included in invitation responses.

### `POST /invites`

```json
{
  "public_id": "TARGET-ID"
}
```

The backend derives the game from the authenticated sender's `playing` presence and validates the sender's active session membership. The host may invite friends. A non-host participant may also invite friends while the session is open and joinable. Only accepted friends may be invited. Offline friends may receive invitations. Success returns `201` with the invitation.

Only `public_id` is accepted in the request. Creating a duplicate pending invitation for the same session, inviter, and recipient returns the existing invitation with `200`. It does not extend the original expiration time.

### `POST /invites/{invitation_id}/accept`

Only the recipient may accept. The backend revalidates expiry, session state, game entitlement, availability of the required published build, capacity, friendship, blocks, and membership. It atomically performs the membership transition, marks the invitation accepted, and creates the authorized join context. If any part fails, the invitation remains pending when it is still valid and the previous membership remains unchanged. The launcher persists the returned join context. A first acceptance returns `200` with the authorized session and join context. Repeating acceptance by the same recipient returns the current authorized context when the membership and session remain valid.

Acceptance may occur while another build is installed or running. The context records `required_build_id`; the launcher installs or updates to that build and relaunches before exposing the context. A running incompatible game does not receive the connection string and receives `409 build_mismatch` if it attempts to join directly.

```json
{
  "session": {},
  "join_context": {
    "context_id": "opaque-context-identifier",
    "session_id": "uuid",
    "invitation_id": "uuid",
    "required_build_id": "uuid"
  }
}
```

### `POST /invites/{invitation_id}/decline`

Only the recipient may decline a pending invitation. Repeating a decline for an invitation already declined by that recipient is idempotent. Success returns `204`.

### `POST /invites/{invitation_id}/cancel`

Only the inviter may cancel a pending invitation. Repeating cancellation for an invitation already cancelled by that inviter is idempotent. Success returns `204`.

## 12. Pending join context lifecycle

The launcher persists at most one pending join context per authenticated account. A context contains only identifiers and lifecycle metadata:

```json
{
  "context_id": "opaque-context-identifier",
  "account_id": "internal-account-identifier",
  "game_slug": "overrun-blitzkrieg",
  "session_id": "uuid",
  "invitation_id": "uuid",
  "required_build_id": "uuid",
  "created_at": "2026-10-05T20:45:00Z",
  "expires_at": "2026-10-05T21:00:00Z"
}
```

`invitation_id` is optional. The connection string and disposable game ticket are not persisted. When the SDK requests the session, the launcher refreshes authorized connection data from the backend and issues a fresh ticket.

The context is written atomically to launcher-owned application data. On restart, it is loaded only after restoring the same authenticated account and revalidating the session with the backend. Invalid, expired, mismatched, or unauthorized state is deleted.

It survives installation, update, repair, process startup, SDK initialization, game-process failure before consumption, launcher restart when the same account session remains valid, and repeated `GET /session` calls.

It is cleared only when:

- the active game explicitly consumes it;
- the invitation expires or is invalidated;
- the session closes;
- the user cancels;
- the account leaves the session;
- logout occurs;
- a newer context explicitly replaces it.

Creating a newer context for the account atomically replaces the previous context and generates a new `context_id`. A request holding the old identifier cannot consume the replacement.

The launcher exposes a context only when its account and game match a valid active launch token. Merely knowing the discovery secret is insufficient. Reading a context updates delivery diagnostics but does not change its lifecycle state.

Only the context matching the active game is visible to that game. Logout clears the locally persisted context for the previous account.

## 13. Connection-string authorization

- The value is opaque to RIFT.
- It is encrypted in transit by remote HTTPS.
- It is never included in public or merely visible listings.
- It is returned only after join authorization or to existing participants.
- It is exposed locally only to the active matching game.
- Closing a session invalidates authorization to retrieve it.

## 14. Events

The remote backend uses the launcher's authenticated WebSocket to notify changes. The launcher translates them into local state updates. Protocol v1 may initially expose changes through refreshable SDK events; a future compatible addition may define a local event stream.

Required semantic events include session changed, host closed, invitation received, invitation resolved, membership changed, and pending connection requested. Event delivery is a hint: SDKs must be able to refresh authoritative state after reconnecting.

## 15. Ordering and retries

- SDKs assign a monotonically increasing generation to replaceable requests.
- A response from an older generation must not overwrite newer state.
- Mutating retries are allowed only when the launcher or backend can identify the same operation idempotently.
- Ticket requests are not retried by reusing a returned ticket.
- After reconnecting, the SDK refreshes identity, hosted session, membership, invitations, and pending join context.

## 16. Required failure behavior

### Launcher absent

Missing, unreadable, stale, or unreachable launcher configuration produces an SDK-level `launcher_unavailable` error. The game may continue without RIFT features when its own policy permits.

### No login

`GET /session` returns `200` with `authenticated: false`. Operations requiring an account return `401 not_authenticated`.

### No active game

Session, matchmaking, and invitation mutations return `409 no_active_game`. Identity retrieval may still succeed without a `game` object.

### Session closed

New joins fail with `session_not_joinable`. Pending invitations and matching join contexts are invalidated. Existing game-network disconnection remains the game's responsibility.

### Invitation expired

Accepting returns `409 invite_expired`. The launcher removes matching pending UI state and does not create a join context.

## 17. Version compatibility

Protocol version `1` identifies the major compatibility contract. New optional response fields, new endpoints, and new stable error codes may be added within v1. Removing fields, changing field meaning or type, weakening authorization, or changing existing endpoint semantics requires a new major version.

An SDK must ignore unknown JSON fields. A launcher must reject a request that explicitly requires an unsupported major version.

## 18. Unreal SDK mapping

The Unreal implementation is the `RiftConnectionManager` plugin and exposes `URiftConnectionSubsystem`. It owns discovery, local HTTP, parsing, request generations, cached identity, host state, search results, invitations, and join context.

The plugin also contains optional server-side ticket validation and a multiplayer authentication component. BLITZKRIEG removes its private `RiftSubsystem`, network authentication component, and ticket validator after adopting the plugin. Game code retains only engine-level reactions, such as starting a listen server and performing `ClientTravel` when `OnConnectionRequested` supplies an authorized opaque connection string.

The current BLITZKRIEG integration must not be copied verbatim where it logs the raw game ticket. The RCM implementation may log request generations, expiration timestamps, game slug, session identifier, and success or failure codes, but never the ticket value.

## 19. Security requirements

- Local server binds exclusively to loopback.
- Discovery secrets, launch tokens, tickets, and connection strings must not be logged.
- Game-bound operations require both the discovery secret and the per-launch token.
- The per-launch token is delivered through the launched process environment and is never stored in `launcher.json`.
- The launcher must limit request body size and reject malformed requests.
- Object and session identifiers are validated server-side on every mutation.
- The backend never trusts a client-selected game slug for local game operations.
- Invitation acceptance and membership insertion are atomic.
- Ticket validation consumes the ticket on first successful use.
- Blocks deliberately hide interaction targets as not found where appropriate.
