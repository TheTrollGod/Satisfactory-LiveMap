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
  | ({ type: "snapshot"; seq: number; rails: Rail[] } & Record<DynamicLayer, Entity[]>)
  | ({ type: "delta"; seq: number; removed?: Partial<Record<DynamicLayer, string[]>> } & Partial<
      Record<DynamicLayer, Entity[]>
    >);
