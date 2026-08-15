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
r = await call("/?id=TM1&lat=1&lon=2");
check(r.status === 400, "missing token rejected");

// --- permanent URL: room comes from assign/<callsign>, not the path ---
// This is what lets a driver paste the URL once and never touch it again; the
// room changes every convoy, so pinning it in the URL is the friction to kill.
let assignLookups = [];
globalThis.fetch = async (url, init) => {
  if (String(url).includes("/assign/")) {
    assignLookups.push(String(url));
    const cs = String(url).split("/assign/")[1].replace(".json", "");
    return new Response(JSON.stringify(cs === "TM1" ? { room: "aenp", ts: 1 } : null), { status: 200 });
  }
  sent = { url, body: JSON.parse(init.body), method: init.method };
  return new Response("{}", { status: 200 });
};

sent = null;
r = await call("/SEKRIT?id=TM1&lat=12.9&lon=77.6&timestamp=1785400000");
check(r.status === 200, "token-only URL accepted");
check(assignLookups.length === 1, "looked up the assignment");
check(sent.url.includes("/convoys/AENP/members/TM1.json"),
      `room from assignment, uppercased (${sent.url})`);

// A phone that is configured but not in any convoy must not error — that is a
// normal state between drives, and an error would surface in the app as a
// permanent upload failure.
sent = null;
r = await call("/SEKRIT?id=NOONE&lat=1&lon=2");
check(r.status === 200, "unassigned callsign is not an error");
check(sent === null, "unassigned callsign writes nothing");

// The old pinned-room URL must keep working so existing phones do not break.
sent = null;
r = await call("/AENP/SEKRIT?id=TM1&lat=1&lon=2");
check(sent.url.includes("/convoys/AENP/members/TM1.json"), "pinned-room URL still works");

globalThis.fetch = async (url, init) => {
  sent = { url, body: JSON.parse(init.body), method: init.method };
  return new Response("{}", { status: 200 });
};

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

// --- WHAT TRACCAR CLIENT ACTUALLY SENDS ---
// Captured verbatim from a real phone via `wrangler tail`. It POSTs the OsmAnd
// fields FORM-ENCODED IN THE BODY — not as query params, not as JSON. That was
// the one combination the first version did not try, so `id` was never found and
// every upload failed with "missing device id" while the data sat in the body.
sent = null;
r = await call("/AENP/SEKRIT", {
  method: "POST",
  headers: { "content-type": "application/x-www-form-urlencoded" },
  body: "id=TM1&lat=12.936833&lon=77.6992084&timestamp=1785430293" +
        "&accuracy=68.52420043945312&altitude=812.8999633789062&batt=47&charge=false",
});
check(r.status === 200, "real traccar form-encoded body accepted");
check(sent.body.callsign === "TM1", "form body device id");
near(sent.body.lat, 12.936833, "form body lat");
near(sent.body.lon, 77.6992084, "form body lon");
check(sent.body.ts === 1785430293000, `form body ts seconds->ms (got ${sent.body.ts})`);
check(sent.body.speed === null, "absent speed stays null, not 0");

// Same body but moving — speed must be treated as knots, like the query protocol.
sent = null;
await call("/AENP/SEKRIT", {
  method: "POST",
  headers: { "content-type": "application/x-www-form-urlencoded" },
  body: "id=TM1&lat=12.9&lon=77.6&timestamp=1785430293&speed=10&bearing=90",
});
near(sent.body.speed, 5.14444, "form body speed knots->m/s");
near(sent.body.heading, 90, "form body bearing");

// --- the REAL Traccar Client v9 payload ---
// Shape confirmed against home-assistant/core#147254. latitude/longitude/heading
// arrive as STRINGS and timestamp as ISO 8601 — parseFloat on that returns 2013,
// which silently becomes a 1970 date and marks the member permanently offline.
// That bug is what this case exists to catch.
sent = null;
r = await call("/AENP/SEKRIT", {
  method: "POST",
  headers: { "content-type": "application/json" },
  body: JSON.stringify({
    device_id: "TM1",
    location: {
      coords: { latitude: "12.9716", longitude: "77.5946",
                accuracy: 10.5, altitude: 102.0, heading: "105.32", speed: 4.2 },
      battery: { level: 0.8, is_charging: false },
      timestamp: "2026-07-30T07:32:51Z",
      is_moving: true,
    },
  }),
});
check(r.status === 200, "traccar v9 payload accepted");
check(sent.body.callsign === "TM1", "v9 device_id -> callsign");
near(sent.body.lat, 12.9716, "v9 lat from string");
near(sent.body.lon, 77.5946, "v9 lon from string");
near(sent.body.heading, 105.32, "v9 heading from string");
near(sent.body.speed, 4.2, "v9 speed stays m/s");
check(sent.body.ts === Date.parse("2026-07-30T07:32:51Z"), `ISO ts parsed (got ${sent.body.ts})`);
check(sent.body.ts > 1e12, "ISO ts is not a 1970 date");

