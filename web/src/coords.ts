import type { Vec3 } from "./protocol";

// Satisfactory's playable world, in Unreal units (cm). The in-game map texture
// covers exactly this square, so a background image goes on these bounds.
export const WORLD = {
  minX: -324698.17,
  maxX: 425301.83,
  minY: -375000,
  maxY: 375000,
} as const;

// Leaflet CRS.Simple, 1 map unit = 1 m. Unreal Y grows south, Leaflet lat grows north.
export function toLatLng([x, y]: Vec3 | [number, number]): [number, number] {
  return [-y / 100, x / 100];
}

export const WORLD_BOUNDS: [[number, number], [number, number]] = [
  toLatLng([WORLD.minX, WORLD.maxY]),
  toLatLng([WORLD.maxX, WORLD.minY]),
];
