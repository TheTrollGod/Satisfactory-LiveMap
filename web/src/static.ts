// Static layers (buildings, belts, pipes, ...) fetched over HTTP and drawn on
// canvas tiles. A late-game save has 100k+ pieces, far too many for one Leaflet
// layer per piece, so each category is one GridLayer backed by a bucket grid.
import L from "leaflet";
import { CATEGORY_STYLE, LINE_STYLE, type BuildingsDoc, type LinesDoc } from "./protocol";

const TILE = 256;
const CELL = 20000; // bucket size in cm (200 m)

type Key = number;
const cellKey = (cx: number, cy: number): Key => (cx + 32768) * 65536 + (cy + 32768);

// cm -> tile pixels at zoom z. CRS.Simple is 1 unit = 1 m with y flipped, and
// toLatLng flips Unreal's y again, so both axes map straight across.
const scaleAt = (z: number) => Math.pow(2, z) / 100;

class Buckets {
  private cells = new Map<Key, number[]>();

  add(i: number, minX: number, minY: number, maxX: number, maxY: number) {
    for (let cx = Math.floor(minX / CELL); cx <= Math.floor(maxX / CELL); cx++) {
      for (let cy = Math.floor(minY / CELL); cy <= Math.floor(maxY / CELL); cy++) {
        const k = cellKey(cx, cy);
        let list = this.cells.get(k);
        if (!list) this.cells.set(k, (list = []));
        list.push(i);
      }
    }
  }

  /** Indices of everything whose box may overlap [minX,maxX]x[minY,maxY], ascending (= draw order). */
  query(minX: number, minY: number, maxX: number, maxY: number): number[] {
    const seen = new Set<number>();
    for (let cx = Math.floor(minX / CELL); cx <= Math.floor(maxX / CELL); cx++) {
      for (let cy = Math.floor(minY / CELL); cy <= Math.floor(maxY / CELL); cy++) {
        const list = this.cells.get(cellKey(cx, cy));
        if (list) for (const i of list) seen.add(i);
      }
    }
    return [...seen].sort((a, b) => a - b);
  }
}

interface TileCoords {
  x: number;
  y: number;
  z: number;
}

/** Rotated footprints of one building category, as world-space corner quads. */
export class FootprintLayer extends L.GridLayer {
  private corners = new Float32Array(0); // 8 per item: x0,y0 .. x3,y3 (cm)
  private classOf = new Uint16Array(0);
  private buckets = new Buckets();
  private names: string[] = [];
  count = 0;

  constructor(private readonly fill: string, private readonly stroke: string, zIndex: number) {
    super({ tileSize: TILE, minZoom: -6, maxZoom: 4, zIndex, updateWhenZooming: false, keepBuffer: 2 });
  }

  setItems(doc: BuildingsDoc, indices: number[]) {
    const stride = doc.stride;
    this.names = doc.classes.map((c) => c.display ?? c.name);
    this.corners = new Float32Array(indices.length * 8);
    this.classOf = new Uint16Array(indices.length);
    this.buckets = new Buckets();
    indices.forEach((item, i) => {
      const o = item * stride;
      const cls = doc.items[o];
      const [x, y, yaw] = [doc.items[o + 1], doc.items[o + 2], doc.items[o + 4]];
      const [bx0, by0, bx1, by1] = doc.classes[cls].box;
      const c = Math.cos((yaw * Math.PI) / 180);
      const s = Math.sin((yaw * Math.PI) / 180);
      const local = [bx0, by0, bx1, by0, bx1, by1, bx0, by1];
      let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
      for (let k = 0; k < 4; k++) {
        const lx = local[k * 2], ly = local[k * 2 + 1];
        const wx = x + lx * c - ly * s;
        const wy = y + lx * s + ly * c;
        this.corners[i * 8 + k * 2] = wx;
        this.corners[i * 8 + k * 2 + 1] = wy;
        minX = Math.min(minX, wx); maxX = Math.max(maxX, wx);
        minY = Math.min(minY, wy); maxY = Math.max(maxY, wy);
      }
      this.classOf[i] = cls;
      this.buckets.add(i, minX, minY, maxX, maxY);
    });
    this.count = indices.length;
    this.redraw();
  }

