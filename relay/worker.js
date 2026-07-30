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

// Timestamps arrive in three different shapes depending on protocol and
// version: epoch seconds, epoch milliseconds, or an ISO 8601 string. parseFloat
// on "2013-09-17T07:32:51Z" quietly returns 2013 — which becomes a 1970 date and
// marks the member permanently offline — so this has to be handled explicitly.
function parseTs(v) {
  if (v == null) return null;
  if (typeof v === "number" && Number.isFinite(v)) return v < 1e11 ? v * 1000 : v;
  const s = String(v).trim();
  if (!s) return null;
  if (/^\d+$/.test(s)) { const n = Number(s); return n < 1e11 ? n * 1000 : n; }
  // ISO 8601, or Traccar's "yyyy-MM-dd HH:mm:ss" which needs the T to parse.
  let d = Date.parse(s);
  if (!Number.isFinite(d)) d = Date.parse(s.replace(" ", "T") + (/[Zz+]/.test(s) ? "" : "Z"));
  return Number.isFinite(d) ? d : null;
}

// `form` carries a form-encoded POST body. Traccar Client actually sends the
// OsmAnd fields that way — not as query parameters and not as JSON, which is the
// one combination the first version of this Worker did not try:
//   id=TM1&lat=12.93&lon=77.69&timestamp=1785430293&accuracy=68.5&batt=47
// Same field names and same units as the query protocol, so they are handled as
// one path rather than duplicated.
function extract(url, body, form) {
  const q = url.searchParams;
  const get = (k) => q.get(k) ?? (form ? form.get(k) : null);
  // "OsmAnd style" means these flat field names, whether they arrived in the
  // query string or the body — as opposed to the nested JSON payload.
  const osmand = get("lat") !== null || get("latitude") !== null;
  const b = body || {};

  const id = get("id") || get("deviceid") ||
             b.device_id || b.deviceId || b.deviceid || b.id || null;

  let lat = osmand ? num(get("lat") ?? get("latitude"))
                   : dig(b, ["lat", "latitude", "location.coords.latitude", "coords.latitude"]);
  let lon = osmand ? num(get("lon") ?? get("longitude"))
                   : dig(b, ["lon", "longitude", "location.coords.longitude", "coords.longitude"]);

  // Some versions send location as a single "lat,lon" string rather than a pair
  // of numbers.
  if (lat === null || lon === null) {
    const loc = get("location") ?? (typeof b.location === "string" ? b.location : null);
    if (loc && loc.includes(",")) {
      const [a, c] = loc.split(",");
      if (lat === null) lat = num(a);
      if (lon === null) lon = num(c);
    }
  }

  let speed = osmand ? num(get("speed"))
                     : dig(b, ["speed", "location.coords.speed", "coords.speed"]);
  // The OsmAnd fields report speed in KNOTS; the nested JSON payload reports
  // m/s. The board's "is it moving" threshold is in m/s, so normalise here —
  // getting this wrong makes a parked car look like it is doing 15 km/h.
  if (speed !== null && osmand) speed = speed * 0.514444;

  const heading = osmand ? num(get("bearing") ?? get("heading"))
                         : dig(b, ["bearing", "heading", "location.coords.heading", "coords.heading"]);

  // Traccar buffers fixes while offline and replays them later, so prefer the
  // device's own timestamp over arrival time — otherwise a whole backlog would
  // look freshly current.
  const rawTs = osmand
    ? get("timestamp")
    : (b.timestamp ?? b?.location?.timestamp ?? b.tst ?? null);
  const ts = parseTs(rawTs) ?? Date.now();

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

    let body = null, form = null, raw = "";
    if (request.method === "POST" || request.method === "PUT") {
      raw = await request.text();
      try { body = JSON.parse(raw); } catch { body = null; }
      // Not JSON? Traccar Client sends form-encoded OsmAnd fields in the body.
      if (body === null && raw.includes("=")) form = new URLSearchParams(raw);
      // ?debug echoes the raw payload — the cheapest way to learn a new Traccar
      // version's shape without guessing.
      if (url.searchParams.has("debug")) {
        return new Response(JSON.stringify({ method: request.method, raw, parsed: body }, null, 2),
                            { headers: { "content-type": "application/json" } });
      }
    }

    const f = extract(url, body, form);

    // Traccar's client reports only "upload failed" with no detail, so a payload
    // we cannot read is invisible from both ends. Log the raw body whenever
    // anything is missing — `wrangler tail` then shows exactly what arrived and
    // which field we failed to find, instead of leaving it to guesswork.
    if (!f.id || f.lat === null || f.lon === null) {
      console.log("INCOMPLETE", JSON.stringify({
        method: request.method,
        got: { id: f.id, lat: f.lat, lon: f.lon },
        parsedJson: body !== null,
        raw: raw.slice(0, 800),
      }));
    }

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