// "yyyy-MM-dd HH:mm:ss" (no T) must parse too
sent = null;
await call("/AENP/SEKRIT", {
  method: "POST", headers: { "content-type": "application/json" },
  body: JSON.stringify({ device_id: "TM2", lat: 1, lon: 2, timestamp: "2026-07-30 07:32:51" }),
});
check(sent.body.ts > 1e12, `space-separated ts parsed (got ${sent.body.ts})`);

// location as a single "lat,lon" string
sent = null;
await call("/AENP/SEKRIT", {
  method: "POST", headers: { "content-type": "application/json" },
  body: JSON.stringify({ id: "TM3", location: "12.5,77.5", timestamp: 1785400000 }),
});
near(sent.body.lat, 12.5, "location string lat");
near(sent.body.lon, 77.5, "location string lon");

// --- OwnTracks: inbound mapping + friends reply ---
// OwnTracks is the app worth pointing people at: it is the only one of the two
// that can be configured from a link, and its HTTP response can carry the rest
// of the convoy back as "friends" for its own map.
globalThis.fetch = async (url, init) => {
  if (init && init.method === "PATCH") { sent = { url, body: JSON.parse(init.body) }; return new Response("{}", { status: 200 }); }
  // the roster read for the friends reply
  const fresh = Date.now();
  return new Response(JSON.stringify({
    TM1: { callsign: "TM1", name: "Shashank", lat: 12.9, lon: 77.6, speed: 10, heading: 90, ts: fresh },
    TM5: { callsign: "TM5", name: "Ravi", lat: 12.8, lon: 77.5, ts: fresh },
    GHOST: { callsign: "GHOST", ts: fresh },                  // joined, no fix yet
    // Pre-rules record: no callsign, so it can never update itself again.
    m_lwynmlxp: { lat: 12.99, lon: 77.71, ts: fresh },
    // Real callsign but three days old — a leftover from an earlier drive.
    OLD: { callsign: "OLD", name: "Stale", lat: 1, lon: 2, ts: fresh - 3 * 86400 * 1000 },
  }), { status: 200 });
};

sent = null;
r = await call("/AENP/SEKRIT", {
  method: "POST",
  headers: { "content-type": "application/json", "X-Limit-U": "TM1" },
  body: JSON.stringify({ _type: "location", lat: 12.95, lon: 77.65, tst: 1785400000,
                         vel: 36, cog: 180, tid: "TM", batt: 80 }),
});
check(r.status === 200, "owntracks location accepted");
check(sent.body.callsign === "TM1", "identity from X-Limit-U, not the 2-char tid");
near(sent.body.speed, 10, "owntracks vel km/h -> m/s");
near(sent.body.heading, 180, "owntracks cog -> heading");
check(sent.body.ts === 1785400000000, `owntracks tst seconds->ms (got ${sent.body.ts})`);

const friends = await r.json();
check(Array.isArray(friends), "friends reply is an array");
const locs = friends.filter((x) => x._type === "location");
const cards = friends.filter((x) => x._type === "card");
check(!locs.some((l) => l.tid === "TM1"), "self excluded from friends");
check(locs.some((l) => l.tid === "TM5"), "other member present as friend");
check(!locs.some((l) => l.tid === "GHOST"), "member with no fix omitted");
check(!locs.some((l) => l.tid === "m_lwynmlxp"), "callsign-less ghost omitted (no raw keys as names)");
check(!locs.some((l) => l.tid === "OLD"), "three-day-old member omitted");
check(cards.some((c) => c.tid === "TM5" && c.name === "Ravi"), "card carries the display name");
const tm5 = locs.find((l) => l.tid === "TM5");
// Seconds, not milliseconds — the fixture is "now", so assert the unit rather
// than a frozen value. A ms value here would put friends 55,000 years out.
check(tm5.tst < 1e11 && Math.abs(tm5.tst - Date.now() / 1000) < 5,
      `friend tst is seconds near now (got ${tm5.tst})`);
check(typeof tm5.topic === "string" && tm5.topic.includes("TM5"), "friend has a topic");

// Identity falls back to the topic's device segment when no header is present.
sent = null;
await call("/AENP/SEKRIT", {
  method: "POST", headers: { "content-type": "application/json" },
  body: JSON.stringify({ _type: "location", lat: 1, lon: 2, tst: 1785400000,
                         topic: "owntracks/shashank/TM9", tid: "TM" }),
});
check(sent.body.callsign === "TM9", `identity from topic tail (got ${sent.body.callsign})`);

// Restore the simple stub for the remaining cases.
globalThis.fetch = async (url, init) => {
  sent = { url, body: JSON.parse(init.body), method: init.method };
  return new Response("{}", { status: 200 });
};

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