  protected createTile(coords: TileCoords): HTMLElement {
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = TILE;
    const k = scaleAt(coords.z);
    const ox = coords.x * TILE, oy = coords.y * TILE;
    const ids = this.buckets.query(ox / k, oy / k, (ox + TILE) / k, (oy + TILE) / k);
    if (!ids.length) return canvas;
    const g = canvas.getContext("2d")!;
    g.fillStyle = this.fill;
    g.strokeStyle = this.stroke;
    g.lineWidth = 1;
    const outline = k * 800 > 12; // a foundation is 8 m; outline once it's 12+ px wide
    for (const i of ids) {
      const p = i * 8;
      const x0 = this.corners[p] * k - ox, y0 = this.corners[p + 1] * k - oy;
      const x2 = this.corners[p + 4] * k - ox, y2 = this.corners[p + 5] * k - oy;
      if (Math.abs(x2 - x0) < 2 && Math.abs(y2 - y0) < 2) {
        g.fillRect(Math.floor((x0 + x2) / 2), Math.floor((y0 + y2) / 2), 1, 1);
        continue;
      }
      g.beginPath();
      g.moveTo(x0, y0);
      g.lineTo(this.corners[p + 2] * k - ox, this.corners[p + 3] * k - oy);
      g.lineTo(x2, y2);
      g.lineTo(this.corners[p + 6] * k - ox, this.corners[p + 7] * k - oy);
      g.closePath();
      g.fill();
      if (outline) g.stroke();
    }
    return canvas;
  }

  /** Name of the topmost piece under a world point (cm), if any. */
  hit(x: number, y: number): string | undefined {
    const ids = this.buckets.query(x, y, x, y);
    for (let n = ids.length - 1; n >= 0; n--) {
      const p = ids[n] * 8;
      let inside = true;
      for (let e = 0; e < 4 && inside; e++) {
        const ax = this.corners[p + e * 2], ay = this.corners[p + e * 2 + 1];
        const bx = this.corners[p + ((e + 1) % 4) * 2], by = this.corners[p + ((e + 1) % 4) * 2 + 1];
        // Corners wind the same way for every item, so "inside" is one sign.
        inside = (bx - ax) * (y - ay) - (by - ay) * (x - ax) >= 0;
      }
      if (inside) return this.names[this.classOf[ids[n]]];
    }
    return undefined;
  }
}

/** Polylines of one kind (belts, pipes, ...), flat [x,y,x,y,...] in cm. */
export class PolylineLayer extends L.GridLayer {
  private lines: number[][] = [];
  private buckets = new Buckets();
  count = 0;

  constructor(private readonly color: string, private readonly width: number, zIndex: number) {
    super({ tileSize: TILE, minZoom: -6, maxZoom: 4, zIndex, updateWhenZooming: false, keepBuffer: 2 });
  }

  setLines(lines: number[][]) {
    this.lines = lines;
    this.buckets = new Buckets();
    lines.forEach((l, i) => {
      let minX = Infinity, minY = Infinity, maxX = -Infinity, maxY = -Infinity;
      for (let j = 0; j < l.length; j += 2) {
        minX = Math.min(minX, l[j]); maxX = Math.max(maxX, l[j]);
        minY = Math.min(minY, l[j + 1]); maxY = Math.max(maxY, l[j + 1]);
      }
      this.buckets.add(i, minX, minY, maxX, maxY);
    });
    this.count = lines.length;
    this.redraw();
  }

