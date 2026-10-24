# WebSocket protocol (draft)

Endpoint: `ws://<host>:<port>/ws`. JSON text frames. Coordinates are Unreal
world units (cm), `[x, y, z]`. The frontend converts to map space.

## Server -> client

### `hello`
```json
{ "type": "hello", "version": 1, "tickHz": 5 }
```

### `snapshot` (once, on connect)
```json
{
  "type": "snapshot",
  "players":   [{ "id": "p1", "name": "Anthony", "pos": [0,0,0], "yaw": 90 }],
  "trains":    [{ "id": "t1", "name": "Iron Express", "pos": [0,0,0], "yaw": 0, "speed": 0 }],
  "vehicles":  [{ "id": "v1", "kind": "truck", "pos": [0,0,0], "yaw": 0 }],
  "buildings": [{ "id": "b1", "class": "Build_SmelterMk1_C", "pos": [0,0,0], "yaw": 0, "size": [1000, 900] }],
  "rails":     [{ "id": "r1", "points": [[0,0,0],[100,0,0]] }]
}
```

### `delta` (every dynamic tick, only what changed)
```json
{ "type": "delta", "players": [{ "id": "p1", "pos": [10,0,0], "yaw": 95 }], "trains": [], "vehicles": [] }
```

### `upsert` / `remove` (static layer changes)
```json
{ "type": "upsert", "layer": "buildings", "items": [ ... ] }
{ "type": "remove", "layer": "rails", "ids": ["r1"] }
```

## Client -> server

```json
{ "type": "subscribe", "layers": ["players", "trains", "vehicles", "rails"] }
```

Layers not subscribed are never sent. Default is everything.

## Open

- IDs: use a stable per-actor id (actor name / save GUID), not pointers.
- Binary encoding (MessagePack) if JSON gets too fat on large saves.
