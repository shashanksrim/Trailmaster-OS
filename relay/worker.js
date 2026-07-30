// Traccar → Firebase relay (Cloudflare Worker).
//
// WHY THIS EXISTS
// The convoy web app both reports position and displays the convoy, so it has
// to stay awake to do the first job — and a locked phone stops running it,
// which is exactly what happens on a drive. Nothing on the web platform fixes
// that: service workers cannot reach navigator.geolocation and iOS suspends the
// tab outright.
//
// Traccar Client (free, App Store + Play Store) does background location
// properly on both platforms. This Worker is the only missing piece: it accepts
// what Traccar posts and writes a correctly-shaped member into the SAME
// Firebase path the convoy already uses. The board and the web app need no
// changes at all — the radar cannot tell the difference.
//
// Reporting is now decoupled from viewing: the phone reports from a pocket, and
// the web app becomes a viewer you can open and close freely.
//
// SETUP
//   URL in Traccar Client:  https://<worker>.workers.dev/<ROOM>/<TOKEN>
//   Device identifier:      the driver's callsign, 5 chars max
//
// Traccar appends ?id=<identifier>&lat=..&lon=.. to whatever URL you give it.
//
// REVERTING: delete this Worker and stop using the URL. Nothing else in the
// project depends on it.

const DB = "https://trailmaster-e43b1-default-rtdb.asia-southeast1.firebasedatabase.app";

// Mirrors convoy_ui.h / convoy_wifi.h / the web app, so a car is the same
// colour on the phone map and the device radar.
const HUES = ["#00E5FF", "#00E676", "#FFD54F", "#FF4081", "#B388FF", "#FF8A65"];

// Stable per-callsign colour: the same driver keeps the same colour across
// drives, instead of it depending on who happened to join first.
function colorFor(id) {
  let h = 0;
  for (let i = 0; i < id.length; i++) h = (h * 31 + id.charCodeAt(i)) >>> 0;
  return HUES[h % HUES.length];
}

const num = (v) => {
  const n = typeof v === "string" ? parseFloat(v) : v;
  return Number.isFinite(n) ? n : null;
};

// Pull a field from a nested object by trying several likely paths. Traccar has
// changed its payload shape across versions (query params on the OsmAnd
// protocol, JSON from v9+), and rather than pin one shape and break on an
// update, accept any of them.
function dig(obj, paths) {
  for (const p of paths) {
    let cur = obj;
    for (const k of p.split(".")) {
      if (cur == null || typeof cur !== "object") { cur = undefined; break; }
      cur = cur[k];
    }
    const n = num(cur);
    if (n !== null) return n;
  }
  return null;
}

function extract(url, body) {
  const q = url.searchParams;
  const fromQuery = q.has("lat") || q.has("latitude");

  const id = q.get("id") || q.get("deviceid") ||
             (body && (body.device_id || body.deviceId || body.id)) || null;

  const lat = fromQuery ? num(q.get("lat") ?? q.get("latitude"))
                        : dig(body || {}, ["lat", "latitude", "location.coords.latitude", "coords.latitude"]);
  const lon = fromQuery ? num(q.get("lon") ?? q.get("longitude"))
                        : dig(body || {}, ["lon", "longitude", "location.coords.longitude", "coords.longitude"]);

  let speed = fromQuery ? num(q.get("speed"))
                        : dig(body || {}, ["speed", "location.coords.speed", "coords.speed"]);
  // The OsmAnd query protocol reports speed in KNOTS; the JSON payloads report
  // m/s. The board's "is it moving" threshold is in m/s, so normalise here —
  // getting this wrong makes a parked car look like it is doing 15 km/h.
  if (speed !== null && fromQuery) speed = speed * 0.514444;

  const heading = fromQuery ? num(q.get("bearing") ?? q.get("heading"))
                            : dig(body || {}, ["bearing", "heading", "location.coords.heading", "coords.heading"]);

  // Traccar buffers fixes while offline and sends them later, so prefer the
  // device's own timestamp over arrival time — otherwise a replayed backlog
  // would all look freshly current. Seconds → ms.
  let ts = fromQuery ? num(q.get("timestamp"))
                     : dig(body || {}, ["timestamp", "location.timestamp", "tst"]);
  if (ts !== null && ts < 1e11) ts = ts * 1000;      // seconds, not ms
  if (ts === null || !Number.isFinite(ts)) ts = Date.now();

  return { id, lat, lon, speed, heading, ts };
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // Path carries the room and a shared secret: /<ROOM>/<TOKEN>. Traccar
    // appends its own query string, and some versions add a trailing slash.
    const parts = url.pathname.split("/").filter(Boolean);
    const room = (parts[0] || "").toUpperCase();
    const token = parts[1] || "";

    if (!room || !token) {
      return new Response("usage: /<ROOM>/<TOKEN>?id=CALLSIGN&lat=..&lon=..\n", { status: 400 });
    }
    // Without this the URL would be a world-writable endpoint into the convoy.
    // Set RELAY_TOKEN with:  wrangler secret put RELAY_TOKEN
    if (!env.RELAY_TOKEN || token !== env.RELAY_TOKEN) {
      return new Response("forbidden\n", { status: 403 });
    }

    let body = null;
    if (request.method === "POST" || request.method === "PUT") {
      const text = await request.text();
      try { body = JSON.parse(text); } catch { body = null; }
      // Keep the raw text around for ?debug — the first real request from a new
      // Traccar version is the cheapest way to learn its actual payload shape.
      if (url.searchParams.has("debug")) {
        return new Response(JSON.stringify({ method: request.method, raw: text, parsed: body }, null, 2),
                            { headers: { "content-type": "application/json" } });
      }
    }

    const f = extract(url, body);

    if (!f.id) return new Response("missing device id\n", { status: 400 });
    if (f.lat === null || f.lon === null) {
      // Traccar also sends status-only pings (battery, heartbeat) with no fix.
      // Those are not an error; there is just nothing to place on the radar.
      return new Response("ok (no fix)\n", { status: 200 });
    }

    // The published RTDB rules require callsign + ts and cap callsign at 5.
    const callsign = String(f.id).toUpperCase().replace(/[^A-Z0-9]/g, "").slice(0, 5);
    if (!callsign) return new Response("bad device id\n", { status: 400 });

    const member = {
      name: callsign,
      callsign,
      lat: f.lat,
      lon: f.lon,
      heading: f.heading,
      speed: f.speed,
      ts: f.ts,
      color: colorFor(callsign),
    };

    // PATCH, not PUT: leaves any field we did not send alone.
    const target = `${DB}/convoys/${encodeURIComponent(room)}/members/${encodeURIComponent(callsign)}.json`;
    const res = await fetch(target, {
      method: "PATCH",
      headers: { "content-type": "application/json" },
      body: JSON.stringify(member),
    });

    if (!res.ok) {
      const detail = await res.text();
      // Most likely cause is the rules rejecting the shape — surface it rather
      // than returning a bare 500, because Traccar shows nothing useful.
      return new Response(`firebase ${res.status}: ${detail}\n`, { status: 502 });
    }

    return new Response(url.searchParams.has("debug")
      ? JSON.stringify({ room, member }, null, 2) + "\n"
      : "ok\n", { status: 200 });
  },
};
