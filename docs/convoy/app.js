// Trailmaster Convoy — Phase 0 web app.
//
// Phones join a "convoy room" by code, share GPS through Firebase Realtime DB,
// and see each other live on a map. No accounts: the room code is the shared
// secret, the member id lives in localStorage. This same data model feeds the
// Trailmaster board in Phase 1 (Web Bluetooth relay) — see CONVOY_NETWORK_PLAN.md.

import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import {
  getDatabase, ref, set, remove, update, onValue, get,
  onChildAdded, onChildChanged, onChildRemoved,
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
import {
  getStorage, ref as sref, uploadBytes, getDownloadURL,
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-storage.js";
import { firebaseConfig, PUSH_INTERVAL_MS, ONLINE_WINDOW_MS, STALE_DROP_MS,
         RELAY_URL, RELAY_TOKEN } from "./config.js";

// ── Colors: mirror convoy_ui.h so the phone map and the board radar agree ─────
const HUES = ["#00E5FF", "#00E676", "#FFD54F", "#FF4081", "#B388FF", "#FF8A65"];
const SELF_COLOR = "#FF6A00";                    // Trailmaster orange = "me"

// Phase 1 — BLE relay to the Trailmaster board (peripheral). Keep these UUIDs in
// sync with convoy_net.h on the firmware side.
const CONVOY_NET_SVC = "54524149-4d53-5452-0001-000000000001";
const CONVOY_NET_CHR = "54524149-4d53-5452-0001-000000000002";

const $ = (id) => document.getElementById(id);

// ── Identity (persisted) ──────────────────────────────────────────────────────
function loadIdentity() {
  let id = localStorage.getItem("cvy_member_id");
  if (!id) {
    id = "m_" + Math.random().toString(36).slice(2, 10);
    localStorage.setItem("cvy_member_id", id);
  }
  return {
    id,
    name:     localStorage.getItem("cvy_name") || "",
    callsign: localStorage.getItem("cvy_callsign") || "",
    code:     localStorage.getItem("cvy_code") || "",
  };
}
function colorFor(id) {
  let h = 0;
  for (const c of id) h = (h * 31 + c.charCodeAt(0)) >>> 0;
  return HUES[h % HUES.length];
}
// Convoy codes are case-insensitive, ignore spaces, and tolerate a leading
// "TM-" (older generated codes carried it) — so NA4V, na4v, "NA 4V" and
// TM-NA4V all resolve to the same room. Prevents "we typed the same code but
// see different rooms" splits.
function normalizeCode(raw) {
  return (raw || "").trim().toUpperCase().replace(/\s+/g, "").replace(/^TM-/, "");
}

// ── Geo math (haversine + bearing), same formulas as convoy_ui.h ──────────────
const d2r = (d) => (d * Math.PI) / 180;
function distM(a, b) {
  const R = 6371000;
  const dLa = d2r(b.lat - a.lat), dLo = d2r(b.lon - a.lon);
  const x = Math.sin(dLa / 2) ** 2 +
            Math.cos(d2r(a.lat)) * Math.cos(d2r(b.lat)) * Math.sin(dLo / 2) ** 2;
  return 2 * R * Math.atan2(Math.sqrt(x), Math.sqrt(1 - x));
}
function fmtDist(m) {
  if (!isFinite(m)) return "—";
  return m < 1000 ? `${Math.round(m)} m` : `${(m / 1000).toFixed(1)} km`;
}
const compass = (b) =>
  ["N","NE","E","SE","S","SW","W","NW"][Math.floor(((b + 22.5) % 360) / 45) & 7];

// ── App state ─────────────────────────────────────────────────────────────────
let db, me, roomRef, myRef;
let serverOffset = 0;                 // Firebase clock-skew correction
let watchId = null, lastPush = 0, lastFix = null;
let unsub = null, demoTimer = null;
let map, markers = {}, members = {}, fitted = false;

// Alternate radar view (port of the Trailmaster convoy_ui.h scope) + sheet state
let view = "map", sheetExpanded = false, lastRowCount = 0;
let radarCv = null, radarCtx = null, radarW = 0, radarH = 0;
let radarRAF = null, radarLastDraw = 0, radarNorthUp = false;
let radarHdg = 0, radarHdgValid = false;
let boardDev = null, boardChar = null, boardTimer = null;   // BLE relay to board

const now = () => Date.now() + serverOffset;
const isConfigured = () => firebaseConfig.apiKey && firebaseConfig.apiKey !== "PASTE_ME";

// ── Boot ──────────────────────────────────────────────────────────────────────
function boot() {
  me = loadIdentity();
  $("f-name").value = me.name;
  $("f-callsign").value = me.callsign;
  $("f-code").value = me.code;

  // Demo mode: `?demo` (or the demo buttons) shows the map with mock moving
  // cars — no Firebase, no GPS needed. Lets you see it work before any setup.
  $("btn-demo").addEventListener("click", startDemo);
  $("setup-demo").addEventListener("click", (e) => { e.preventDefault(); startDemo(); });

  // Map-screen controls (need no Firebase, so wire them for demo + live alike).
  $("btn-leave").addEventListener("click", leave);
  $("btn-recenter").addEventListener("click", () => fitAll(true));
  $("btn-view").addEventListener("click", toggleView);
  $("hdg-btn").addEventListener("click", () => { radarNorthUp = !radarNorthUp; });
  $("sheet-grab").addEventListener("click", toggleSheet);
  $("sheet-head").addEventListener("click", toggleSheet);
  window.addEventListener("resize", () => { if (view === "radar") sizeRadar(); });
  // Connect a board. This lists boards over FIREBASE, not over the local network
  // or Bluetooth, which is what makes it work on iOS: the board announces itself
  // once it is on the hotspot, and picking it writes this convoy's room into its
  // node. Bluetooth is offered inside the picker where it exists, as the legacy
  // relay path.
  $("btn-link").addEventListener("click", openBoardPicker);
  $("boards-close").addEventListener("click", () => $("boards").classList.add("hidden"));
  $("track-close").addEventListener("click", () => $("track").classList.add("hidden"));
  $("tr-manual").addEventListener("click", (e) => {
    e.preventDefault();
    $("tr-manual-box").classList.toggle("hidden");
  });

  document.querySelectorAll("#tabs .tab")
          .forEach((b) => b.addEventListener("click", () => showTab(b.dataset.tab)));
  wireWifiTab();
  wireImageTab();
  // #wifi / #img open a tab directly, so the QR on the board could point at a
  // specific one later, and so a link in a message lands where it means to.
  const want = location.hash.replace("#", "");
  if (["wifi", "img"].includes(want)) setTimeout(() => showTab(want), 0);

  if (new URLSearchParams(location.search).has("demo")) { startDemo(); return; }

  if (!isConfigured()) { $("setup").classList.remove("hidden"); return; }

  const appFb = initializeApp(firebaseConfig);
  db = getDatabase(appFb);
  onValue(ref(db, ".info/serverTimeOffset"), (s) => { serverOffset = s.val() || 0; });

  $("btn-join").addEventListener("click", () => doJoin($("f-code").value));
  $("btn-create").addEventListener("click", () => {
    const code = Math.random().toString(36).slice(2, 6).toUpperCase();   // e.g. NA4V
    $("f-code").value = code;
    doJoin(code);
  });
  // Rejoin automatically if we were in a room last time.
  if (me.code) $("f-code").value = me.code;
}

// ── Join / leave ──────────────────────────────────────────────────────────────
function doJoin(rawCode) {
  const name = $("f-name").value.trim();
  const callsign = $("f-callsign").value.trim().toUpperCase().slice(0, 5);
  const code = normalizeCode(rawCode);
  $("f-err").textContent = "";

  if (!name)     return err("Enter your name.");
  if (!callsign) return err("Enter a short callsign (≤5 chars).");
  if (!code)     return err("Enter or create a convoy code.");
  if (!("geolocation" in navigator)) return err("This device has no geolocation.");

  me = { ...me, name, callsign, code };
  localStorage.setItem("cvy_name", name);
  localStorage.setItem("cvy_callsign", callsign);
  localStorage.setItem("cvy_code", code);

  roomRef = ref(db, `convoys/${code}/members`);
  myRef   = ref(db, `convoys/${code}/members/${me.id}`);

  // Point any background tracker at this convoy. Doing it here — rather than
  // baking the room into the phone's URL — is what keeps that URL permanent.
  publishAssignment();

  // Write an initial record so we appear even before the first GPS fix.
  set(myRef, {
    name, callsign, color: colorFor(me.id),
    lat: null, lon: null, heading: null, speed: null, ts: now(),
  });
  // No onDisconnect-remove: a phone that locks or dips out of signal should fade
  // (via stale ts), not vanish. The explicit Leave button removes you cleanly.

  startGeo();
  subscribe();
  showMap(code);
}

// ── Demo mode ─────────────────────────────────────────────────────────────────
// Mock convoy that drifts around a centre, so the map is demonstrable with zero
// setup. Uses the real device GPS for "you" when granted, otherwise a default.
function startDemo() {
  me = { ...me, name: me.name || "You", callsign: me.callsign || "YOU", code: "DEMO" };
  const c = { lat: 12.9716, lon: 77.5946 };   // default centre (Bengaluru)
  const seed = [
    { id: me.id, name: me.name, callsign: me.callsign, off: [0, 0] },
    { id: "d_lead",  name: "Lead car", callsign: "LEAD",  off: [ 0.004,  0.003] },
    { id: "d_sweep", name: "Sweep",    callsign: "SWEEP", off: [-0.006, -0.002] },
    { id: "d_rohan", name: "Rohan",    callsign: "RO",    off: [ 0.002, -0.005] },
  ];
  members = {};
  for (const s of seed)
    members[s.id] = { name: s.name, callsign: s.callsign,
      lat: c.lat + s.off[0], lon: c.lon + s.off[1], heading: null, speed: 6, ts: now() };

  showMap("DEMO");
  render();

  // Center "you" on real GPS if we can get it (no permission = stay on default).
  if (navigator.geolocation)
    navigator.geolocation.getCurrentPosition((p) => {
      if (members[me.id]) { members[me.id].lat = p.coords.latitude; members[me.id].lon = p.coords.longitude; }
      fitted = false; render();
    }, () => {}, { maximumAge: 10000, timeout: 8000 });

  clearInterval(demoTimer);
  demoTimer = setInterval(() => {
    for (const id in members) {
      if (id === me.id) continue;                 // others wander; you stay put
      members[id].lat += (Math.random() - 0.5) * 0.0008;
      members[id].lon += (Math.random() - 0.5) * 0.0008;
      members[id].ts = now();
    }
    render();
  }, 2000);
}

function leave() {
  if (demoTimer) { clearInterval(demoTimer); demoTimer = null; }
  if (watchId != null) { navigator.geolocation.clearWatch(watchId); watchId = null; }
  releaseWakeLock();          // let the screen sleep again once we are out
  if (unsub) { unsub(); unsub = null; }
  if (myRef) { remove(myRef); }
  for (const k in markers) { map.removeLayer(markers[k]); }
  markers = {}; members = {}; fitted = false;
  if (boardTimer) { clearInterval(boardTimer); boardTimer = null; }
  if (boardDev && boardDev.gatt && boardDev.gatt.connected) boardDev.gatt.disconnect();
  boardChar = null; setBoardBtn(false);
  stopRadar();
  if (view !== "map") switchView("map");
  sheetExpanded = false;
  $("sheet").classList.remove("expanded");
  $("fabs").style.display = "flex";
  $("app").classList.add("hidden");
  $("join").classList.remove("hidden");
}

// ── Screen wake lock ─────────────────────────────────────────────────────────
// A backgrounded or locked phone stops running this page, which stops
// watchPosition, which is exactly what happens on a drive. There is no way to
// keep a web page feeding GPS with the screen off -- service workers cannot
// reach navigator.geolocation, and iOS suspends the tab outright -- so the one
// thing we CAN do is stop the screen turning itself off while a convoy is
// running. The phone is dash-mounted and charging anyway.
//
// The lock is released by the browser whenever the page is hidden, and is NOT
// restored automatically, so it has to be re-acquired on visibilitychange.
// Secure-context only; works on Chrome/Android and Safari 16.4+.
let wakeLock = null, wantWakeLock = false;

async function acquireWakeLock() {
  if (!wantWakeLock || !("wakeLock" in navigator) || wakeLock) return;
  try {
    wakeLock = await navigator.wakeLock.request("screen");
    wakeLock.addEventListener("release", () => { wakeLock = null; });
  } catch (e) {
    // Denied (low battery, or no permission). Not fatal -- the convoy still
    // works while the screen happens to be on.
    console.warn("wake lock:", e.message || e);
  }
}

function releaseWakeLock() {
  wantWakeLock = false;
  if (wakeLock) { wakeLock.release().catch(() => {}); wakeLock = null; }
}

document.addEventListener("visibilitychange", () => {
  if (document.visibilityState === "visible") acquireWakeLock();
});

// ── GPS → Firebase (throttled) ────────────────────────────────────────────────
function startGeo() {
  wantWakeLock = true;
  acquireWakeLock();
  watchId = navigator.geolocation.watchPosition(
    (pos) => {
      const c = pos.coords;
      lastFix = {
        lat: c.latitude, lon: c.longitude,
        heading: Number.isFinite(c.heading) ? c.heading : null,
        speed:   Number.isFinite(c.speed) ? c.speed : null,
      };
      const t = Date.now();
      if (t - lastPush >= PUSH_INTERVAL_MS) { lastPush = t; pushFix(); }
    },
    (e) => err("GPS: " + e.message),
    { enableHighAccuracy: true, maximumAge: 2000, timeout: 15000 }
  );
  // Heartbeat: refresh ts even when parked so we stay "online" without a new fix.
  setInterval(() => { if (myRef && lastFix) pushFix(); }, PUSH_INTERVAL_MS);
}
function pushFix() {
  if (!myRef || !lastFix) return;
  update(myRef, { ...lastFix, ts: now() });
}

// ── Subscribe to the room ─────────────────────────────────────────────────────
// Per-CHILD listeners, not onValue on the whole members node.
//
// onValue re-sends the entire roster to every listener on every position update.
// With N cars each pushing every PUSH_INTERVAL_MS that is O(N^3) egress: fine at
// six cars (~117 MB per four-hour drive), but ~4.3 GB at twenty — which burns the
// 10 GB/month free tier in two drives. Child listeners send only the member that
// actually changed, making it O(N^2) and roughly twenty times cheaper at that
// size. Same rendering, same behaviour; only the wire traffic differs.
function subscribe() {
  // onValue replaced the roster wholesale on every fire; child listeners only
  // ever add to it, so start clean or a previous room's cars could linger.
  members = {};
  const upsert = (s) => { members[s.key] = s.val(); render(); };
  const offs = [
    onChildAdded(roomRef, upsert),
    onChildChanged(roomRef, upsert),
    onChildRemoved(roomRef, (s) => { delete members[s.key]; render(); }),
  ];
  unsub = () => offs.forEach((off) => off());
}

// ── Map + roster render ───────────────────────────────────────────────────────
function ensureMap() {
  if (map) return;
  map = L.map("map", { zoomControl: false, attributionControl: false }).setView([20, 0], 2);
  L.tileLayer("https://{s}.tile.openstreetmap.org/{z}/{x}/{y}.png", {
    maxZoom: 19,
  }).addTo(map);
  L.control.zoom({ position: "bottomright" }).addTo(map);
}
function markerIcon(color, callsign, isSelf) {
  const ring = isSelf ? `box-shadow:0 0 0 3px rgba(255,106,0,.35);` : "";
  return L.divIcon({
    className: "cv-marker",
    html: `<div class="cv-dot" style="background:${color};${ring}"></div>
           <div class="cv-tag" style="color:${color}">${callsign || "?"}</div>`,
    iconSize: [0, 0], iconAnchor: [0, 0],
  });
}
function render() {
  const t = now();
  const rows = [];
  const self = members[me.id];
  const selfPt = self && self.lat != null ? { lat: self.lat, lon: self.lon } : null;

  const live = {};
  for (const [id, m] of Object.entries(members)) {
    const isSelf = id === me.id;
    const age = t - (m.ts || 0);
    if (!isSelf && age > STALE_DROP_MS) continue;   // long-gone: drop from view
    const online = age < ONLINE_WINDOW_MS;
    const color = isSelf ? SELF_COLOR : colorFor(id);
    live[id] = true;

    if (m.lat != null && m.lon != null) {
      const ll = [m.lat, m.lon];
      if (markers[id]) {
        markers[id].setLatLng(ll).setIcon(markerIcon(color, m.callsign, isSelf));
      } else {
        markers[id] = L.marker(ll, { icon: markerIcon(color, m.callsign, isSelf) }).addTo(map);
      }
      markers[id].setOpacity(online ? 1 : 0.35);
    }

    const d = selfPt && !isSelf && m.lat != null
      ? distM(selfPt, { lat: m.lat, lon: m.lon }) : NaN;
    const brg = selfPt && !isSelf && m.lat != null
      ? bearing(selfPt, { lat: m.lat, lon: m.lon }) : null;
    rows.push({ id, m, color, isSelf, online, d, brg });
  }

  // Drop markers for members who left.
  for (const id in markers) if (!live[id]) { map.removeLayer(markers[id]); delete markers[id]; }

  // Roster: self first, then nearest → farthest.
  rows.sort((a, b) =>
    a.isSelf ? -1 : b.isSelf ? 1 : (a.d || 1e18) - (b.d || 1e18));
  const onlineCount = rows.filter((r) => r.online).length;
  $("count").textContent = `${onlineCount} car${onlineCount === 1 ? "" : "s"}`;

  $("roster").innerHTML = rows.map((r) => {
    const sub = r.isSelf ? "you"
      : r.m.lat == null ? "no GPS yet"
      : `${fmtDist(r.d)}${r.brg != null ? " · " + compass(r.brg) : ""}`;
    return `<div class="rrow${r.online ? "" : " off"}">
      <span class="rdot" style="background:${r.color}"></span>
      <span class="rcall" style="color:${r.color}">${esc(r.m.callsign || "?")}</span>
      <span class="rname">${esc(r.m.name || "")}</span>
      <span class="rsub">${sub}</span></div>`;
  }).join("");

  lastRowCount = rows.length;
  updateSheetMore();
  if (!fitted) fitAll(false);
}
function bearing(a, b) {
  const y = Math.sin(d2r(b.lon - a.lon)) * Math.cos(d2r(b.lat));
  const x = Math.cos(d2r(a.lat)) * Math.sin(d2r(b.lat)) -
            Math.sin(d2r(a.lat)) * Math.cos(d2r(b.lat)) * Math.cos(d2r(b.lon - a.lon));
  const br = (Math.atan2(y, x) * 180) / Math.PI;
  return (br + 360) % 360;
}
function fitAll(force) {
  const pts = Object.values(markers).map((mk) => mk.getLatLng());
  if (!pts.length) return;
  if (pts.length === 1) { map.setView(pts[0], Math.max(map.getZoom(), 15)); }
  else { map.fitBounds(L.latLngBounds(pts).pad(0.25)); }
  if (!force) fitted = true;
}

// ── Radar view — canvas port of convoy_ui.h (ME centre, cars by bearing/dist) ─
const STEEL = "#1E3A4C";
function niceScale(m) {
  const s = [200, 500, 1000, 2000, 5000, 10000, 20000];
  for (const v of s) if (m <= v) return v;
  return 50000;
}
function sizeRadar() {
  if (!radarCv) return;
  // Size the bitmap to the visual viewport in CSS px (canvas is position:fixed
  // inset:0, so that's its box). No dpr multiply and no reading the element's own
  // size — that avoids a bitmap→box→resize feedback loop. 1:1, centre is centre.
  radarW = window.innerWidth; radarH = window.innerHeight;
  if (radarCv.width !== radarW) radarCv.width = radarW;
  if (radarCv.height !== radarH) radarCv.height = radarH;
  radarCtx = radarCv.getContext("2d");
  radarCtx.setTransform(1, 0, 0, 1, 0, 0);
}
// Heading from our own GPS course, smoothed + held north-up when stopped (so the
// scope doesn't spin on COG noise) — same policy as convoy_ui.h.
function updateRadarHeading() {
  const self = members[me.id];
  const moving = self && self.speed != null && self.speed > 1.5;
  if (!self || self.heading == null || !moving) { radarHdgValid = false; return; }
  const deg = ((self.heading % 360) + 360) % 360;
  if (!radarHdgValid) { radarHdg = deg; radarHdgValid = true; return; }
  let diff = deg - radarHdg;
  while (diff > 180) diff -= 360;
  while (diff < -180) diff += 360;
  radarHdg = ((radarHdg + diff * 0.25) % 360 + 360) % 360;
}
function drawRadar(ts) {
  if (!radarCtx) return;
  const ctx = radarCtx, cx = radarW / 2, cy = radarH * 0.44;  // nudged up over the sheet
  const R = Math.min(radarW, radarH * 0.88) * 0.42;
  ctx.clearRect(0, 0, radarW, radarH);
  ctx.textAlign = "center"; ctx.textBaseline = "middle";

  updateRadarHeading();
  const northUp = radarNorthUp || !radarHdgValid;
  const hdg = northUp ? 0 : radarHdg;

  for (let i = 1; i <= 3; i++) {                       // range rings
    ctx.beginPath(); ctx.arc(cx, cy, R * i / 3, 0, Math.PI * 2);
    ctx.strokeStyle = STEEL; ctx.globalAlpha = i === 3 ? 0.8 : 0.5;
    ctx.lineWidth = i === 3 ? 2 : 1; ctx.stroke();
  }
  ctx.globalAlpha = 1;

  const pill = $("hdg-btn");
  pill.textContent = radarNorthUp ? "NORTH UP"
    : radarHdgValid ? `${Math.round(hdg) % 360}° ${compass(hdg)}` : "-- HOLD";

  for (const [lbl, abs] of [["N", 0], ["E", 90], ["S", 180], ["W", 270]]) {
    const sa = (abs - hdg) * Math.PI / 180, rr = R - 12;
    if (lbl === "N") { ctx.fillStyle = SELF_COLOR; ctx.font = `bold ${northUp ? 22 : 15}px system-ui,sans-serif`; }
    else { ctx.fillStyle = "#4A6472"; ctx.font = "12px system-ui,sans-serif"; }
    ctx.fillText(lbl, cx + rr * Math.sin(sa), cy - rr * Math.cos(sa));
  }
  if (!northUp) {                                       // forward lubber + chevron
    ctx.strokeStyle = SELF_COLOR; ctx.globalAlpha = 0.35; ctx.lineWidth = 2;
    ctx.beginPath(); ctx.moveTo(cx, cy - 14); ctx.lineTo(cx, cy - (R - 6)); ctx.stroke();
    ctx.globalAlpha = 1; ctx.fillStyle = SELF_COLOR; ctx.font = "15px system-ui,sans-serif";
    ctx.fillText("▲", cx, cy - (R + 2));
  }

  const self = members[me.id] || {};
  if (self.lat == null || self.lon == null) {
    ctx.fillStyle = SELF_COLOR; ctx.font = "16px system-ui,sans-serif";
    ctx.fillText("ACQUIRING GPS…", cx, cy);
    return;
  }

  const t = now();
  let maxd = 0; const others = [];
  for (const [id, m] of Object.entries(members)) {
    if (id === me.id || m.lat == null) continue;
    if (t - (m.ts || 0) > STALE_DROP_MS) continue;
    const d = distM(self, m), online = t - (m.ts || 0) < ONLINE_WINDOW_MS;
    if (online && d > maxd) maxd = d;
    others.push({ id, m, d, b: bearing(self, m), online });
  }
  const scale = niceScale(maxd || 1);

  ctx.fillStyle = "#4E6675"; ctx.font = "12px system-ui,sans-serif"; ctx.textAlign = "left";
  ctx.fillText(fmtDist(scale), cx + R * 0.6, cy - R * 0.66);
  ctx.textAlign = "center";

  for (const o of others) {
    const rel = (o.b - hdg) * Math.PI / 180;
    const rpx = Math.min(o.d / scale * R, R);
    const x = cx + rpx * Math.sin(rel), y = cy - rpx * Math.cos(rel);
    ctx.globalAlpha = o.online ? 1 : 0.4;
    ctx.beginPath(); ctx.arc(x, y, 7, 0, Math.PI * 2);
    ctx.fillStyle = colorFor(o.id); ctx.fill();
    ctx.lineWidth = 2; ctx.strokeStyle = "#000"; ctx.stroke();
    ctx.fillStyle = colorFor(o.id); ctx.font = "bold 13px system-ui,sans-serif";
    ctx.fillText(o.m.callsign || "?", x, y - 16);
    ctx.globalAlpha = 1;
  }

  const phase = (ts % 1600) / 1600;                     // ME pulse + dot
  ctx.beginPath(); ctx.arc(cx, cy, 11 + phase * 22, 0, Math.PI * 2);
  ctx.strokeStyle = SELF_COLOR; ctx.globalAlpha = 1 - phase; ctx.lineWidth = 2; ctx.stroke();
  ctx.globalAlpha = 1;
  ctx.beginPath(); ctx.arc(cx, cy, 9, 0, Math.PI * 2);
  ctx.fillStyle = SELF_COLOR; ctx.fill();
  ctx.lineWidth = 2; ctx.strokeStyle = "#000"; ctx.stroke();
  ctx.fillStyle = SELF_COLOR; ctx.font = "bold 12px system-ui,sans-serif";
  ctx.fillText("ME", cx, cy - 16);
}
function radarLoop(ts) {
  if (view !== "radar") { radarRAF = null; return; }
  // Re-size only when the viewport actually changed (rotate/resize) — compares
  // against innerWidth/innerHeight, which the bitmap doesn't affect, so no loop.
  if (window.innerWidth !== radarW || window.innerHeight !== radarH) sizeRadar();
  if (ts - radarLastDraw >= 33) { radarLastDraw = ts; drawRadar(ts); }  // ~30fps
  radarRAF = requestAnimationFrame(radarLoop);
}
function startRadar() { if (!radarRAF) radarRAF = requestAnimationFrame(radarLoop); }
function stopRadar() { if (radarRAF) { cancelAnimationFrame(radarRAF); radarRAF = null; } }

const ICON_RADAR = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linecap="round"><circle cx="12" cy="12" r="9"/><circle cx="12" cy="12" r="4.5"/><circle cx="12" cy="12" r="1.4" fill="currentColor" stroke="none"/></svg>`;
const ICON_MAP = `<svg viewBox="0 0 24 24" fill="none" stroke="currentColor" stroke-width="2" stroke-linejoin="round"><path d="M9 4 3 6v14l6-2 6 2 6-2V4l-6 2-6-2z"/><line x1="9" y1="4" x2="9" y2="18"/><line x1="15" y1="6" x2="15" y2="20"/></svg>`;

function switchView(to) {
  view = to;
  const mapEl = $("map"), rad = $("radar"), pill = $("hdg-btn"),
        rec = $("btn-recenter"), vbtn = $("btn-view");
  if (to === "radar") {
    mapEl.classList.add("hidden"); rad.classList.remove("hidden"); pill.classList.remove("hidden");
    rec.style.display = "none"; vbtn.innerHTML = ICON_MAP; vbtn.title = "Map view";
    sizeRadar(); startRadar();
  } else {
    rad.classList.add("hidden"); pill.classList.add("hidden"); mapEl.classList.remove("hidden");
    rec.style.display = ""; vbtn.innerHTML = ICON_RADAR; vbtn.title = "Radar view";
    stopRadar(); setTimeout(() => { if (map) map.invalidateSize(); }, 30);
  }
}
function toggleView() { switchView(view === "radar" ? "map" : "radar"); }

function toggleSheet() {
  sheetExpanded = !sheetExpanded;
  $("sheet").classList.toggle("expanded", sheetExpanded);
  $("fabs").style.display = sheetExpanded ? "none" : "flex";
  updateSheetMore();
}
function updateSheetMore() {
  const el = $("sheet-more"); if (!el) return;
  if (sheetExpanded) { el.textContent = "tap to collapse"; return; }
  el.textContent = lastRowCount > 2 ? `+${lastRowCount - 2} more · tap` : "";
}

// ── Phase 1: relay the roster to the Trailmaster board over BLE ───────────────
// Encodes the room to the compact wire format convoy_net.h parses. The connected
// phone is the board's own car, so its own member becomes the "S" (self) line.
function encodeRoster() {
  const t = now();
  const self = members[me.id];
  const lines = [];
  if (self && self.lat != null) {
    lines.push(`S,${self.lat.toFixed(6)},${self.lon.toFixed(6)},` +
      `${self.heading != null ? Math.round(self.heading) : -1},` +
      `${self.speed != null ? self.speed.toFixed(1) : 0},1`);
  } else {
    lines.push("S,0,0,-1,0,0");
  }
  for (const [id, m] of Object.entries(members)) {
    if (id === me.id || m.lat == null) continue;
    if (t - (m.ts || 0) > STALE_DROP_MS) continue;
    const online = t - (m.ts || 0) < ONLINE_WINDOW_MS ? 1 : 0;
    const cs = (m.callsign || "?").slice(0, 5).replace(/,/g, "");
    lines.push(`C,${cs},${m.lat.toFixed(6)},${m.lon.toFixed(6)},${online}`);
  }
  return lines.join("\n");
}
async function pushBoard() {
  if (!boardChar) return;
  const data = new TextEncoder().encode(encodeRoster());
  try {
    if (boardChar.writeValueWithoutResponse) await boardChar.writeValueWithoutResponse(data);
    else await boardChar.writeValue(data);
  } catch (e) { /* transient BLE error — next tick retries */ }
}
// ── Background tracking (OwnTracks) ─────────────────────────────────────────
// This page cannot report position with the screen off — geolocation stops when
// it backgrounds, service workers have no access to it, and iOS suspends the tab
// outright. OwnTracks does it properly on both platforms, posting to the relay
// Worker, which writes into the same room. So reporting stops depending on
// anyone keeping a browser tab alive.
//
// The relay URL deliberately contains NO room code. The room is looked up from
// assign/<callsign>, written below when you join — which is why the phone is
// configured once and never again, even though room codes change every convoy.
const isIOS = () => /iPad|iPhone|iPod/.test(navigator.userAgent) ||
                    (navigator.platform === "MacIntel" && navigator.maxTouchPoints > 1);

function relayUrl() { return `${RELAY_URL}/${RELAY_TOKEN}`; }

// Build the one-tap configuration link. OwnTracks accepts a base64-encoded
// .otrc config inline in a URL, which is the whole reason to prefer it over
// Traccar Client — nobody has to type a URL on a phone keyboard.
function ownTracksConfigLink(callsign) {
  const cfg = {
    _type: "configuration",
    mode: 3,                        // 3 = HTTP (0 would be MQTT)
    url: relayUrl(),
    username: callsign,             // arrives as X-Limit-U; becomes the callsign
    deviceId: "convoy",
    tid: callsign.slice(-2),        // 2-char label OwnTracks draws on the map
    monitoring: 1,                  // significant-change reporting
    locatorInterval: 30,
    locatorDisplacement: 50,
    pubExtendedData: true,
  };
  // btoa needs latin1; callsigns are alphanumeric so this is safe.
  return "owntracks:///config?inline=" + encodeURIComponent(btoa(JSON.stringify(cfg)));
}

// Tell the relay which convoy this callsign is currently in. Called on join, so
// switching convoys never requires touching the phone's tracking config.
async function publishAssignment() {
  if (!db || !me || !me.callsign || !me.code) return;
  try {
    await set(ref(db, `assign/${me.callsign}`), { room: me.code, ts: now() });
  } catch (e) {
    console.warn("assign:", e.message || e);
  }
}

function openTracking() {
  if (!me || !me.callsign) return;
  $("track").classList.remove("hidden");
  $("tr-store").href = isIOS()
    ? "https://apps.apple.com/app/owntracks/id692424691"
    : "https://play.google.com/store/apps/details?id=org.owntracks.android";
  $("tr-config").href = ownTracksConfigLink(me.callsign);
  $("tr-url").textContent = relayUrl();
  $("tr-user").textContent = me.callsign;
}

// ── Board picker (Firebase-mediated pairing) ────────────────────────────────
// A browser cannot discover or talk to the board over the LAN — the page is
// HTTPS and the board is plain http on a private IP, which is hard-blocked as
// mixed content, and a browser tab has no listening socket for the board to
// connect back to. But both ends already speak to Firebase, so pairing goes
// through there: the board publishes devices/<id>, we list the fresh ones, and
// picking one writes this convoy's room into it.
const BOARD_FRESH_MS = 120000;   // a board heartbeats every 2-10s; be generous

async function openBoardPicker() {
  const box = $("board-list");
  $("boards").classList.remove("hidden");
  box.textContent = "Looking for boards…";

  if (!db || !me || !me.code) { box.textContent = "Join a convoy first."; return; }

  let snap;
  try { snap = await get(ref(db, "devices")); }
  catch (e) { box.textContent = "Couldn't reach Firebase."; console.warn(e); return; }

  const all = snap.val() || {};
  const fresh = Object.entries(all)
    .filter(([, d]) => d && d.ts && (now() - d.ts) < BOARD_FRESH_MS);

  box.textContent = "";

  // This phone first: keeping it reporting with the screen off matters to every
  // driver, whereas the board list only matters to whoever owns the Trailmaster.
  {
    const row  = document.createElement("div"); row.className = "brow";
    const info = document.createElement("div");
    const nm   = document.createElement("b");     nm.textContent = "This phone";
    const st   = document.createElement("small"); st.textContent = "keep reporting with the screen off";
    info.append(nm, st);
    const btn = document.createElement("button");
    btn.className = "btn btn-orange";
    btn.textContent = "Set up";
    btn.addEventListener("click", () => { $("boards").classList.add("hidden"); openTracking(); });
    row.append(info, btn);
    box.appendChild(row);
  }

  if (!fresh.length) {
    const p = document.createElement("small");
    p.style.color = "var(--muted)";
    p.textContent = "No boards online. Turn on your hotspot and open the Tracker on the board.";
    box.appendChild(p);
  }
  for (const [id, d] of fresh) {
    const linked = d.room && normalizeCode(d.room) === me.code;
    const row  = document.createElement("div"); row.className = "brow";
    const info = document.createElement("div");
    const nm   = document.createElement("b");     nm.textContent = d.name || id;
    const st   = document.createElement("small");
    st.textContent = linked ? "connected to this convoy"
                            : d.room ? `in convoy ${d.room}` : "not connected";
    info.append(nm, st);
    const btn = document.createElement("button");
    btn.className = "btn " + (linked ? "btn-outline" : "btn-orange");
    btn.textContent = linked ? "Linked" : "Connect";
    btn.disabled = !!linked;
    btn.addEventListener("click", () => linkBoard(id, btn));
    row.append(info, btn);
    box.appendChild(row);
  }

  // Legacy Bluetooth relay, where the browser has it at all.
  if (navigator.bluetooth) {
    const row  = document.createElement("div"); row.className = "brow";
    const info = document.createElement("div");
    const nm   = document.createElement("b");     nm.textContent = "Bluetooth";
    const st   = document.createElement("small"); st.textContent = "pair directly, no hotspot";
    info.append(nm, st);
    const btn = document.createElement("button");
    btn.className = "btn btn-outline";
    btn.textContent = "Pair";
    btn.addEventListener("click", () => { $("boards").classList.add("hidden"); connectBoard(); });
    row.append(info, btn);
    box.appendChild(row);
  }
}

async function linkBoard(id, btn) {
  btn.disabled = true;
  btn.textContent = "Linking…";
  try {
    await update(ref(db, `devices/${id}`), { room: me.code, callsign: me.callsign });
    setBoardBtn(true);
    $("boards").classList.add("hidden");
  } catch (e) {
    console.warn("link board:", e.message || e);
    btn.disabled = false;
    btn.textContent = "Retry";
  }
}

async function connectBoard() {
  if (!navigator.bluetooth) return;
  try {
    const dev = await navigator.bluetooth.requestDevice({ filters: [{ services: [CONVOY_NET_SVC] }] });
    dev.addEventListener("gattserverdisconnected", onBoardDisconnect);
    const gatt = await dev.gatt.connect();
    const svc = await gatt.getPrimaryService(CONVOY_NET_SVC);
    boardChar = await svc.getCharacteristic(CONVOY_NET_CHR);
    boardDev = dev;
    setBoardBtn(true);
    pushBoard();                                  // push immediately
    boardTimer = setInterval(pushBoard, 1200);    // then ~1/s
  } catch (e) { console.warn("Bluetooth:", e.message || e); }
}
function onBoardDisconnect() {
  if (boardTimer) { clearInterval(boardTimer); boardTimer = null; }
  boardChar = null; setBoardBtn(false);
}
function setBoardBtn(on) {
  const b = $("btn-link"); if (!b) return;
  b.classList.toggle("linked", on);
  b.title = on ? "Trailmaster connected" : "Connect Trailmaster";
}

// ── UI helpers ────────────────────────────────────────────────────────────────
function showMap(code) {
  $("join").classList.add("hidden");
  $("app").classList.remove("hidden");
  $("code-chip").textContent = code;
  radarCv = $("radar");
  view = "map";
  ensureMap();
  setTimeout(() => map.invalidateSize(), 50);   // map was hidden when created
}
function err(msg) { $("f-err").textContent = msg; }
function esc(s) {
  return String(s).replace(/[&<>"]/g, (c) =>
    ({ "&": "&amp;", "<": "&lt;", ">": "&gt;", '"': "&quot;" }[c]));
}

// ── Tabs ──────────────────────────────────────────────────────────────────────
// The map is a live Leaflet instance sized to its container, so panels overlay
// it rather than replacing it — coming back to Convoy must not re-init the map
// or re-fit the view.
let tab = "convoy";

function showTab(which) {
  tab = which;
  $("p-wifi").classList.toggle("hidden", which !== "wifi");
  $("p-img").classList.toggle("hidden",  which !== "img");
  // Convoy's own furniture would otherwise float above a panel.
  for (const id of ["sheet", "fabs", "hdg-btn"]) {
    const el = $(id);
    if (el) el.style.visibility = which === "convoy" ? "" : "hidden";
  }
  document.querySelectorAll("#tabs .tab")
          .forEach((b) => b.classList.toggle("active", b.dataset.tab === which));
  if (which === "wifi") refreshBoardNote();
  if (which === "img")  refreshPhotoList();
}

// ── Board lookup, shared by the Wi-Fi and Images tabs ────────────────────────
// Both write into the board's own node, so both need to know which board is
// yours. A board only lists itself while its pairing window is open, so this is
// re-read each time a tab opens rather than cached.
async function freshBoards() {
  if (!db) return [];
  let snap;
  try { snap = await get(ref(db, "devices")); } catch { return []; }
  return Object.entries(snap.val() || {})
    .filter(([, d]) => d && d.ts && (now() - d.ts) < BOARD_FRESH_MS);
}

async function refreshBoardNote() {
  const note = $("wifi-board");
  const boards = await freshBoards();
  if (!boards.length) {
    note.innerHTML = "<b>No board is listening.</b> Open the Tracker screen on " +
                     "the Trailmaster — it only accepts settings while that is up.";
    return;
  }
  note.innerHTML = "Sending to <b>" + boards.map(([, d]) => d.name || "Trailmaster").join(", ") + "</b>";
}

function wireWifiTab() {
  $("w-send").addEventListener("click", async () => {
    const ssid = $("w-ssid").value.trim();
    const pass = $("w-pass").value;
    const errEl = $("w-err");
    errEl.style.color = "";
    if (!ssid) { errEl.textContent = "Enter a network name."; return; }
    if (ssid.length > 32 || pass.length > 64) { errEl.textContent = "Too long for the board."; return; }

    const boards = await freshBoards();
    if (!boards.length) { errEl.textContent = "No board is listening right now."; return; }

    errEl.textContent = "Sending…";
    try {
      // Flat keys, not a nested object: the database rules validate per field
      // and reject anything they do not name, so a nested shape would need a
      // rules change of its own for no benefit.
      await Promise.all(boards.map(([id]) =>
        update(ref(db, `devices/${id}`), { wssid: ssid, wpass: pass })));
      errEl.style.color = "#7ee08a";
      errEl.textContent = "Sent. The board saves it within a few seconds, then erases it here.";
      $("w-ssid").value = ""; $("w-pass").value = "";
    } catch (e) {
      errEl.textContent = "Rejected by the database — check the rules include wssid/wpass.";
      console.warn(e);
    }
  });
}

// ── Images ────────────────────────────────────────────────────────────────────
// The board has no image decoder: it downloads whatever photos.json points at
// straight onto the SD card and blits it. So the browser does the work — crop to
// the round 466 px screen, then convert to raw little-endian RGB565.
const IMG_DIM = 466;
let imgEl = null, imgScale = 1, imgMinScale = 1, imgOffX = 0, imgOffY = 0;

function drawCrop() {
  const cv = $("i-canvas"), ctx = cv.getContext("2d");
  ctx.fillStyle = "#000"; ctx.fillRect(0, 0, IMG_DIM, IMG_DIM);
  if (!imgEl) return;
  const w = imgEl.width * imgScale, h = imgEl.height * imgScale;
  imgOffX = Math.min(0, Math.max(IMG_DIM - w, imgOffX));
  imgOffY = Math.min(0, Math.max(IMG_DIM - h, imgOffY));
  ctx.drawImage(imgEl, imgOffX, imgOffY, w, h);
}

function toRGB565(ctx) {
  const px = ctx.getImageData(0, 0, IMG_DIM, IMG_DIM).data;
  const out = new Uint8Array(IMG_DIM * IMG_DIM * 2);
  for (let i = 0, o = 0; i < px.length; i += 4) {
    const v = ((px[i] & 0xf8) << 8) | ((px[i + 1] & 0xfc) << 3) | (px[i + 2] >> 3);
    out[o++] = v & 0xff;           // little-endian, matching convert_to_bin.py
    out[o++] = (v >> 8) & 0xff;
  }
  return out;
}

function wireImageTab() {
  $("i-file").addEventListener("change", (ev) => {
    const f = ev.target.files && ev.target.files[0];
    if (!f) return;
    const im = new Image();
    im.onload = () => {
      imgEl = im;
      imgMinScale = Math.max(IMG_DIM / im.width, IMG_DIM / im.height);
      imgScale = imgMinScale;
      imgOffX = (IMG_DIM - im.width * imgScale) / 2;
      imgOffY = (IMG_DIM - im.height * imgScale) / 2;
      const z = $("i-zoom");
      z.min = imgMinScale; z.max = imgMinScale * 3; z.step = 0.01; z.value = imgScale;
      $("i-zoomrow").classList.remove("hidden");
      $("i-send").disabled = false;
      drawCrop();
    };
    im.src = URL.createObjectURL(f);
  });

  $("i-zoom").addEventListener("input", (e) => {
    const cx = IMG_DIM / 2, prev = imgScale;
    imgScale = parseFloat(e.target.value);
    // Zoom about the centre, so the framing the user set does not drift.
    imgOffX = cx - ((cx - imgOffX) / prev) * imgScale;
    imgOffY = cx - ((cx - imgOffY) / prev) * imgScale;
    drawCrop();
  });

  let drag = null;
  const cv = $("i-canvas");
  cv.addEventListener("pointerdown", (e) => { drag = { x: e.clientX, y: e.clientY }; cv.setPointerCapture(e.pointerId); });
  cv.addEventListener("pointermove", (e) => {
    if (!drag) return;
    const k = IMG_DIM / cv.clientWidth;              // canvas is displayed smaller
    imgOffX += (e.clientX - drag.x) * k;
    imgOffY += (e.clientY - drag.y) * k;
    drag = { x: e.clientX, y: e.clientY };
    drawCrop();
  });
  cv.addEventListener("pointerup",     () => { drag = null; });
  cv.addEventListener("pointercancel", () => { drag = null; });

  $("i-send").addEventListener("click", uploadImage);
}

async function uploadImage() {
  const errEl = $("i-err");
  errEl.style.color = "";
  if (!imgEl || !db) return;

  const name = `tm_${Date.now().toString(36)}.bin`;
  errEl.textContent = "Converting…";
  const bytes = toRGB565($("i-canvas").getContext("2d"));

  try {
    errEl.textContent = "Uploading…";
    const storage = getStorage();
    const dest = sref(storage, `photos/${name}`);
    await uploadBytes(dest, bytes, { contentType: "application/octet-stream" });
    const url = await getDownloadURL(dest);

    // The board reads this list on its own schedule (ota_sync_photos) and
    // downloads anything it does not already have, so publishing the entry IS
    // the handoff — nothing has to be pushed to the board directly.
    const snap = await get(ref(db, "photos/files"));
    const files = Array.isArray(snap.val()) ? snap.val() : [];
    files.push({ path: name, url });
    await set(ref(db, "photos/files"), files);

    errEl.style.color = "#7ee08a";
    errEl.textContent = "Uploaded. The board fetches it next time it syncs.";
    $("i-send").disabled = true;
    refreshPhotoList();
  } catch (e) {
    // Storage is a separate Firebase product and is not enabled by default;
    // say so plainly rather than reporting a generic failure.
    const msg = String(e && e.code || e);
    errEl.textContent = msg.includes("storage/unauthorized") || msg.includes("storage/unknown")
      ? "Firebase Storage rejected this — enable Storage and allow writes to photos/."
      : "Upload failed: " + msg;
    console.warn(e);
  }
}

async function refreshPhotoList() {
  const box = $("i-list");
  if (!db) { box.textContent = "—"; return; }
  let snap;
  try { snap = await get(ref(db, "photos/files")); } catch { box.textContent = "Couldn't read."; return; }
  const files = Array.isArray(snap.val()) ? snap.val() : [];
  box.textContent = "";
  if (!files.length) { box.textContent = "Nothing published yet."; return; }
  for (const f of files) {
    const d = document.createElement("div");
    d.className = "it";
    d.textContent = f.path || "?";
    box.appendChild(d);
  }
}

boot();

// PWA
if ("serviceWorker" in navigator) {
  navigator.serviceWorker.register("./sw.js").catch(() => {});
}
