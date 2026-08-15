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

// Keep in sync with convoy_cfg.h, the app's CALLSIGN_MAX and the database
// rules. Truncating to a different length here would write a member the room
// rules then reject, or an assign key the app never looks under.
const CALLSIGN_MAX = 7;

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
// OwnTracks posts {_type:"location", lat, lon, tst, tid, vel, cog} and names the
// user in the X-Limit-U / X-Limit-D headers. It is the app worth pointing people
// at because — unlike Traccar Client — it can be configured from a single link
// (owntracks:///config?inline=...), and because its HTTP mode lets the RESPONSE
// carry the rest of the convoy back as "friends".
function isOwnTracks(b) { return !!b && b._type === "location"; }

function extract(url, body, form, headers) {
  const q = url.searchParams;
  const get = (k) => q.get(k) ?? (form ? form.get(k) : null);

  if (isOwnTracks(body)) {
    const b = body;
    // Identity, best first: the configured username, then the topic's device
    // segment, then the 2-char tracker id. tid alone collides easily in a
    // convoy (TM1/TM4 both start "TM"), so it is the last resort.
    const topicTail = typeof b.topic === "string" ? b.topic.split("/").filter(Boolean).pop() : null;
    const id = (headers && (headers.get("x-limit-u") || headers.get("X-Limit-U"))) ||
               topicTail || b.tid || null;
    return {
      id,
      lat: num(b.lat), lon: num(b.lon),
      // OwnTracks reports vel in KM/H; the board's threshold is m/s.
      speed: num(b.vel) === null ? null : num(b.vel) / 3.6,
      heading: num(b.cog),
      ts: parseTs(b.tst) ?? Date.now(),
    };
  }
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

// Build the OwnTracks "friends" reply: the rest of the convoy, as the app
// expects to receive it.
//
// In HTTP mode the app never polls — the only chance to hand it anything is the
// response to its own position POST. An array of _type location + card objects
// is rendered as friends on its map, which means OwnTracks can show the convoy
// without our web app being open at all.
//
// Documented as supported on both iOS and Android; iOS is worth verifying on a
// real device before relying on it.
// Anything older than this is a ghost, not a convoy member. The room accumulates
// records from earlier sessions — and entries predating the database rules have
// no callsign at all, so they can never update themselves and would otherwise
// haunt the friends list forever at a three-day-old position.
const FRIEND_STALE_MS = 30 * 60 * 1000;

function buildFriends(members, selfCallsign, now = Date.now()) {
  const out = [];
  for (const [, m] of Object.entries(members || {})) {
    if (!m || m.lat == null || m.lon == null) continue;             // no fix, nothing to place
    const cs = m.callsign;
    if (!cs) continue;                        // pre-rules ghost; the key is not a name
    if (m.ts && now - m.ts > FRIEND_STALE_MS) continue;
    if (cs.toUpperCase() === String(selfCallsign).toUpperCase()) continue;   // not ourselves

    const topic = `owntracks/convoy/${cs}`;
    out.push({
      _type: "location",
      tid: cs,
      topic,
      lat: m.lat,
      lon: m.lon,
      tst: Math.round((m.ts || Date.now()) / 1000),                 // OwnTracks wants seconds
      vel: m.speed == null ? undefined : Math.round(m.speed * 3.6), // back to km/h
      cog: m.heading == null ? undefined : Math.round(m.heading),
    });
    // The card carries the human-readable name; without it the app shows only
    // the tracker id.
    out.push({ _type: "card", tid: cs, topic, name: m.name || cs });
  }
  return out;
}

// ── Photo store ───────────────────────────────────────────────────────────────
// Board images live here rather than in Firebase Storage, for two reasons.
//
// Storage is a Google Cloud Storage bucket wearing a Firebase badge, and since
// late 2024 provisioning one needs the Blaze plan — a card on file to host a few
// hundred KB of dashboard wallpaper. Workers KV is free at this size (1 GB, and
// a frame is 424 KB) on an account this project already uses.
//
// The second reason matters more. `ota_sync_photos()` downloads whatever URL it
// finds in the manifest, so a world-writable manifest is a way to make the board
// fetch an arbitrary host. Here the manifest is DERIVED from what is actually in
// KV — every url is one this Worker serves — so no write can point the board
// somewhere else. That property comes from the shape, not from the token: the
// token lives in config.js, which is public, and is a speed bump, not a secret.
// Keys are photo:<dev>:<name>, and the <dev> segment is what makes this usable
// by more than one Trailmaster. Several boards can be on the field at once; each
// asks for /photos.json?dev=<its own id> and sees only its own images. Without
// the scope every board would download every image anyone ever uploaded.
//
// <dev> is the board's eFuse-MAC id — the same string it publishes as
// devices/<id>, so the app already knows it from pairing.
const PHOTO_PREFIX = "photo:";
const DEV_RE  = /^[A-Fa-f0-9]{1,16}$/;
const NAME_RE = /^[A-Za-z0-9_-]{1,60}\.bin$/;

async function photoRoutes(request, env, url, parts) {
  if (!env.PHOTOS) {
    return new Response("PHOTOS KV namespace is not bound to this Worker\n", { status: 501 });
  }

  // GET /photos.json?dev=<id> — the manifest, in the exact shape
  // download_file_list() parses: {"files":[{"path":..,"url":..}]}.
  if (parts[0] === "photos.json") {
    const dev = url.searchParams.get("dev") || "";
    if (!DEV_RE.test(dev)) {
      // Deliberately not "everything": an unscoped request used to mean the
      // whole store, which is how one board ends up with another's wallpaper.
      return new Response(JSON.stringify({ files: [], error: "dev required" }), {
        status: 400, headers: { "content-type": "application/json" },
      });
    }
    const listed = await env.PHOTOS.list({ prefix: `${PHOTO_PREFIX}${dev}:` });
    const files = listed.keys.map((k) => {
      const path = k.name.split(":").pop();
      return { path, url: `${url.origin}/photo/${dev}/${encodeURIComponent(path)}` };
    });
    return new Response(JSON.stringify({ files }), {
      headers: { "content-type": "application/json", "cache-control": "no-store" },
    });
  }

  if (parts[0] !== "photo" || !parts[2]) return null;      // not ours
  // Names are generated by the app as tm_<base36>.bin. Pinning the shape keeps
  // the KV keyspace to actual frames, and rejects an encoded "%2E%2E%2F.." — the
  // unencoded form never arrives, since new URL() normalises it away first.
  const dev  = decodeURIComponent(parts[1]);
  const name = decodeURIComponent(parts[2]);
  if (!DEV_RE.test(dev) || !NAME_RE.test(name)) {
    return new Response("bad name\n", { status: 400 });
  }
  const key = `${PHOTO_PREFIX}${dev}:${name}`;

  if (request.method === "GET") {
    const body = await env.PHOTOS.get(key, "arrayBuffer");
    if (!body) return new Response("not found\n", { status: 404 });
    return new Response(body, {
      headers: {
        "content-type": "application/octet-stream",
        // Frames are immutable — the name carries a timestamp — so let the
        // board and any CDN hop cache them.
        "cache-control": "public, max-age=31536000, immutable",
      },
    });
  }

  if (request.method === "PUT" || request.method === "DELETE") {
    if (!env.RELAY_TOKEN || url.searchParams.get("t") !== env.RELAY_TOKEN) {
      return new Response("bad token\n", { status: 403 });
    }
    if (request.method === "DELETE") {
      await env.PHOTOS.delete(key);
      return new Response("ok\n", { headers: cors() });
    }
    const buf = await request.arrayBuffer();
    // 466*466*2 = 434312. Anything far off that is not a frame this board can
    // display, and KV values are capped at 25 MiB regardless.
    if (buf.byteLength === 0 || buf.byteLength > 2 * 1024 * 1024) {
      return new Response("bad size\n", { status: 413, headers: cors() });
    }
    await env.PHOTOS.put(key, buf);
    return new Response(JSON.stringify({ path: name, url: `${url.origin}/photo/${dev}/${encodeURIComponent(name)}` }),
                        { headers: { "content-type": "application/json", ...cors() } });
  }

  return new Response("method not allowed\n", { status: 405 });
}

// The web app is served from github.io and uploads straight from the browser.
const cors = () => ({
  "access-control-allow-origin": "*",
  "access-control-allow-methods": "GET,PUT,DELETE,OPTIONS",
  "access-control-allow-headers": "content-type",
});

export default {
  async fetch(request, env) {
    const url = new URL(request.url);

    // Photo store first — it owns /photo/* and /photos.json, which would
    // otherwise be read as a room code by the relay routing below.
    {
      const seg = url.pathname.split("/").filter(Boolean);
      if (request.method === "OPTIONS") return new Response(null, { headers: cors() });
      if (seg[0] === "photo" || seg[0] === "photos.json") {
        const res = await photoRoutes(request, env, url, seg);
        if (res) return res;
      }
    }

    // Two URL forms, and the shorter one is the point:
    //
    //   /<TOKEN>            room resolved from assign/<callsign>   ← permanent
    //   /<ROOM>/<TOKEN>     room pinned in the URL                 ← legacy
    //
    // Room codes change with every convoy. With the room in the URL, every
    // driver has to re-paste a long URL each time — which is precisely the
    // friction worth removing. Looking the room up per callsign instead means
    // the phone is configured once, ever, and changing convoys happens in the
    // web app, which people open anyway.
    const parts = url.pathname.split("/").filter(Boolean);
    let pinnedRoom = "", token = "";
    if (parts.length >= 2)      { pinnedRoom = parts[0].toUpperCase(); token = parts[1]; }
    else if (parts.length === 1) { token = parts[0]; }

    if (!token) {
      return new Response("usage: /<TOKEN>?id=CALLSIGN&lat=..&lon=..\n", { status: 400 });
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

    const f = extract(url, body, form, request.headers);

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
    const callsign = String(f.id).toUpperCase().replace(/[^A-Z0-9]/g, "").slice(0, CALLSIGN_MAX);
    if (!callsign) return new Response("bad device id\n", { status: 400 });

    // Resolve the room: pinned in the URL, else whatever the web app last
    // assigned this callsign when it joined a convoy.
    let room = pinnedRoom;
    if (!room) {
      try {
        const a = await fetch(`${DB}/assign/${encodeURIComponent(callsign)}.json`);
        if (a.ok) {
          const v = await a.json();
          room = (typeof v === "string" ? v : v && v.room) || "";
          room = String(room).toUpperCase();
        }
      } catch (e) { console.log("assign lookup failed:", e.message); }
    }
    if (!room) {
      // Configured phone, but not currently in a convoy. Not an error — the app
      // should keep reporting quietly, and will land somewhere the moment the
      // user joins a room. Failing here would make the app show upload errors
      // for a state that is entirely normal between drives.
      console.log("no room assigned for", callsign);
      return isOwnTracks(body)
        ? new Response("[]", { status: 200, headers: { "content-type": "application/json" } })
        : new Response("ok (no convoy assigned)\n", { status: 200 });
    }

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

    // OwnTracks: reply with the rest of the convoy so the app can draw it on its
    // own map. It never polls in HTTP mode, so this response is the only chance
    // to hand it anything. Traccar ignores the body, so this costs it nothing —
    // but skip the extra Firebase read for clients that cannot use it.
    if (isOwnTracks(body)) {
      let friends = [];
      try {
        const r = await fetch(`${DB}/convoys/${encodeURIComponent(room)}/members.json`);
        if (r.ok) friends = buildFriends(await r.json(), callsign);
      } catch (e) {
        // A failed roster read must not fail the position report — the write
        // already succeeded, and the app will ask again in a few seconds.
        console.log("friends read failed:", e.message);
      }
      return new Response(JSON.stringify(friends), {
        status: 200, headers: { "content-type": "application/json" },
      });
    }

    return new Response(url.searchParams.has("debug")
      ? JSON.stringify({ room, member }, null, 2) + "\n"
      : "ok\n", { status: 200 });
  },
};
