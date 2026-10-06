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
- The secret is generated when the launcher starts and is invalid after that launcher instance stops.
- The secret authenticates access to the local launcher; it is not an account token and must never be forwarded remotely.
- A missing or incorrect secret returns `403`.
- Unsupported paths return `404`.
- Unsupported methods return `405`.

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
| `not_authenticated` | 401 | No authenticated RIFT account. |
| `no_active_game` | 409 | The launcher has no active RIFT-launched game. |
| `wrong_game` | 403 | The operation does not belong to the active game. |
| `game_not_owned` | 403 | Account lacks entitlement. |
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
  "game": {
    "slug": "overrun-blitzkrieg"
  },
  "join_context": {
    "session_id": "uuid",
    "invitation_id": "uuid"
  }
}
```

Required fields are `protocol_version` and `authenticated`. When `authenticated` is true, `public_id`, `username`, and `ticket` are required. `game` and `join_context` are optional.

When no account is authenticated, the endpoint returns `200`:

```json
{
  "protocol_version": 1,
  "authenticated": false
}
```

Each authenticated call issues a fresh single-use ticket. Issuing a fresh ticket may invalidate an older unused ticket for the same account. SDK consumers must reject out-of-order responses and must not reuse a submitted ticket.

Calling this endpoint does not consume `join_context`.

### `POST /session/join-context/consume`

Confirms that the active game received the pending context.

Request:

```json
{
  "session_id": "uuid",
  "invitation_id": "uuid"
}
```

`invitation_id` is optional when the player joined without an invitation. The launcher clears the context only when the supplied identifiers match the stored context. Success returns `204`.

## 7. Session representation

```json
{
  "session_id": "uuid",
  "game_slug": "overrun-blitzkrieg",
  "host_public_id": "8MCR-LXFH",
  "host_username": "sen11k",
  "map_id": "map-identifier",
  "mode_id": "mode-identifier",
  "current_players": 1,
  "max_players": 10,
  "joinable": true,
  "friends_only": true,
  "connection_string": "opaque-value"
}
```

`connection_string` is omitted from searches and unauthorized reads. It is returned only after an authorized join or to an authorized participant.

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

`max_players` must be positive. `connection_string` is opaque and must not be parsed or rewritten. Success returns `201` with the authorized session representation.

### `GET /sessions/host`

Returns the caller's active hosted session for the active game, or `404 session_not_found`.

### `PATCH /sessions/host`

Updates provided host fields. Omitted fields remain unchanged. The host may update capacity, connection string, map, mode, joinability, and friends-only policy. Success returns the updated session.

### `DELETE /sessions/host`

Closes the hosted session, makes it non-joinable, expires pending invitations, and clears related join contexts. Success returns `204`.

## 9. Finding and reading games

### `GET /sessions`

Returns visible joinable sessions for the active game. Optional query parameters may filter `map_id`, `mode_id`, and available slots. The game slug is never accepted as a client-selected filter.

```json
{
  "sessions": []
}
```

Search results omit `connection_string`.

### `GET /sessions/{session_id}`

Returns a visible session. `connection_string` is included only when the account is an authorized participant or host.

## 10. Joining and leaving

### `POST /sessions/{session_id}/join`

The backend validates game identity, entitlement, visibility, capacity, state, and existing membership. Success returns an authorized connection context:

```json
{
  "session": {},
  "join_context": {
    "session_id": "uuid"
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
  "inviter_public_id": "8MCR-LXFH",
  "inviter_username": "sen11k",
  "status": "pending",
  "expires_at": "2026-10-05T21:00:00Z",
  "created_at": "2026-10-05T20:45:00Z"
}
```

Statuses are `pending`, `accepted`, `declined`, `cancelled`, and `expired`.

### `GET /invites`

Returns invitations visible to the authenticated account. By default, returns pending incoming and outgoing invitations.

### `POST /invites`

```json
{
  "public_id": "TARGET-ID"
}
```

The backend derives the game from the authenticated sender's `playing` presence and derives or validates the sender's active hosted session. Only accepted friends may be invited. Offline friends may receive invitations. Success returns `201` with the invitation.

### `POST /invites/{invitation_id}/accept`

Only the recipient may accept. The backend revalidates expiry, session state, game entitlement, capacity, friendship, blocks, and membership. It atomically accepts the invitation and adds the recipient to the session. The launcher persists the returned join context. Success returns the authorized session and join context.

### `POST /invites/{invitation_id}/decline`

Only the recipient may decline a pending invitation. Success returns `204`.

### `POST /invites/{invitation_id}/cancel`

Only the inviter may cancel a pending invitation. Success returns `204`.

## 12. Pending join context lifecycle

The launcher persists one pending join context per authenticated account. It survives installation, update, repair, process startup, SDK initialization, launcher restart when the account session remains valid, and repeated `GET /session` calls.

It is cleared only when:

- the active game explicitly consumes it;
- the invitation expires or is invalidated;
- the session closes;
- the user cancels;
- the account leaves the session;
- logout occurs;
- a newer context explicitly replaces it.

The launcher must verify that the context game matches the game it launches before exposing it locally.

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

## 19. Security requirements

- Local server binds exclusively to loopback.
- Secrets and tickets must not be logged.
- The launcher must limit request body size and reject malformed requests.
- Object and session identifiers are validated server-side on every mutation.
- The backend never trusts a client-selected game slug for local game operations.
- Invitation acceptance and membership insertion are atomic.
- Ticket validation consumes the ticket on first successful use.
- Blocks deliberately hide interaction targets as not found where appropriate.
