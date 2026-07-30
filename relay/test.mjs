import worker from "./worker.js";

let pass = 0, fail = 0, sent = null;
const check = (c, m) => c ? pass++ : (fail++, console.log("  FAIL:", m));
const near = (a, b, m) => check(Math.abs(a - b) < 1e-4, `${m} (got ${a}, want ${b})`);

// Stub the outbound Firebase write so tests touch nothing real.
globalThis.fetch = async (url, init) => {
  sent = { url, body: JSON.parse(init.body), method: init.method };
  return new Response("{}", { status: 200 });
};

const env = { RELAY_TOKEN: "SEKRIT" };
const call = (path, init) => worker.fetch(new Request("https://r.dev" + path, init), env);

console.log("=== relay worker ===");

// --- auth ---
let r = await call("/AENP/WRONG?id=TM1&lat=1&lon=2");
check(r.status === 403, "bad token rejected");
r = await call("/AENP?id=TM1&lat=1&lon=2");
check(r.status === 400, "missing token rejected");

// --- OsmAnd query protocol (what Traccar Client sends by default) ---
sent = null;
r = await call("/aenp/SEKRIT?id=TM1&lat=12.9716&lon=77.5946&timestamp=1785400000&speed=10&bearing=90");
check(r.status === 200, "osmand GET accepted");
check(sent.method === "PATCH", "uses PATCH not PUT");
check(sent.url.includes("/convoys/AENP/members/TM1.json"), `room uppercased in path (${sent.url})`);
near(sent.body.lat, 12.9716, "lat");
near(sent.body.lon, 77.5946, "lon");
near(sent.body.speed, 5.14444, "speed knots->m/s");
near(sent.body.heading, 90, "heading");
check(sent.body.ts === 1785400000000, `ts seconds->ms (got ${sent.body.ts})`);
check(sent.body.callsign === "TM1", "callsign");
check(/^#[0-9A-F]{6}$/.test(sent.body.color), `colour looks valid (${sent.body.color})`);

// Colour must be stable for the same callsign across calls.
const c1 = sent.body.color;
await call("/AENP/SEKRIT?id=TM1&lat=1&lon=2");
check(sent.body.color === c1, "colour stable per callsign");

// --- trailing slash (some Traccar versions) ---
sent = null;
r = await call("/AENP/SEKRIT/?id=TM2&lat=1&lon=2");
check(r.status === 200 && sent.url.includes("members/TM2.json"), "trailing slash tolerated");

// --- status ping with no fix must not 500 ---
r = await call("/AENP/SEKRIT?id=TM1&batt=80");
check(r.status === 200, "no-fix ping is ok, not an error");

// --- callsign sanitising (rules cap at 5 chars) ---
sent = null;
await call("/AENP/SEKRIT?id=my-long-phone&lat=1&lon=2");
check(sent.body.callsign.length <= 5, `callsign capped at 5 (got '${sent.body.callsign}')`);
check(/^[A-Z0-9]+$/.test(sent.body.callsign), "callsign alphanumeric only");

// --- JSON POST (Traccar v9+) ---
sent = null;
r = await call("/AENP/SEKRIT", {
  method: "POST",
  headers: { "content-type": "application/json" },
  body: JSON.stringify({
    device_id: "TM3",
    location: { coords: { latitude: 1.5, longitude: 2.5, speed: 4, heading: 33 },
                timestamp: "1785400000" },
  }),
});
check(r.status === 200, "json POST accepted");
near(sent.body.lat, 1.5, "json lat");
near(sent.body.lon, 2.5, "json lon");
near(sent.body.speed, 4, "json speed stays m/s (no knot conversion)");
near(sent.body.heading, 33, "json heading");

// --- ms timestamps must not be multiplied again ---
sent = null;
await call("/AENP/SEKRIT?id=TM1&lat=1&lon=2&timestamp=1785400000000");
check(sent.body.ts === 1785400000000, `ms ts passed through (got ${sent.body.ts})`);

// --- missing timestamp falls back to now ---
sent = null;
await call("/AENP/SEKRIT?id=TM1&lat=1&lon=2");
check(Math.abs(sent.body.ts - Date.now()) < 5000, "missing ts falls back to now");

// --- firebase rejection surfaces, not a bare 500 ---
globalThis.fetch = async () => new Response("permission denied", { status: 401 });
r = await call("/AENP/SEKRIT?id=TM1&lat=1&lon=2");
check(r.status === 502, "firebase error surfaced as 502");
check((await r.text()).includes("permission denied"), "firebase detail included");

console.log(`PASS: ${pass}  FAIL: ${fail}`);
process.exit(fail ? 1 : 0);
