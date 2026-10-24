# Fork FRM or build from scratch?

Researched 2026-10-24 against porisius/FicsitRemoteMonitoring `main`
(v1.5.4, last commit 2026-10-15, SML ^3.12, GameVersion >= 513885).

## What FRM is

- Two runtime modules: `FicsitRemoteMonitoring` (data + HTTP) and
  `FicsitRemoteMonitoringServer` (dedicated server glue).
- Embedded uWebSockets with prebuilt libs in `Source/ThirdParty`.
- ~8.3k lines of C++ outside third-party code.
- HTTP API: `GET /api/:Endpoint`, ~90 endpoints (`getPlayer`, `getTrains`,
  `getTrainRails`, `getTruck`, `getDrone`, `getBelts`, `getFactory`, ...).
  Each request walks the world and builds JSON from scratch.
- Has a WebSocket route (`app.ws("/*")`), but the core model is request/response.
- Bundled web UI is a prebuilt Next.js export in `www/`; source lives in a
  separate repo.
- Also does serial output, chat, power switches, sink stats: a lot of scope
  that isn't a map.

## Why not fork

1. **No license.** No LICENSE file in the repo; `CONTRIBUTING.md` refers to "the
   project license" that doesn't exist. Without one, you have no right to
   redistribute modified code. You could ask Porisius, but don't build on it
   assuming yes.
2. **Wrong data model for a live map.** Polling full JSON for every building at
   map refresh rates doesn't scale on large saves. A live map needs a snapshot
   plus deltas pushed to clients. Retrofitting that into FRM means rewriting its
   core, at which point you own a fork of a large codebase you didn't write,
   chasing upstream merges.
3. **Scope.** Most of FRM is unrelated to maps. A fork inherits all of it.

## Why not just use FRM as-is

Actually a reasonable option, and worth doing for M0: point the Leaflet frontend
at a running FRM instance (`/api/getPlayer`, `/api/getTrains`, `/api/getTrainRails`)
to nail down the coordinate transform and rendering before writing any C++.
If that's good enough for you, you don't need a mod at all.

You'd build your own when you want: push updates, smooth train/vehicle movement,
large-save performance, and control over the protocol.

## Recommendation

Build from scratch. Read FRM's endpoint implementations to learn which
FactoryGame classes and subsystems hold each piece of data, then write your own
collectors. Don't copy code from it.

Risks you're taking on:
- C++ against FactoryGame headers that change between game updates (true either way).
- Owning the embedded server and its threading. FRM touches shared state from
  uWS callbacks; do the game-thread/server-thread split properly from day one.
