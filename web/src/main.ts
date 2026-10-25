import L from "leaflet";
import "leaflet/dist/leaflet.css";
import { WORLD_BOUNDS, toLatLng } from "./coords";
import {
  CATEGORY_STYLE,
  DYNAMIC_LAYERS,
  LINE_STYLE,
  type BuildingsDoc,
  type DynamicLayer,
  type Entity,
  type Rail,
  type ServerMessage,
} from "./protocol";
import { createStaticLayers } from "./static";

const params = new URLSearchParams(location.search);
const WS_URL = params.get("ws") ?? import.meta.env.VITE_LIVEMAP_WS ?? "ws://localhost:8086/ws";
const USE_MOCK = params.has("mock");
// Static layers come over plain HTTP from the same server: ws://host:port/ws -> http://host:port
const HTTP_BASE = (() => {
  const u = new URL(WS_URL, location.href);
  return `${u.protocol === "wss:" ? "https:" : "http:"}//${u.host}`;
})();

const map = L.map("map", { crs: L.CRS.Simple, minZoom: -4, maxZoom: 2, zoomSnap: 0.25 });
map.fitBounds(WORLD_BOUNDS);
L.rectangle(WORLD_BOUNDS, { color: "#555", weight: 1, fill: false, interactive: false }).addTo(map);
// Terrain: the mod renders the game's own map texture and serves it at
// /static/map, so no Coffee Stain art ships with this repo. Own pane under the
// building tiles (tilePane is z-index 200).
map.createPane("terrain").style.zIndex = "150";
let terrain: L.ImageOverlay | undefined;
let terrainVersion = 0;
let terrainBlobUrl: string | undefined;
// The image is several MB, so fetch it ourselves to show progress while it
// downloads; the rest of the map stays usable meanwhile.
const loading = document.getElementById("loading")!;
const loadingText = loading.querySelector("span")!;
const loadingBar = loading.querySelector<HTMLElement>(".bar > div")!;
async function syncTerrain(version: number | undefined) {
  if (!version || version === terrainVersion) return;
  terrainVersion = version;
  loading.hidden = false;
  loadingText.textContent = "Loading map…";
  loadingBar.style.width = "0";
  try {
    const res = await fetch(`${HTTP_BASE}/static/map?v=${version}`, { cache: "no-store" });
    if (!res.ok || !res.body) throw new Error(`HTTP ${res.status}`);
    const total = Number(res.headers.get("Content-Length")) || 0;
    const reader = res.body.getReader();
    const chunks: Uint8Array[] = [];
    let received = 0;
    for (;;) {
      const { done, value } = await reader.read();
      if (done) break;
      chunks.push(value);
      received += value.length;
      if (total) {
        loadingText.textContent = `Loading map… ${(received / 1048576).toFixed(1)} / ${(total / 1048576).toFixed(1)} MB`;
        loadingBar.style.width = `${(100 * received) / total}%`;
      }
    }
    if (version !== terrainVersion) return; // a newer map arrived meanwhile
    const url = URL.createObjectURL(new Blob(chunks, { type: res.headers.get("Content-Type") ?? "image/jpeg" }));
    if (terrain) {
      terrain.setUrl(url);
    } else {
      terrain = L.imageOverlay(url, WORLD_BOUNDS, { pane: "terrain", interactive: false }).addTo(map);
      layerControl.addOverlay(terrain, "Terrain");
    }
    if (terrainBlobUrl) URL.revokeObjectURL(terrainBlobUrl);
    terrainBlobUrl = url;
    loadingText.textContent = "Drawing map…";
    loadingBar.style.width = "100%";
    terrain.once("load error", () => {
      if (version === terrainVersion) loading.hidden = true;
    });
  } catch (err) {
    if (version !== terrainVersion) return;
    console.warn("terrain load failed", err);
    terrainVersion = 0; // retry on the next snapshot
    loading.hidden = true;
  }
}

const status = document.getElementById("status")!;

