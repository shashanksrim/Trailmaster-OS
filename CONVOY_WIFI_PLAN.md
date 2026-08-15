# Convoy over WiFi — board pulls Firebase directly

Supersedes the phone-BLE-relay transport in `CONVOY_NETWORK_PLAN.md` for the
network path. Written 2026-07-29.

Goal: get the convoy roster onto the board without the owner's phone acting as a
BLE relay. This makes an iPhone a first-class owner phone and removes the
dependency on the phone's browser staying awake.

---

## 1. Constraints that drove the design

**(a) The browser can never be the WiFi transport to the board.** Not fixable.
The convoy PWA needs `navigator.geolocation` and `navigator.wakeLock`, both
**secure-context-only**. So:

- Page served from the board over `http://192.168.4.1` → no GPS, no wake lock.
- Page on GitHub Pages (HTTPS) POSTing to the board over LAN http → mixed
  content, hard-blocked, no user override.
- A board LAN IP cannot hold a CA-signed cert, and self-signed only permits
  click-through on top-level navigation, not on fetch.
- Phone joined to the board's softAP has no uplink → Firebase unreachable anyway.

This is why the PhotoFrame AP pattern does **not** generalise: photo upload uses
only a plain form POST, which needs no secure-context API.

Corollary: the board must fetch the roster itself. Web Bluetooth and this are the
only two browser→board transports, and Web BLE is absent on iOS. Hence WiFi
direct-to-cloud.

**(b) BLE and WiFi cannot coexist on this build.** Proven spike, recorded at
`.ino:2075-2078` — `esp_bt_controller_init -4`, malloc failure at 21-47KB free
internal RAM. Controller-side, so `CONFIG_BT_NIMBLE_MEM_ALLOC_MODE_EXTERNAL`
does not help. This is the root of the radio-handover handshake.

**(c) OBD owns WiFi.** The OBD worker is the only task permitted to touch WiFi
(`.ino:160-230`). Convoy asks via `convoy_radio_mode` and waits for the
`convoy_obd_released` ack. That ownership rule must be preserved.

## 2. Already built — do not rebuild

| Capability | Where | Note |
|---|---|---|
| HTTPS client w/ TLS | `OTAManager.cpp:268` | `WiFiClientSecure` + `HTTPClient` against raw.githubusercontent.com, incl. binary download. TLS on this board is proven. |
| Saved-network store | `OTAManager.h:32-34` | `ota_add_network` / `ota_remove_network` / `ota_list_networks`. The owner's hotspot is likely already saved from OTA. |
| Captive portal + creds UI | `PhotoFrameApp.cpp:1005-1120` | softAP 192.168.4.1, DNS portal, `/wifi`, `/wifi/add`, `/wifi/list`. Phone keyboard instead of on-screen entry. |
| Source picker + role switch | `.ino:2053-2159` | `cvs_source_t` enum, `convoy_src_sel`, role-class change logic. |
| UI setters (source-agnostic) | `convoy_ui.h` | `convoy_set_self` / `convoy_set_car` / `convoy_set_heading`. Radar needs no change. |

## 3. Design

New source `CVS_CLOUD` alongside `CVS_MESH` / `CVS_PHONE`.

```
owner phone (hotspot, cellular)          other cars' phones
        │                                        │
        │  board joins as STA                    │ PWA → Firebase RTDB
        ▼                                        ▼
   ESP32-S3 ──── HTTPS SSE ────►  convoys/{code}/members.json
        │
        └─► convoy_set_self / convoy_set_car  →  radar (unchanged)
```

- Board is **STA on the owner's hotspot**. Phone keeps cellular for Firebase.
- **No NimBLE in this mode at all.** Radio handover becomes a WiFi→WiFi STA
  retarget, not a `WIFI_OFF` + BT-controller bring-up.
- The owner's own car position still comes from the phone PWA (board has no GPS),
  published to Firebase like any other member. A sleeping phone now loses only
  its own dot, not the whole roster.

