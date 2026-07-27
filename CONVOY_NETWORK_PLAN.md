# Trailmaster Convoy — Network (Online) Setup Plan

A cellular/internet-based convoy tracker that complements the off-grid Meshtastic
LoRa setup. Cars join a shared "convoy room" from their phone (a web page / lite
PWA), share GPS over the internet, and pop up on the Trailmaster radar screen.

This is **planning only** — nothing here is built yet.

---

## 1. Core insight — reuse, don't rebuild

`convoy_ui.h` (the radar screen) is completely **data-source-agnostic**. It only
exposes setters:

- `convoy_set_self(lat, lon, fix)`
- `convoy_set_heading(deg, valid)`
- `convoy_set_car(i, name, lat, lon, color, online, has_fix)`

The Meshtastic path (`convoy_link.h`) fills those over BLE from a co-located
T-Beam. **The network path is just a second feeder** that fills the same setters
from the internet. The radar screen, geometry, heading-up mode, cards — all
unchanged. Mesh and network cars can even coexist on one scope.

New firmware feeder file (Phase 1): `convoy_net.h` (parallel to `convoy_link.h`).

---

## 2. Topology — the phone is the gateway

Off-grid, the T-Beam is the "co-processor with GPS + comms." Online, **the phone
plays that exact role** — it has GPS *and* cell data.

```
  Driver's phone  (web page / installable PWA)
    ├─ reads phone GPS            (Geolocation watchPosition)
    ├─ joins a convoy room        (enter a code, e.g. TRAIL-4X4)
    ├─ pushes own position ─────────────────► Firebase room
    └─ receives all members ◄────────────────┘  (realtime fan-out)
          │
          └─ (only the Trailmaster owner's phone) relays roster
                          │  Web Bluetooth / native BLE
                          ▼
             Trailmaster board  →  convoy_set_car()  →  radar screen
```

Key realization for a **mixed iPhone/Android fleet**: only **one** phone needs to
talk to the board — the phone of the driver who *owns* the Trailmaster. Every
other convoy member just needs the web map + Firebase. So the board-hop platform
constraint applies to a single device, not the whole convoy (see §6).

---

## 3. Sign-up / join flow ("just sign up on a page")

1. Driver opens the URL (hosted on GitHub Pages, same as the flasher).
2. Enters **name**, **callsign** (≤5 chars — radar label budget), and a
   **convoy code**. "Create" generates a fresh code; "Join" uses an existing one.
3. Grants location permission.
4. They're live. Everyone with the same code sees each other. **The code is the
   room** — no accounts required for the MVP.
5. (Optional later) "Install app" prompt → PWA on the home screen, full-screen,
   works like a native app.

---

## 4. Backend — Firebase Realtime Database

Chosen for: static web app + realtime listeners, generous free tier, zero server
to run, fits existing GitHub Pages hosting.

### Data model

```
/convoys/{code}/members/{memberId}
    name:     "Shashank"
    callsign: "TM1"          // ≤5 chars, radar label
    lat:      12.9716
    lon:      77.5946
    heading:  184.5          // deg from true north (from GPS course)
    speed:    8.3            // m/s (for moving/stopped + heading validity)
    color:    "#00E5FF"      // assigned per member for the radar dot
    ts:       1690000000000  // last update (ms) — freshness / presence
```

- Each phone **writes only its own** `{memberId}` node; **listens to the whole**
  `/members` subtree.
- `memberId` = random client id in localStorage (survives reloads, no login).
- **Presence:** a member is "online" if `now - ts < 120s`. This matches
  `convoy_ui.h`'s existing 120 000 ms online window exactly. Use Firebase
  `onDisconnect()` to null the node on clean disconnects too.
- **Update cadence:** throttle writes to ~1 every 3–5 s (matches Meshtastic
  broadcast cadence; saves phone battery + Firebase quota). Interpolate on the
  map between updates if it looks choppy.

### Security rules (MVP → hardening)

- MVP: rules require the room path + basic shape validation; the code is the
  shared secret.
- Positions are sensitive (live location). Harden with: Firebase Anonymous Auth,
  short-lived / rotating convoy codes, and a per-room member cap + write
  rate-limit in rules.

---

## 5. The web app (PWA) — "lite Trailmaster app using web"

Pure static site (HTML/JS + Firebase SDK), no build server needed.

- **Join screen:** name + callsign + code.
- **Live screen:** Leaflet map, own position via `navigator.geolocation.watchPosition`,
  every member as a colored marker + callsign; auto-fit bounds; tap a marker for
  distance/bearing. This is useful **standalone**, even with no Trailmaster board.
- **Heading/speed:** taken from the Geolocation `coords.heading` / `coords.speed`
  when moving; feed the board so heading-up mode works.
- **Board pairing (Android/Chrome):** a "Connect Trailmaster" button →
  Web Bluetooth → writes the roster to the board (see §6/§7).