const layers = {
  rails: L.layerGroup().addTo(map),
  players: L.layerGroup().addTo(map),
  trains: L.layerGroup().addTo(map),
  vehicles: L.layerGroup().addTo(map),
};
const statics = createStaticLayers(HTTP_BASE, () => updateStatus());
const overlays: Record<string, L.Layer> = {};
for (const [cat, layer] of Object.entries(statics.buildings)) {
  layer.addTo(map);
  overlays[CATEGORY_STYLE[cat].label] = layer;
}
for (const [kind, layer] of Object.entries(statics.lines)) {
  layer.addTo(map);
  overlays[LINE_STYLE[kind].label] = layer;
}
Object.assign(overlays, { Rails: layers.rails, Players: layers.players, Trains: layers.trains, Vehicles: layers.vehicles });
const layerControl = L.control.layers(undefined, overlays, { collapsed: false }).addTo(map);

map.on("click", (ev: L.LeafletMouseEvent) => {
  // Inverse of toLatLng: lat = -y/100, lng = x/100.
  const name = statics.hit(map, ev.latlng.lng * 100, -ev.latlng.lat * 100);
  if (name) L.popup().setLatLng(ev.latlng).setContent(name).openOn(map);
});

let helloText = "connecting…";
function updateStatus() {
  const pieces = Object.values(statics.buildings).reduce((n, l) => n + l.count, 0);
  status.textContent = pieces ? `${helloText} · ${pieces.toLocaleString()} buildings` : helloText;
}

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
      helloText = `connected (v${msg.version}, ${msg.tickHz} Hz)`;
      updateStatus();
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
      statics.sync(msg.static);
      syncTerrain(msg.static?.map);
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
    helloText = "disconnected, retrying…";
    updateStatus();
    setTimeout(connect, 2000);
  };
}

// Mock feed (?mock) for working on the frontend without the game or the sim.
function startMock() {
  helloText = "mock data";
  statics.load("buildings", mockBuildings(), 1);
  statics.load("lines", { belts: [[0, 0, 40000, 0, 40000, 30000]], pipes: [[0, 2000, 40000, 2000]] }, 2);
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

function mockBuildings(): BuildingsDoc {
  const classes: BuildingsDoc["classes"] = [
    { name: "Build_Foundation_8x4_01_C", display: "Foundation", cat: "structure", box: [-400, -400, 400, 400] },
    { name: "Build_ConstructorMk1_C", display: "Constructor", cat: "production", box: [-400, -500, 400, 500] },
  ];
  const items: number[] = [];
  for (let i = 0; i < 40; i++) for (let j = 0; j < 30; j++) items.push(0, i * 800, -j * 800, 0, 0);
  for (let i = 0; i < 10; i++) items.push(1, 2000 + i * 1200, -4000, 400, 30 * i);
  return { classes, stride: 5, items };
}

// Read-only hook for the end-to-end tests.
(window as unknown as { __livemap: unknown }).__livemap = {
  lastSeq: () => lastSeq,
  ids: (layer: DynamicLayer) => [...markers[layer].keys()].sort(),
  pos: (layer: DynamicLayer, id: string) => markers[layer].get(id)?.getLatLng(),
  rails: () => layers.rails.getLayers().length,
  buildings: () => Object.fromEntries(Object.entries(statics.buildings).map(([k, l]) => [k, l.count])),
  lines: () => Object.fromEntries(Object.entries(statics.lines).map(([k, l]) => [k, l.count])),
  staticVersions: () => ({ ...statics.versions }),
  terrain: () => (terrain ? { url: terrain.getElement()?.src, loaded: terrain.getElement()?.complete } : null),
  hit: (x: number, y: number) => statics.hit(map, x, y),
  setView: (x: number, y: number, z: number) => map.setView(toLatLng([x, y]), z, { animate: false }),
  // Resolves once every visible static layer has drawn its tiles.
  tilesLoaded: () =>
    Promise.all(
      [...Object.values(statics.buildings), ...Object.values(statics.lines)].map(
        (l) => new Promise<void>((done) => ((l as unknown as { _loading?: boolean })._loading ? l.once("load", () => done()) : done())),
      ),
    ),
};

if (USE_MOCK) startMock();
else connect();