// ── Photo store ──────────────────────────────────────────────────────────────
// In-memory stand-in for Workers KV: enough of the surface the Worker uses.
function fakeKV() {
  const m = new Map();
  return {
    m,
    async put(k, v) { m.set(k, v); },
    async get(k)    { return m.has(k) ? m.get(k) : null; },
    async delete(k) { m.delete(k); },
    async list({ prefix }) {
      return { keys: [...m.keys()].filter((k) => k.startsWith(prefix)).map((name) => ({ name })) };
    },
  };
}
const penv = { RELAY_TOKEN: "SEKRIT", PHOTOS: fakeKV() };
const pcall = (path, init) => worker.fetch(new Request("https://r.dev" + path, init), penv);
const frame = new Uint8Array(466 * 466 * 2);
frame[0] = 0xAB;

console.log("=== photo store ===");

// A missing binding must say so rather than 500 — it is the likeliest misconfig.
r = await worker.fetch(new Request("https://r.dev/photos.json?dev=AABB1122"), { RELAY_TOKEN: "SEKRIT" });
check(r.status === 501, "unbound KV reported as 501");

r = await pcall("/photo/AABB1122/a.bin", { method: "PUT", body: frame });
check(r.status === 403, "upload without a token rejected");

// Unencoded "../" never reaches the handler — new URL() normalises it away —
// so the encoded form is the one the name rule actually has to stop.
r = await pcall("/photo/AABB1122/%2E%2E%2Fetc?t=SEKRIT", { method: "PUT", body: frame });
check(r.status === 400, "encoded traversal in the name rejected");
r = await pcall("/photo/AABB1122/notaframe.txt?t=SEKRIT", { method: "PUT", body: frame });
check(r.status === 400, "non-.bin name rejected");

r = await pcall("/photo/AABB1122/tm_1.bin?t=SEKRIT", { method: "PUT", body: frame });
check(r.status === 200, `upload accepted (got ${r.status})`);
check(penv.PHOTOS.m.get("photo:AABB1122:tm_1.bin").byteLength === 434312, "stored 466x466x2 bytes");

r = await pcall("/photo/AABB1122/empty.bin?t=SEKRIT", { method: "PUT", body: new Uint8Array(0) });
check(r.status === 413, "empty upload rejected");

// The manifest is DERIVED from KV, which is what stops it naming another host.
r = await pcall("/photos.json?dev=AABB1122");
const man = await r.json();
check(man.files.length === 1, `manifest lists one file (got ${man.files.length})`);
check(man.files[0].path === "tm_1.bin", "manifest path is the bare name");
check(man.files[0].url === "https://r.dev/photo/AABB1122/tm_1.bin", `manifest url self-hosted (got ${man.files[0].url})`);
check(man.files.every((f) => f.url.startsWith("https://r.dev/")), "no manifest url can point off-origin");

// Shape the firmware's download_file_list() scans for.
const raw = JSON.stringify(man);
check(raw.includes('"files"') && raw.includes('"path":') && raw.includes('"url":'),
      "manifest shape matches download_file_list");

r = await pcall("/photo/AABB1122/tm_1.bin");
check(r.status === 200, "download works without a token");
check((await r.arrayBuffer()).byteLength === 434312, "downloaded bytes round-trip");

r = await pcall("/photo/AABB1122/nope.bin");
check(r.status === 404, "missing image 404s");

r = await pcall("/photo/AABB1122/tm_1.bin?t=SEKRIT", { method: "DELETE" });
check(r.status === 200, "delete accepted");
check((await (await pcall("/photos.json?dev=AABB1122")).json()).files.length === 0, "manifest empties after delete");

// The relay's own routing must still work with the photo routes in front.
sent = null;
globalThis.fetch = async (url, init) => {
  sent = { url, body: JSON.parse(init.body), method: init.method };
  return new Response("{}", { status: 200 });
};
r = await call("/AENP/SEKRIT?id=TM1&lat=1&lon=2");
check(r.status === 200 && sent !== null, "relay route unaffected by photo routes");


// --- two boards on the field must not see each other's images ---
await pcall("/photo/AABB1122/mine.bin?t=SEKRIT", { method: "PUT", body: frame });
await pcall("/photo/CCDD3344/theirs.bin?t=SEKRIT", { method: "PUT", body: frame });
const mineMan  = await (await pcall("/photos.json?dev=AABB1122")).json();
const theirMan = await (await pcall("/photos.json?dev=CCDD3344")).json();
check(mineMan.files.length === 1 && mineMan.files[0].path === "mine.bin", "board A sees only its own image");
check(theirMan.files.length === 1 && theirMan.files[0].path === "theirs.bin", "board B sees only its own image");
check(mineMan.files[0].url.includes("/AABB1122/"), "board A url is scoped to board A");

// An unscoped manifest must NOT fall back to the whole store.
r = await pcall("/photos.json");
check(r.status === 400, `manifest without dev refused (got ${r.status})`);

console.log(`PASS: ${pass}  FAIL: ${fail}`);
process.exit(fail ? 1 : 0);
