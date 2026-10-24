# Satisfactory Live Map

A server-side Satisfactory mod that streams the world state (players, buildings,
rails, trains, trucks, drones) over WebSocket to a browser map built on Leaflet.

Status: scaffold only. Nothing here has been compiled against the Starter Project yet.

## Decision: build from scratch, use FRM as a reference

See [docs/DECISION.md](docs/DECISION.md). Short version:

- Ficsit Remote Monitoring (FRM) has **no license file**. Default copyright applies,
  so forking and redistributing it (including on ficsit.app) needs Porisius's
  permission. That alone kills "just fork it".
- FRM is a REST polling API (`/api/getTrains`, `/api/getPlayer`, ~90 endpoints)
  that rebuilds full JSON on every request. A live map wants the opposite:
  push-based deltas on a WebSocket, with static geometry sent once.
- FRM is still useful: it proves uWebSockets runs inside the game on Windows and
  Linux dedicated servers, and its endpoint code shows which FactoryGame classes
  hold the data. Read it; don't copy it.

## Layout

```
Mod/LiveMap/          SML plugin. Drop into <StarterProject>/Mods/LiveMap
  LiveMap.uplugin
  Source/LiveMap/     C++ module: subsystem, collectors, server
web/                  Vite + TypeScript + Leaflet frontend
docs/
  DECISION.md         Fork vs scratch reasoning
  PROTOCOL.md         WebSocket message format
```

## Architecture

```
Game thread                         Server thread (uWS loop)           Browser
-----------                         ------------------------           -------
ALiveMapSubsystem
  dynamic tick (5 Hz)  ->  snapshot  ->  diff vs last  ->  "delta"  ->  Leaflet markers
    players, trains, vehicles, drones
  static, event-driven ->  build/dismantle hooks ->       "upsert"/"remove"
    buildings, rails, belts
  on client connect    ->  full "snapshot" message
```

Rules that matter:

1. **Only touch UObjects on the game thread.** Collectors run on the game thread
   and produce plain structs / JSON strings. The server thread only ever sees
   serialized bytes handed over through a lock-free queue.
2. **Static vs dynamic layers.** A big save has tens of thousands of buildables.
   Sending them every tick is how you get a 20 MB/s map. Buildings and rails go
   out once on connect, then as incremental changes.
3. **Server-side only.** `RequiredOnRemote: false`. It runs on the host or the
   dedicated server; clients don't need the mod.

## Dev setup

1. Follow the SML docs to set up the Starter Project: Coffee Stain's UE5 fork
   (via Epic/GitHub account link), Visual Studio 2022 with the C++ game workload
   and the exact MSVC/Windows SDK versions the docs pin, Wwise integration.
   https://docs.ficsit.app/satisfactory-modding/latest/Development/BeginnersGuide/dependencies.html
2. Copy or symlink `Mod/LiveMap` into `<StarterProject>/Mods/LiveMap`.
3. Regenerate project files, build `Development Editor`.
4. In the editor, create a `GameWorldModule` blueprint for the mod and add
   `LiveMapSubsystem` to its Mod Subsystems list (SML registers subsystems via
   that asset; it can't be created from C++ here).
5. Package with Alpakit (Windows + LinuxServer targets).
6. Frontend: `cd web && npm install && npm run dev`, point it at
   `ws://<host>:8086/ws` (port configurable).

## Roadmap

- [ ] M0: Frontend renders mock data with correct coordinate transform
- [ ] M1: Mod boots, serves `/health`, logs player positions
- [ ] M2: WebSocket server + players/vehicles/trains dynamic layer
- [ ] M3: Static layer: rails (splines), buildings (footprints), belts
- [ ] M4: Build/dismantle events -> incremental updates
- [ ] M5: Config (port, tick rate, auth token), dedicated server testing
- [ ] M6: Serve the built frontend from the mod itself

## Open questions

- **License.** Not picked. Pick one before publishing (MIT or MPL-2.0 are the
  usual choices for SML mods).
- **Map tiles.** The background map is a Coffee Stain asset. Every community map
  uses an extracted copy, but that's their IP; don't commit it to a public repo
  without thinking about it.
- **WebSocket library.** Leaning uWebSockets (Apache-2.0, proven in FRM). civetweb
  (MIT, single C file) is the simpler-to-vendor fallback.
- **Auth.** An open port on a dedicated server leaks base layouts and player
  positions. Needs at least a shared token before anyone exposes it publicly.
