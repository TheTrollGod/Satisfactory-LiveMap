// Mirrors docs/PROTOCOL.md. Positions are Unreal units (cm), rounded to integers.
export type Vec3 = [number, number, number];

export interface Entity {
  id: string;
  name?: string;
  kind?: string;
  pos: Vec3;
  yaw?: number;
  speed?: number;
}

export interface Rail {
  id: string;
  points: Vec3[];
}

export const DYNAMIC_LAYERS = ["players", "trains", "vehicles"] as const;
export type DynamicLayer = (typeof DYNAMIC_LAYERS)[number];

export type ServerMessage =
  | { type: "hello"; version: number; tickHz: number }
  | ({ type: "snapshot"; seq: number; rails: Rail[]; static?: Record<string, number> } & Record<DynamicLayer, Entity[]>)
  | ({ type: "delta"; seq: number; removed?: Partial<Record<DynamicLayer, string[]>> } & Partial<
      Record<DynamicLayer, Entity[]>
    >);

// GET /static/buildings. items is flat: [class, x, y, z, yaw] per piece, sorted by z.
export interface BuildingClass {
  name: string;
  display?: string;
  cat: string;
  /** Footprint in the piece's local space, cm: [minX, minY, maxX, maxY]. */
  box: [number, number, number, number];
}
export interface BuildingsDoc {
  classes: BuildingClass[];
  stride: number;
  items: number[];
}

// GET /static/lines. Each polyline is flat [x, y, x, y, ...] in cm.
export type LinesDoc = Record<string, number[][]>;

// Draw order is bottom to top. Unknown categories fall into "other".
export const CATEGORY_STYLE: Record<string, { label: string; fill: string; stroke: string }> = {
  structure: { label: "Foundations & walls", fill: "#5a606b", stroke: "#3c4048" },
  other: { label: "Other buildings", fill: "#8d8f96", stroke: "#5e6066" },
  storage: { label: "Storage", fill: "#c46a8f", stroke: "#7a3f58" },
  logistics: { label: "Splitters, lifts & stations", fill: "#e07b39", stroke: "#8a4a20" },
  power: { label: "Power", fill: "#e3c84b", stroke: "#8f7d2b" },
  production: { label: "Production", fill: "#4fa3e0", stroke: "#2b5f85" },
};

export const LINE_STYLE: Record<string, { label: string; color: string; width: number }> = {
  pipes: { label: "Pipes", color: "#5ec4b6", width: 120 },
  belts: { label: "Belts", color: "#f0a04b", width: 150 },
  hypertubes: { label: "Hypertubes", color: "#b48ae0", width: 120 },
  power: { label: "Power lines", color: "#f4e27a", width: 30 },
};
