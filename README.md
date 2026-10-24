# Satisfactory Live Map

A server-side Satisfactory mod that streams the world state (players, buildings,
rails, trains, trucks, drones) over WebSocket to a browser map built on Leaflet.

Status: works in-game (Satisfactory 1.2, SML 3.12) for players, trains,
vehicles and rails. Buildings, belts, pipes, hypertubes and power lines are
tested end to end against the sim but not yet in a real save. See [Testing](#testing).

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
Mod/LiveMap/                      SML plugin. Drop into <StarterProject>/Mods/LiveMap
  LiveMap.uplugin
  Source/LiveMap/
    Public/LiveMapSubsystem.h     Unreal glue: collects actors on the game thread
    Private/Core/                 Engine-free C++: FrameDiffer, JsonWriter, Server
    Private/Core/Civetweb.c       Compiles vendored civetweb into the module
  Source/ThirdParty/civetweb/     civetweb v1.16, unmodified (MIT)
web/                              Vite + TypeScript + Leaflet frontend
tests/core/                       CMake build of the core + unit/integration tests + sim
tests/e2e/                        Headless-browser test: sim -> WebSocket -> frontend
docs/
  DECISION.md                     Fork vs scratch reasoning
  PROTOCOL.md                     WebSocket message format
```

## Architecture

```
Game thread                         Server thread (uWS loop)           Browser
-----------                         ------------------------           -------
ALiveMapSubsystem                   livemap::Server
  5 Hz: collect actors                sender thread: owns all writes
    -> FrameDiffer.Update  -- seq'd delta -->  queue  -->  "delta"     ->  Leaflet markers
    -> FrameDiffer.Snapshot (cached) ------->          new client / resync -> "snapshot"
  10 s: rescan rails + buildings     GET /static/buildings, /static/lines
    -> encode on thread pool  ---->  SetStatic (versioned)  ---->  canvas tiles
```

Rules that matter:

1. **Only touch UObjects on the game thread.** Collectors run on the game thread
   and produce plain structs and JSON strings. The server threads only ever see
   strings. `Publish()` never blocks on the network: a slow client stalls the
   sender thread, the queue overflows, and everyone gets a fresh snapshot.
2. **Static vs dynamic layers.** A big save has 100k+ buildables. Sending them
   every tick is how you get a 20 MB/s map. Buildings, belts and pipes are HTTP
   documents the client fetches when the snapshot announces a new version.
3. **Server-side only.** `RequiredOnRemote: false`. It runs on the host or the
   dedicated server; clients don't need the mod.

## Dev setup

1. Follow the SML docs to set up the Starter Project: Coffee Stain's UE5 fork
   (via Epic/GitHub account link), Visual Studio 2022 with the C++ game workload
   and the exact MSVC/Windows SDK versions the docs pin, Wwise integration.
   https://docs.ficsit.app/satisfactory-modding/latest/Development/BeginnersGuide/dependencies.html
2. Copy or symlink `Mod/LiveMap` into `<StarterProject>/Mods/LiveMap`.
3. Regenerate project files, build `Development Editor`.
4. Nothing to set up in the editor: `FLiveMapModule` spawns the subsystem on
   every authoritative game world.
5. Package with Alpakit (Windows + LinuxServer targets).
6. Frontend: `cd web && npm install && npm run dev`, point it at
   `ws://<host>:8086/ws` (port configurable).

## Testing

What has been verified, and how to rerun it:

```bash
# Core: unit + integration tests (Linux/macOS, or MinGW for a Windows build)
cmake -S tests/core -B build/core && cmake --build build/core
./build/core/test_core && ./build/core/test_server

# Same under ThreadSanitizer (suppressions only cover civetweb internals)
cmake -S tests/core -B build/tsan -DSANITIZE=thread && cmake --build build/tsan
TSAN_OPTIONS=suppressions=$PWD/tests/core/tsan.supp ./build/tsan/test_server

# End to end: C++ sim server -> WebSocket -> frontend in headless Chromium
(cd web && npm install && npm run build && npx vite preview --port 4173 &)
node tests/e2e/e2e.mjs build/core/livemap_sim http://127.0.0.1:4173/ shot.png

# Frontend against the sim by hand
./build/core/livemap_sim 8086 &
(cd web && npm run dev)   # open http://localhost:5173, or add ?mock for no server
```

Verified on Windows: UBT build, Alpakit packaging, players/trains/vehicles/rails
in a real save. Not verified yet: buildings in a real save, the Linux cross
toolchain, a dedicated server.

## Roadmap

- [x] M0: Frontend renders data with the world-bounds coordinate transform
- [x] M1: Mod boots, serves `/health`, logs player positions
- [x] M2: WebSocket server + players/vehicles/trains dynamic layer (works in-game)
- [ ] M3: Static layers: rails (done), buildings (footprints, incl. lightweight foundations), belts, pipes, hypertubes, power lines (written and tested outside the game; needs in-game run); terrain background
- [ ] M4: Build/dismantle events -> incremental updates
- [ ] M5: Config (port, tick rate, auth token), dedicated server testing
- [ ] M6: Serve the built frontend from the mod itself

## Open questions

- **License.** Not picked. Pick one before publishing (MIT or MPL-2.0 are the
  usual choices for SML mods).
- **Map tiles.** The background map is a Coffee Stain asset. The mod renders it
  at runtime from the player's own install and serves it at `/static/map`, so
  the repo never contains it. Don't commit an extracted copy to a public repo.
- **WebSocket library.** Went with civetweb (MIT) over uWebSockets. uWS needs
  prebuilt uSockets/libuv binaries per platform (FRM ships them); civetweb is
  one C file UBT compiles for Win64 and Linux itself. Cost: one thread per
  viewer, which is fine for a map.
- **Auth.** An open port on a dedicated server leaks base layouts and player
  positions. Needs at least a shared token before anyone exposes it publicly.
