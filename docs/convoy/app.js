// Trailmaster Convoy — Phase 0 web app.
//
// Phones join a "convoy room" by code, share GPS through Firebase Realtime DB,
// and see each other live on a map. No accounts: the room code is the shared
// secret, the member id lives in localStorage. This same data model feeds the
// Trailmaster board in Phase 1 (Web Bluetooth relay) — see CONVOY_NETWORK_PLAN.md.

import { initializeApp } from "https://www.gstatic.com/firebasejs/10.12.2/firebase-app.js";
import {
  getDatabase, ref, set, remove, update, onValue,
} from "https://www.gstatic.com/firebasejs/10.12.2/firebase-database.js";
import { firebaseConfig, PUSH_INTERVAL_MS, ONLINE_WINDOW_MS, STALE_DROP_MS } from "./config.js";

// ── Colors: mirror convoy_ui.h so the phone map and the board radar agree ─────
const HUES = ["#00E5FF", "#00E676", "#FFD54F", "#FF4081", "#B388FF", "#FF8A65"];
const SELF_COLOR = "#FF6A00";                    // Trailmaster orange = "me"

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
  if (unsub) { unsub(); unsub = null; }
  if (myRef) { remove(myRef); }
  for (const k in markers) { map.removeLayer(markers[k]); }
  markers = {}; members = {}; fitted = false;
  stopRadar();
  if (view !== "map") switchView("map");
  sheetExpanded = false;
  $("sheet").classList.remove("expanded");
  $("fabs").style.display = "flex";
  $("app").classList.add("hidden");
  $("join").classList.remove("hidden");
}

// ── GPS → Firebase (throttled) ────────────────────────────────────────────────
function startGeo() {
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
function subscribe() {
  const cb = onValue(roomRef, (snap) => {
    members = snap.val() || {};
    render();
  });
  unsub = cb;   // onValue returns its own unsubscribe fn
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

boot();

// PWA
if ("serviceWorker" in navigator) {
  navigator.serviceWorker.register("./sw.js").catch(() => {});
}