- Installable: web manifest + service worker → add-to-home-screen PWA.

---

## 6. Board hop across a mixed fleet (the one real constraint)

Web Bluetooth (page writes straight to the board) works on **Chrome/Android** but
**not iOS Safari**. Resolution, given only the Trailmaster owner's phone needs it:

| Owner's phone | Board hop approach |
|---|---|
| **Android** | Pure PWA + Web Bluetooth. Nothing to install. Best case. |
| **iPhone** | Option A: **Bluefy** browser (Web Bluetooth on iOS). Option B: thin **Capacitor** native wrapper of the same web app, using a native BLE plugin. Option C: iPhone owner uses the phone-only map and skips the board (still sees convoy on the phone). |
| **Everyone else** | No board hop needed — pure web map participants on any phone. |

**Recommendation:** target the Android PWA + Web Bluetooth as the full path; treat
the board hop as Android-only for now and iPhones as map-only participants. Revisit
a Capacitor wrapper only if the Trailmaster owner is on iOS.

---

## 7. Board firmware — Phase 1 (`convoy_net.h`)

- **Board = BLE peripheral (GATT server).** Advertises "Trailmaster" with a
  Convoy service exposing one **writable "roster" characteristic**. The paired
  phone writes a **compact roster blob** every few seconds:
  - Per car: callsign, lat, lon, online flag; plus a **`self` flag** on the
    owner's own entry.
  - Board parses → `convoy_set_car()` for other cars, `convoy_set_self()` +
    `convoy_set_heading()` from the `self` entry (the owner phone's GPS is the
    Trailmaster car's position — no GPS on the board itself, same as the mesh path).
  - Blob format: start with newline-delimited CSV/JSON for easy bring-up; switch
    to a packed binary struct if it grows.
- Gate behind `#define CONVOY_NET_ENABLE 0/1` (mirrors `CONVOY_BLE_ENABLE`).
- **Sim:** never includes `convoy_net.h` (firmware-only, like `convoy_link.h`);
  sim keeps feeding mock data through the same setters.

### ⚠ Blocker inherited from the mesh path

Enabling BLE on the board hits the **NimBLE ↔ AMOLED QSPI display-corruption bug**
already flagged as BLOCKING (NimBLE init drops internal RAM 129→68 KB, collides
with LVGL draw buffers → permanent ghosting until reboot). Must fix *first*:

1. Move the LVGL draw buffer to **PSRAM** if it's in internal/DMA RAM (likely
   decisive).
2. Trim NimBLE config (1 connection, min buffers, smaller MTU).
3. If still tight, time-share / burst the link.

This is **shared work** with the Meshtastic path (both need BLE + display to
coexist), so it's not extra cost — it's the same prerequisite for both.

---

## 8. Network vs mesh — complementary, not competing

| | Meshtastic (LoRa) | Network (this plan) |
|---|---|---|
| Coverage | Off-grid, no cell needed | Anywhere with cell data |
| Range | Line-of-sight / relays | Unlimited (internet) |
| Extra hardware | T-Beam per car | None — just a phone |
| GPS accuracy | T-Beam u-blox | Phone GPS (often better) |
| Fails when | Terrain / out of LoRa range | Cell dead zones |

They fail in **opposite** conditions. End state: run both feeders into
`convoy_ui.h` and show each car from whichever source is fresher — optionally
tag the source (LoRa vs NET) per blip. The radar already supports up to
`CONVOY_MAX_CARS = 6`.

---

## 9. Phased task breakdown

**Phase 0 — Phone-only (no firmware, zero board risk)**
1. Firebase project + Realtime DB + security rules.
2. Static join page (name/callsign/code) on GitHub Pages.
3. Live Leaflet map: watchPosition write + realtime roster read + presence.
4. Throttling, reconnect, add-to-home-screen PWA manifest.
5. Field test with the real convoy — validates rooms/GPS/backend end-to-end.

**Phase 1 — Board integration**
6. Fix NimBLE ↔ AMOLED coexistence (PSRAM draw buffer). *Prereq, shared w/ mesh.*
7. `convoy_net.h`: board BLE peripheral + roster characteristic + parser →
   `convoy_set_*`.
8. Web app: Web Bluetooth pairing + roster push (Android).
9. iPhone-owner fallback decision (Bluefy vs Capacitor vs map-only).

**Phase 2 — Polish**
10. Merge mesh + network sources; per-car source tag.
11. Security hardening (anon auth, rotating codes, rate limits).
12. Nice-to-haves: SOS/ping, breadcrumb trail, ETA to lead car.

---

## 10. Open questions to resolve before Phase 1

- Is the Trailmaster owner's phone Android or iPhone? (decides §6 path)
- Roster blob format — CSV/JSON first, or go straight to packed binary?
- One active source at a time (NET **or** mesh), or always merge both?
