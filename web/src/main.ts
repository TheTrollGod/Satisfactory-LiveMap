import L from "leaflet";
import "leaflet/dist/leaflet.css";
import { WORLD_BOUNDS, toLatLng } from "./coords";
import { DYNAMIC_LAYERS, type DynamicLayer, type Entity, type Rail, type ServerMessage } from "./protocol";

const params = new URLSearchParams(location.search);
const WS_URL = params.get("ws") ?? import.meta.env.VITE_LIVEMAP_WS ?? "ws://localhost:8086/ws";
const USE_MOCK = params.has("mock");

const map = L.map("map", { crs: L.CRS.Simple, minZoom: -4, maxZoom: 2, zoomSnap: 0.25 });
map.fitBounds(WORLD_BOUNDS);
L.rectangle(WORLD_BOUNDS, { color: "#555", weight: 1, fill: false, interactive: false }).addTo(map);
// TODO: L.imageOverlay("tiles/map.png", WORLD_BOUNDS).addTo(map) once we decide how to ship the map image.

const status = document.getElementById("status")!;

const layers = {
  rails: L.layerGroup().addTo(map),
  players: L.layerGroup().addTo(map),
  trains: L.layerGroup().addTo(map),
  vehicles: L.layerGroup().addTo(map),
};
L.control.layers(undefined, layers).addTo(map);

const colors: Record<DynamicLayer, string> = { players: "#4fc3f7", trains: "#ffb74d", vehicles: "#aed581" };
const markers: Record<DynamicLayer, Map<string, L.CircleMarker>> = {
  players: new Map(),
  trains: new Map(),
  vehicles: new Map(),
};
let lastSeq = 0;

function upsert(layer: DynamicLayer, items: Entity[] = []) {
  for (const e of items) {
    const existing = markers[layer].get(e.id);
    if (existing) {
      existing.setLatLng(toLatLng(e.pos));
      existing.setTooltipContent(e.name ?? e.id);
      continue;
    }
    const m = L.circleMarker(toLatLng(e.pos), { radius: 6, color: colors[layer], fillOpacity: 0.9 }).bindTooltip(
      e.name ?? e.id,
    );
    m.addTo(layers[layer]);
    markers[layer].set(e.id, m);
  }
}

function remove(layer: DynamicLayer, ids: string[] = []) {
  for (const id of ids) {
    markers[layer].get(id)?.remove();
    markers[layer].delete(id);
  }
}

function drawRails(rails: Rail[]) {
  layers.rails.clearLayers();
  for (const r of rails) {
    L.polyline(r.points.map(toLatLng), { color: "#9e9e9e", weight: 2 }).addTo(layers.rails);
  }
}

function handle(msg: ServerMessage) {
  switch (msg.type) {
    case "hello":
      status.textContent = `connected (v${msg.version}, ${msg.tickHz} Hz)`;
      break;
    case "snapshot":
      // A snapshot is the full state (first connect or a resync after we fell
      // behind), so anything not in it is gone.
      for (const layer of DYNAMIC_LAYERS) {
        const keep = new Set(msg[layer].map((e) => e.id));
        remove(layer, [...markers[layer].keys()].filter((id) => !keep.has(id)));
        upsert(layer, msg[layer]);
      }
      drawRails(msg.rails);
      lastSeq = msg.seq;
      break;
    case "delta":
      if (msg.seq <= lastSeq) break; // stale, already in our snapshot
      for (const layer of DYNAMIC_LAYERS) {
        upsert(layer, msg[layer]);
        remove(layer, msg.removed?.[layer]);
      }
      lastSeq = msg.seq;
      break;
  }
}

function connect() {
  const ws = new WebSocket(WS_URL);
  ws.onmessage = (ev) => handle(JSON.parse(ev.data) as ServerMessage);
  ws.onclose = () => {
    status.textContent = "disconnected, retrying…";
    setTimeout(connect, 2000);
  };
}

// Mock feed (?mock) for working on the frontend without the game or the sim.
function startMock() {
  status.textContent = "mock data";
  handle({ type: "snapshot", seq: 0, players: [], trains: [], vehicles: [], rails: [
    { id: "r1", points: [[-50000, 0, 0], [0, 20000, 0], [50000, 0, 0]] },
  ] });
  let seq = 0;
  setInterval(() => {
    const t = ++seq * 0.05;
    handle({
      type: "delta",
      seq,
      players: [{ id: "p1", name: "Pioneer", pos: [Math.cos(t) * 20000, Math.sin(t) * 20000, 0] }],
      trains: [{ id: "t1", name: "Iron Express", pos: [Math.sin(t) * 50000, 10000, 0] }],
    });
  }, 200);
}

// Read-only hook for the end-to-end tests.
(window as unknown as { __livemap: unknown }).__livemap = {
  lastSeq: () => lastSeq,
  ids: (layer: DynamicLayer) => [...markers[layer].keys()].sort(),
  pos: (layer: DynamicLayer, id: string) => markers[layer].get(id)?.getLatLng(),
  rails: () => layers.rails.getLayers().length,
};

if (USE_MOCK) startMock();
else connect();
