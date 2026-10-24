# WebSocket protocol (v1)

Endpoint: `ws://<host>:<port>/ws` (default port 8086). JSON text frames.
Positions are Unreal world units (cm) rounded to integers, `[x, y, z]`; yaw is
whole degrees. The frontend converts to map space (`web/src/coords.ts`).

`GET /health` returns `{"ok":true,"clients":N,"seq":S}`.

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
  "rails":    [{ "id": "Build_RailroadTrack_C_7", "points": [[0,0,0],[1000,0,0]] }]
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

## Client -> server

Ignored for now. Planned:
```json
{ "type": "subscribe", "layers": ["players", "trains", "vehicles", "rails"] }
```

## Known limits

- IDs are actor names: stable for a session, not across save loads.
- Rails only change via snapshot (resync). Fine for tracks; buildings (M3) will
  need real `upsert`/`remove` messages since there are far more of them.
- civetweb holds one worker thread per open websocket, so `WorkerThreads`
  (16 by default) caps concurrent viewers at roughly 14.
