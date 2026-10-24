// Mirrors docs/PROTOCOL.md. Positions are Unreal units (cm).
export type Vec3 = [number, number, number];

export interface Entity {
  id: string;
  name?: string;
  kind?: string;
  pos: Vec3;
  yaw?: number;
}

export interface Rail {
  id: string;
  points: Vec3[];
}

export type ServerMessage =
  | { type: "hello"; version: number; tickHz: number }
  | { type: "snapshot"; players: Entity[]; trains: Entity[]; vehicles: Entity[]; rails: Rail[] }
  | { type: "delta"; players?: Entity[]; trains?: Entity[]; vehicles?: Entity[] }
  | { type: "remove"; layer: string; ids: string[] };