**Transport: RTDB SSE streaming, not polling.** A fresh TLS handshake costs
~1-2s on ESP32; polling at 1Hz is not viable. Use `Accept: text/event-stream`
against
`https://trailmaster-e43b1-default-rtdb.asia-southeast1.firebasedatabase.app/convoys/{code}/members.json`
and hold one connection open. RTDB pushes `put`/`patch` events only on change.

## 4. Phases

### Phase 0 — spike ✅ PASSED on hardware 2026-07-29
Joined the phone hotspot and pulled the roster over HTTPS with the Tracker open:

```
[OBD] WiFi released for Tracker (STA kept up) (internal RAM=205824)
[CVW] joined 'Pixel10_shnk' rssi=-62 ip=10.192.232.44  internal=180384 (wifi cost 572)
[CVW] GET .../convoys/SETME/members.json -> 200  len=4  internal=132872 (tls cost 47512)
[CVW] body: null      (expected — SETME is not a real room)
[CVW] spike done: internal=158124 (net delta -22832)
```

**Heap is a non-issue, by a wide margin.** 180KB free internal with the convoy UI
live; TLS costs ~47KB for the duration of the session, leaving ~133KB. For
contrast, the NimBLE path was failing to init at 21-47KB free *total*. The WiFi
join itself costs almost nothing (572 bytes) — the whole cost is the TLS session.

Carry-overs into Phase 1:

- **~22KB is not returned** after `http.end()` + `WiFi.disconnect()`. Some is the
  WiFi driver staying resident (we deliberately don't `WIFI_OFF`). Matters much
  less for the real design, which holds ONE long-lived SSE connection rather than
  repeating handshakes — but watch it across repeated Tracker enter/exit cycles.
- **Ordering costs 15s.** `load_networks()` returns saved networks in stored
  order, so the board burned a full 15s timeout on out-of-range home WiFi before
  reaching the hotspot. Phase 1 should scan first and only attempt SSIDs that are
  actually in range, or try strongest-RSSI first.
- `WiFi.scanNetworks()` returned -2 (failed) when called immediately after the
  OBD handover. Didn't matter once the join succeeded, but the scan needs a
  settle delay if Phase 1 relies on it for the ordering fix above.

Note the 2.4GHz gotcha still applies to iPhone hotspots (ESP32-S3 has no 5GHz
radio); this run used a Pixel hotspot, which was already a saved network.

### Phase 0.5 — SD fallback (done, and it was a real bug)
The spike initially found **zero** saved networks. Cause: every credential read
went through `sd_load_networks()` alone, and the SD card on this board is
unavailable — so the two networks sitting in NVS were invisible. NVS was written
as a "backup" that nothing ever read back.

Fixed in `OTAManager.cpp`: added `nvs_load_networks()` and a `load_networks()`
that tries SD then falls back to NVS, and routed all four read sites through it.
Also made `sd_save_networks()` return a bool, because `migrate_nvs_to_sd()` was
logging "Migrated N network(s) NVS->SD" unconditionally — printing success
directly after the write had warned that it failed.

**This was breaking OTA too**, not just convoy: with no readable SD, the updater
could not find a network to connect to. Worth checking why the card is
unavailable (not inserted? not mounted?) since PhotoFrame reads photos from it.

### Phase 1 — `convoy_wifi.h` ✅ PROVEN ON HARDWARE 2026-07-29
`convoy_wifi_begin/loop/status/end`, mirroring `convoy_net.h`. End-to-end run
against a real room (phone GPS → Firebase → board WiFi → radar):

```
[OTA] Attempting 2 saved network(s), best signal first...
[OTA] Trying network 1: 'Pixel10_shnk'
[CVW] joined 'Pixel10_shnk' rssi=-80 room=AENP self=TM1 internal=175496
[CVW] roster: 4 member(s), 3 car(s), self=fix
```

Decisions taken during the build:

- **Polling with connection reuse, NOT SSE.** The reason the plan specified SSE
  was the TLS handshake cost (~47KB, seconds) — but `setReuse(true)` on a
  long-lived `HTTPClient`/`WiFiClientSecure` avoids the handshake just as well,
  while reusing the already-proven OTA request path instead of adding an
  event-stream parser to an unproven feature. Poll cadence 1.5s. SSE remains
  available as an optimisation if latency or data volume ever justifies it.
- **Presence needs no clock.** The board has no NTP and in convoy mode may have
  no route to a time server, so a member is "online" if its `ts` is within
  `CONVOY_ROSTER_ONLINE_MS` (120s, matching convoy_ui) of the FRESHEST member in
  the roster, rather than of wall-clock now. Self-calibrating and clock-free.
- **Self is found by callsign.** The board has no GPS, so its own position is the
  owner phone's member entry; `CONVOY_WIFI_CALLSIGN` picks it out. Everyone else
  renders as a car. If the callsign is absent from the room the radar still shows
  everyone else, with no ME fix.
- **Colours come from the roster,** not from a board-side palette index, so a car
  is the same colour on the phone map and on the device radar. The
  `CONVOY_WIFI_HUES` palette is only a fallback for a member with no colour.
- **Parsing lives in `convoy_roster.h`** — pure, Arduino-free, host-tested in
  `test/test_ota.cpp` (62 assertions green via `test/run_tests.sh`). Member ids
  are random so keys cannot be matched by name; `ts` is ms-since-epoch and needs
  double precision; a free-text `name` may contain braces, so strings are skipped
  wholesale rather than brace-counted.

Also fixed here: `wifi_connect_saved_core()` now **scans first and attempts only
in-range SSIDs, strongest first**, falling back to trying everything if the scan
fails or sees none of them (a hidden SSID never appears in a scan). This removed
the 15s dead wait on out-of-range home WiFi — which was costing OTA too, not just
convoy.

### Phase 2 — ✅ PROVEN ON HARDWARE 2026-07-30
Three sources in the picker (MESHTASTIC / CONVOY / USE PHONE), replacing the
`CONVOY_WIFI_SPIKE` compile flag that had left both BLE sources unreachable.

Three **role classes** now, not three sources: NimBLE central (scan/mesh), NimBLE
peripheral (phone), WiFi STA (cloud). `convoy_radio_renegotiate()` re-runs the
OBD handover when a class change also changes which radio we hold, since the ack
means "WiFi is off" for BLE but "STA left up" for WiFi. Order is strict — old
stack down, radio changes hands, new stack up — because the BT controller cannot
get its buffers while the WiFi stack is up.

Both directions verified over repeated cycles:

```
[CVY] radio now WiFi (STA); internal=174868    → role -> 4, roster streaming
[CVY] radio now BLE (WiFi off); internal=172696
[CVY] BLE up (internal RAM 172696 -> 109732)   → my_node_num=0FD8C818
```

**A real leak fell out of this, and it corrects the Phase 3 claim below.**
`setReuse(true)` is what makes polling cheap — `end()` keeps the socket and its
mbedTLS context so the next request skips the ~47KB handshake — but on teardown
it stranded those 47KB of *internal* RAM, exactly what NimBLE needs. Each cloud
session walked the baseline down (174K → 130K → 127K) until switching back to
Meshtastic could no longer bring BLE up and the link stalled mid-connect.
`convoy_wifi_end()` now clears reuse and calls `stop()` explicitly; the baseline
then holds flat at ~172K across three cycles.

Measurement lesson worth keeping: the earlier "no leak" reading was taken
join-to-join *within one source*, which never ran the leaking teardown. Only
cycling two sources exposed it.

### Phase 2 (original plan) — `CVS_CLOUD` in the source picker and role switch
Add to `cvs_source_t` and `convoy_source_ui.h`. In `convoyLinkTask`
(`.ino:2114-2151`) the role-class test grows a third class: WiFi. Class change
BLE↔WiFi requires full teardown of whichever stack is up.

Split the OBD ack so `convoy_radio_mode` distinguishes:
- *release-for-BLE* → current behaviour, `WIFI_OFF`.
- *release-for-WiFi* → `client.stop()` + `WiFi.disconnect(true,true)`, stay in
  `WIFI_STA`, then ack. Never powers the radio down.

### Phase 3 — pairing ✅ PROVEN ON HARDWARE 2026-07-29
**Nothing is typed on the device, and no room code is entered anywhere.** The
first design here was a `/convoy` tab on the captive portal for room + callsign;
that was rejected as bad UX and it deserved to be — the room code changes with
every convoy, so the form would need revisiting constantly.

What shipped instead: the board announces itself to Firebase, the app lists
boards that are online, and you tap yours.

1. Board joins the hotspot on saved credentials, then PATCHes
   `devices/<id>` = `{name, ts}`. `ts` uses RTDB's `{".sv":"timestamp"}`
   sentinel — the board has no synced clock and could not write a truthful one,
   and a server stamp is what makes the app's freshness filter meaningful.
2. Unlinked, the Tracker shows the board's name (`Trailmaster-F6E8`, from the
   eFuse MAC) so the user knows which row to tap.
