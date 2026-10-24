# WebSocket protocol (v1)

Endpoint: `ws://<host>:<port>/ws` (default port 8086). JSON text frames.
Positions are Unreal world units (cm) rounded to integers, `[x, y, z]`; yaw is
whole degrees. The frontend converts to map space (`web/src/coords.ts`).

`GET /health` returns `{"ok":true,"clients":N,"seq":S}`.

Big, slow-changing layers (buildings, belts, pipes) are not sent over the
socket. They are JSON documents at `GET /static/<name>`, and the snapshot lists
their current versions. See [Static layers](#static-layers).

## Sequence numbers

Every delta carries `seq`, incrementing by one. A snapshot carries the `seq` of
the last delta already folded into it. Clients:

- replace their whole state on `snapshot`, then
- apply a `delta` only if `delta.seq > lastSeq`.

The server sends a client a snapshot when it connects, and again (a "resync")
when the client fell too far behind or the static layer changed. Deltas are
never dropped selectively, so a client is always either in step or resynced.

## Server -> client

### `hello`
```json
{ "type": "hello", "version": 1, "tickHz": 5 }
```

### `snapshot`
```json
{
  "type": "snapshot", "seq": 41,
  "players":  [{ "id": "Char_Player_C_1", "name": "Anthony", "kind": "player", "pos": [0,0,0], "yaw": 90 }],
  "trains":   [{ "id": "FGTrain_3", "name": "Iron Express", "kind": "train", "pos": [0,0,0], "yaw": 0 }],
  "vehicles": [{ "id": "BP_Truck_C_2", "name": "BP_Truck_C", "kind": "wheeled", "pos": [0,0,0], "yaw": 0 }],
  "rails":    [{ "id": "Build_RailroadTrack_C_7", "points": [[0,0,0],[1000,0,0]] }],
  "static":   { "buildings": 3, "lines": 4 }
}
```

### `delta`
Only entities that moved (more than 10 cm or 1 degree) or changed, plus removals.
Layers with nothing to say are omitted.
```json
{ "type": "delta", "seq": 42,
  "players": [{ "id": "Char_Player_C_1", "name": "Anthony", "kind": "player", "pos": [10,0,0], "yaw": 95 }],
  "removed": { "vehicles": ["BP_Truck_C_2"] } }
```

## Static layers

`GET /static/buildings` and `GET /static/lines` return JSON with
`X-LiveMap-Version: N` and `Cache-Control: no-store` (404 until the first
scan finishes). Versions are one counter shared by all documents and restart
at 1 when the server restarts, so clients refetch when the snapshot's version
differs from theirs (`!==`, not `>`).

The mod rescans every 10 s and only re-encodes (off the game thread) when
something changed; it then resyncs everyone so the new version reaches them.

### `buildings`
```json
{
  "classes": [
    { "name": "Build_Foundation_8x4_01_C", "display": "Foundation 8m x 4m", "cat": "structure", "box": [-400,-400,400,400] },
    { "name": "Build_ConstructorMk1_C", "display": "Constructor", "cat": "production", "box": [-390,-500,390,500] }
  ],
  "stride": 5,
  "items": [0, 1000, 2000, 0, 90,   1, 1500, 2000, 400, 0]
}
```
`items` is flat, `stride` numbers per piece: `class, x, y, z, yaw`. Sorted by
`z` so drawing in order puts higher floors on top. `box` is the class's
footprint in the piece's local space (cm), rotate it by `yaw` and add `x, y`.
Categories: `production`, `power`, `storage`, `logistics`, `structure`,
`other`. Foundations, walls and other "lightweight" pieces are included.

### `lines`
```json
{ "belts": [[0,0, 1000,0, 1000,800]], "pipes": [], "hypertubes": [], "power": [[0,0, 3000,0]] }
```
Each polyline is flat `[x, y, x, y, ...]` in cm. Rails stay in the snapshot.

## Client -> server

Ignored for now. Planned:
```json
{ "type": "subscribe", "layers": ["players", "trains", "vehicles", "rails"] }
```

## Known limits

- IDs are actor names: stable for a session, not across save loads.
- Rails only change via snapshot (resync).
- Static layers are whole documents: building one foundation refetches all of
  them (about 5 MB for 200k pieces). Fine on localhost or a LAN; per-piece
  deltas would be the next step if that ever matters.
- Footprints are per class: variable-length pieces (beams) all get the same box.
- civetweb holds one worker thread per open websocket, so `WorkerThreads`
  (16 by default) caps concurrent viewers at roughly 14.