  protected createTile(coords: TileCoords): HTMLElement {
    const canvas = document.createElement("canvas");
    canvas.width = canvas.height = TILE;
    const k = scaleAt(coords.z);
    const ox = coords.x * TILE, oy = coords.y * TILE;
    const ids = this.buckets.query(ox / k, oy / k, (ox + TILE) / k, (oy + TILE) / k);
    if (!ids.length) return canvas;
    const g = canvas.getContext("2d")!;
    g.strokeStyle = this.color;
    // Real width (belts ~1.5 m) once zoomed in, never thinner than a hairline.
    g.lineWidth = Math.max(1, this.width * k);
    g.lineCap = "round";
    g.lineJoin = "round";
    g.beginPath();
    for (const i of ids) {
      const l = this.lines[i];
      g.moveTo(l[0] * k - ox, l[1] * k - oy);
      for (let j = 2; j < l.length; j += 2) g.lineTo(l[j] * k - ox, l[j + 1] * k - oy);
    }
    g.stroke();
    return canvas;
  }
}

export interface StaticLayers {
  buildings: Record<string, FootprintLayer>;
  lines: Record<string, PolylineLayer>;
  versions: Record<string, number>;
  /** Snapshot told us these versions are current; fetch whatever we don't have. */
  sync(versions: Record<string, number> | undefined): void;
  /** Use a document directly (mock mode). */
  load(name: string, doc: unknown, version: number): void;
  /** Name of the topmost visible building under a world point (cm). */
  hit(map: L.Map, x: number, y: number): string | undefined;
}

export function createStaticLayers(httpBase: string, onLoaded: (name: string) => void): StaticLayers {
  const buildings: Record<string, FootprintLayer> = {};
  Object.entries(CATEGORY_STYLE).forEach(([cat, s], i) => (buildings[cat] = new FootprintLayer(s.fill, s.stroke, 10 + i)));
  const lines: Record<string, PolylineLayer> = {};
  Object.entries(LINE_STYLE).forEach(([kind, s], i) => (lines[kind] = new PolylineLayer(s.color, s.width, 30 + i)));

  const versions: Record<string, number> = {};
  const wanted: Record<string, number> = {};
  const inFlight = new Set<string>();

  async function fetchDoc(name: string) {
    inFlight.add(name);
    try {
      const res = await fetch(`${httpBase}/static/${name}`, { cache: "no-store" });
      if (!res.ok) return;
      const version = Number(res.headers.get("X-LiveMap-Version") ?? 0);
      load(name, await res.json(), version);
    } catch (e) {
      console.warn(`livemap: fetching ${name} failed`, e);
    } finally {
      inFlight.delete(name);
    }
    // A newer version was announced while this one downloaded.
    if (wanted[name] !== undefined && wanted[name] !== versions[name]) setTimeout(() => sync({ [name]: wanted[name] }), 1000);
  }

  function sync(next: Record<string, number> | undefined) {
    for (const [name, v] of Object.entries(next ?? {})) {
      if (name !== "buildings" && name !== "lines") continue; // e.g. "map" is an image, not ours
      wanted[name] = v;
      // !== not >: a restarted server counts from 1 again.
      if (versions[name] !== v && !inFlight.has(name)) void fetchDoc(name);
    }
  }

  function load(name: string, doc: unknown, version: number) {
    if (name === "buildings") applyBuildings(doc as BuildingsDoc);
    else if (name === "lines") applyLines(doc as LinesDoc);
    versions[name] = version;
    onLoaded(name);
  }

  function applyBuildings(doc: BuildingsDoc) {
    const byCat: Record<string, number[]> = {};
    const n = doc.items.length / doc.stride;
    for (let i = 0; i < n; i++) {
      const cat = doc.classes[doc.items[i * doc.stride]]?.cat ?? "other";
      (byCat[cat in buildings ? cat : "other"] ??= []).push(i);
    }
    for (const [cat, layer] of Object.entries(buildings)) layer.setItems(doc, byCat[cat] ?? []);
  }

  function applyLines(doc: LinesDoc) {
    for (const [kind, layer] of Object.entries(lines)) layer.setLines(doc[kind] ?? []);
  }

  return {
    buildings,
    lines,
    versions,
    sync,
    load,
    hit(map, x, y) {
      // Topmost category first, matching zIndex.
      for (const layer of Object.values(buildings).reverse()) {
        if (!map.hasLayer(layer)) continue;
        const name = layer.hit(x, y);
        if (name) return name;
      }
      return undefined;
    },
  };
}