3. The app's Connect button lists devices with `ts` inside 2 minutes; picking one
   writes `{room, callsign}` into that node.
4. The board adopts it within ~2s, saves to `Preferences("hellojimny")`, and
   never asks again.
5. It keeps watching the node afterwards (lazily, 10s), so re-pointing the board
   at a new convoy is just picking it again — no device interaction at all.

Verified end to end by substituting curl for the button:

```
[CVW] joined 'Pixel10_shnk' as 'Trailmaster-F6E8' room=(unlinked) self=(unset)
[CVW] linked by app: room=AENP self=TM1
[CVW] roster: 4 member(s), 3 car(s), self=fix
```

and on re-entry, straight in with no pairing step:

```
[CVW] joined ... room=AENP self=TM1 internal=174400
```

**Why pairing cannot go over the LAN.** The obvious idea — board reads the room
from the phone's open browser session — is not possible in either direction. A
browser tab has no listening socket, so there is nothing for the board to
connect to; and the page reaching the board is the same mixed-content block as
everywhere else in this document. Firebase is the one channel both ends already
speak, so pairing rides on that.

The `/convoy` portal tab was still built and remains as a manual override.

**Memory across cycles — THIS CLAIM WAS WRONG; see Phase 2.** Measured
like-for-like at the join line over two Tracker cycles (175112 → 174400) it
looked clean, but both cycles used the same source, so the teardown that actually
leaked never ran. Cycling cloud ⇄ mesh exposed ~47KB of TLS session stranded per
cloud session. Fixed in Phase 2.

**Not yet deployed.** The Connect button, board list and link write are built and
render correctly, but the phone loads the app from GitHub Pages on `main`, so the
picker is unusable from a phone until `docs/convoy/` is pushed.

### Phase 4 — wake lock in the web app
~20 lines in `docs/convoy/app.js`: acquire `navigator.wakeLock` on join,
re-acquire on `visibilitychange`. Independent of the firmware work and shippable
immediately. Still needed for the owner's own GPS, and helps every passenger
phone on both platforms.

### Phase 5 — retire `convoy_net.h`
Once `CVS_CLOUD` is proven, delete the NimBLE **GATT server** path and the
`CVS_PHONE` source. Halves the NimBLE surface; the central/client role stays for
mesh.

## 5. Open decision — mesh transport

Deferred until after the Phase 0 spike (agreed 2026-07-29). T-Beams are
**car/USB powered**, so the battery objection to running Meshtastic over WiFi
does not apply. Remaining objections are Meshtastic's TCP API (port 4403) being
less battle-tested than its BLE API, and having to reconfigure both nodes.

The prize for going all-WiFi is large: NimBLE leaves the codebase entirely,
which retires backlog items 4 (role-switch lifecycle) and 5 (`ble_hs_stop`
teardown), and collapses source switching from a radio switch into a socket
switch. Off-grid it would mean the board runs softAP and the T-Beam joins as a
client — no phone required.

Revisit once the spike reports real heap numbers.

## 6. Risks

- **Heap under TLS + LVGL + PSRAM draw buffers.** Mitigated by OTA already doing
  HTTPS, but OTA runs with the convoy UI closed. Phase 0 measures it.
- **iOS hotspot 5GHz default** — see Phase 0.
- **iOS hotspot idles off** with no client attached (~90s), but the board stays
  attached, so this should not bite.
- **RTDB auth.** Rules are still open with the room code as shared secret. A
  board reading over REST needs no auth today; publishing rules would require a
  token story for the board. Track separately.
