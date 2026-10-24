import L from "leaflet";
import "leaflet/dist/leaflet.css";
import type { Entity, Rail, ServerMessage, Vec3 } from "./protocol";

const WS_URL = import.meta.env.VITE_LIVEMAP_WS ?? "ws://localhost:8086/ws";

// Unreal X/Y (cm) -> Leaflet CRS.Simple [lat, lng]. Unreal Y grows south.
// TODO(M0): calibrate scale/offset against the map background image bounds.
const UNITS_PER_PIXEL = 100;
const toLatLng = ([x, y]: Vec3): L.LatLngExpression => [-y / UNITS_PER_PIXEL, x / UNITS_PER_PIXEL];

const map = L.map("map", { crs: L.CRS.Simple, minZoom: -5, maxZoom: 3 }).setView([0, 0], -3);
const status = document.getElementById("status")!;

const layers = {
  rails: L.layerGroup().addTo(map),
  players: L.layerGroup().addTo(map),
  trains: L.layerGroup().addTo(map),
  vehicles: L.layerGroup().addTo(map),
};
L.control.layers(undefined, layers).addTo(map);

const colors = { players: "#4fc3f7", trains: "#ffb74d", vehicles: "#aed581" } as const;
type DynamicLayer = keyof typeof colors;
const markers = new Map<string, L.CircleMarker>();

function upsert(layer: DynamicLayer, items: Entity[] = []) {
  for (const e of items) {
    const key = `${layer}:${e.id}`;
    const existing = markers.get(key);
    if (existing) {
      existing.setLatLng(toLatLng(e.pos));
      continue;
    }
    const m = L.circleMarker(toLatLng(e.pos), { radius: 6, color: colors[layer], fillOpacity: 0.9 })
      .bindTooltip(e.name ?? e.id);
    m.addTo(layers[layer]);
    markers.set(key, m);
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
      drawRails(msg.rails);
      upsert("players", msg.players);
      upsert("trains", msg.trains);
      upsert("vehicles", msg.vehicles);
      break;
    case "delta":
      upsert("players", msg.players);
      upsert("trains", msg.trains);
      upsert("vehicles", msg.vehicles);
      break;
    case "remove":
      for (const id of msg.ids) {
        const key = `${msg.layer}:${id}`;
        markers.get(key)?.remove();
        markers.delete(key);
      }
      break;
  }
}

function connect() {
  const ws = new WebSocket(WS_URL);
  ws.onmessage = (ev) => handle(JSON.parse(ev.data) as ServerMessage);
  ws.onclose = () => {
    status.textContent = "disconnected, using mock data";
    startMock();
    setTimeout(connect, 5000);
  };
}

// Mock feed so the frontend is usable before the mod exists.
let mockTimer: number | undefined;
function startMock() {
  if (mockTimer) return;
  drawRails([{ id: "r1", points: [[-50000, 0, 0], [0, 20000, 0], [50000, 0, 0]] }]);
  let t = 0;
  mockTimer = window.setInterval(() => {
    t += 0.05;
    handle({
      type: "delta",
      players: [{ id: "p1", name: "Pioneer", pos: [Math.cos(t) * 20000, Math.sin(t) * 20000, 0] }],
      trains: [{ id: "t1", name: "Iron Express", pos: [Math.sin(t) * 50000, 10000, 0] }],
    });
  }, 200);
}

connect();
