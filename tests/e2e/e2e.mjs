// End-to-end: C++ sim server -> WebSocket -> Leaflet frontend in headless Chromium.
//   node tests/e2e/e2e.mjs <path-to-livemap_sim> <frontend-url> [screenshot.png]
// Needs `playwright` resolvable (preinstalled in CI images, or npm i -D playwright).
import { spawn } from "node:child_process";
import { createRequire } from "node:module";
// require() so NODE_PATH works for a globally installed playwright.
const { chromium } = createRequire(import.meta.url)("playwright");

const [simPath, url, shot] = process.argv.slice(2);
const PORT = 18186;
let failures = 0;
const check = (cond, msg) => {
  console.log(`${cond ? "ok  " : "FAIL"} ${msg}`);
  if (!cond) failures++;
};
const sleep = (ms) => new Promise((r) => setTimeout(r, ms));

function startSim() {
  const p = spawn(simPath, [String(PORT)], { stdio: ["ignore", "pipe", "inherit"] });
  return new Promise((resolve, reject) => {
    p.stdout.on("data", (d) => String(d).includes("listening") && resolve(p));
    p.on("exit", (code) => reject(new Error(`sim exited ${code}`)));
  });
}

let sim = await startSim();
const browser = await chromium.launch();
const page = await browser.newPage({ viewport: { width: 1200, height: 800 } });
const errors = [];
page.on("pageerror", (e) => errors.push(String(e)));
page.on("console", (m) => m.type() === "error" && errors.push(m.text()));

await page.goto(`${url}?ws=ws://127.0.0.1:${PORT}/ws`);
await page.waitForFunction(() => document.getElementById("status").textContent.startsWith("connected"), null, { timeout: 10000 });
check(true, "frontend connected and got hello");

const api = (fn, ...args) => page.evaluate(([f, a]) => window.__livemap[f](...a), [fn, args]);

await page.waitForFunction(() => window.__livemap.ids("trains").length > 0, null, { timeout: 5000 });
check((await api("ids", "trains")).join() === "train-1", "train marker present");
check((await api("ids", "vehicles")).join() === "truck-1", "truck marker present");
check((await api("ids", "players")).includes("player-1"), "player marker present");
check((await api("rails")) === 1, "rail polyline drawn from snapshot");

const p0 = await api("pos", "trains", "train-1");
const s0 = await api("lastSeq");
await sleep(1500);
const p1 = await api("pos", "trains", "train-1");
const s1 = await api("lastSeq");
check(p0.lat !== p1.lat || p0.lng !== p1.lng, `train moved (${p0.lat.toFixed(1)},${p0.lng.toFixed(1)} -> ${p1.lat.toFixed(1)},${p1.lng.toFixed(1)})`);
check(s1 >= s0 + 5, `deltas flowing at ~5 Hz (seq ${s0} -> ${s1} in 1.5 s)`);

// Coordinate sanity: map centre X=50301.83 -> lng 503.0; the sim's train orbits it at R=150000 cm.
const r = Math.hypot(p1.lat, p1.lng - 503.0183);
check(Math.abs(r - 1500) < 2, `train is on its 1500 m orbit in map space (r=${r.toFixed(1)})`);

// player-2 exists for t in [0,10) s of every 20 s, so it must be added and removed.
let sawP2 = false, sawNoP2 = false;
for (let i = 0; i < 30 && !(sawP2 && sawNoP2); i++) {
  const ids = await api("ids", "players");
  if (ids.includes("player-2")) sawP2 = true; else if (sawP2) sawNoP2 = true;
  await sleep(500);
}
check(sawP2 && sawNoP2, "removed entity disappears from the map");

if (shot) await page.screenshot({ path: shot });

const seqBeforeKill = await api("lastSeq");
// Kill the server, bring it back: client must reconnect and not keep ghosts.
sim.kill("SIGKILL");
await page.waitForFunction(() => document.getElementById("status").textContent.startsWith("disconnected"), null, { timeout: 5000 });
check(true, "frontend notices the server going away");
sim = await startSim();
await page.waitForFunction(() => document.getElementById("status").textContent.startsWith("connected"), null, { timeout: 10000 });
await sleep(600);
const players = await api("ids", "players");
check(players.join() === "player-1,player-2", `reconnected, state rebuilt from new snapshot (${players.join()})`);
const seqAfter = await api("lastSeq");
check(seqAfter < seqBeforeKill, `seq reset after server restart was accepted via snapshot (${seqBeforeKill} -> ${seqAfter})`);

check(errors.length === 0, `no page errors${errors.length ? ": " + errors.join(" | ") : ""}`);
await browser.close();
sim.kill();
console.log(failures ? `FAIL (${failures})` : "OK");
process.exit(failures ? 1 : 0);
