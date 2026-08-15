# One Trailmaster client — unification plan

## Why

There are two web clients today with overlapping jobs, and users have to know
which is which and remember two URLs:

| | served from | does |
|---|---|---|
| Board portal (`PhotoFrameApp.cpp`, inline `R"rawliteral"` in flash) | AP `192.168.4.1` | tabs: Wi-Fi, Convoy, Photos |
| Cloud PWA (`docs/convoy/`) | GitHub Pages | live convoy map, phone GPS relay |

Goal: one page the user thinks about, reached by the QR on the device.

## The two constraints that shape the design

**1. A phone cannot be on the board's AP and the internet at the same time.**
The two states are physically exclusive, not a UI preference:

- No Wi-Fi creds yet -> phone must join the board AP -> no internet -> Firebase
  unreachable -> convoy and cloud photos genuinely CANNOT work.
- Creds synced -> board joins the network and talks to Firebase itself -> phone
  returns to the internet -> PWA works, and the board no longer needs the phone.

So convoy/images are hidden before provisioning because they cannot function,
not merely to reduce clutter.

**2. The PWA bundle cannot ship on the board.** ~100 KB of OTA-partition
headroom (see the flash-ceiling note), and the board's pages are compiled-in
string literals. "One page" therefore means one page the USER thinks about, not
one artifact.

## The unlock

`/devices/<id>` in Firebase is already a mailbox the app writes and the board
polls (`convoy_wifi_pair_tick()` in `convoy_wifi.h`) — room and callsign already
flow that way. Extending that pattern lets the PWA configure the board with no
direct connection: no mDNS, no LAN discovery, no router client-isolation issues.

NOTE: this depends on the read-reconcile-write ordering fix made 2026-08-12 —
the announce must happen AFTER the read, or a local edit is destroyed before it
is ever seen.

## Shape

```
STATE 1  UNPROVISIONED          STATE 2  PROVISIONED
phone -> board AP               phone -> internet
+------------------+            +--------------------------+
| 192.168.4.1      |            |  Trailmaster PWA    [gear]|
|                  |  ------>   |  +--------+--------+      |
|  Wi-Fi setup     |  hands     |  | Convoy | Images |      |
|  (only thing     |  off to    |  +--------+--------+      |
|   that can work) |  cloud URL |  gear = callsign, room,   |
|                  |            |         Wi-Fi, pairing    |
|  saved -> shows  |            |         (all via Firebase)|
|  cloud URL + QR  |            +--------------------------+
+------------------+                        |
        ^                                   |
        +---- board can't connect ----------+
              (moved network) -> AP returns
```

ONE QR on the device, always pointing at the board portal. After creds save,
that page hands off to the cloud URL with a link and a second QR. The user only
ever scans what is on the screen.

## Work, in dependency order

1. ~~**Shrink the board portal to provisioning only.**~~ DONE 2026-08-14.
   `/convoy` and `/convoy/save` are gone, both tab bars are gone, and `/wifi` is
   the whole portal. Saving a network now redirects to `/wifi?ok=<ssid>`, which
   renders a handoff card: the cloud URL, a QR for it, and a lead line telling
   the user to leave the AP first (the link cannot load until they do). The QR
   is a 37x37 1-bit PNG baked into flash as base64 (`CLOUD_QR_B64`) and drawn at
   222px with `image-rendering:pixelated` — no encoder on the board, and no
   round trip to a QR service the phone could not reach anyway. Net flash:
   **-5,040 bytes** (3,635,663 -> 3,630,623), so it did pay for itself.

   Deviation: `/photos` and `/upload` were KEPT, reachable only from a small
   footer link on `/wifi`. Deleting the page now would break AP photo upload
   outright, and step 4 below is what actually replaces it — "stop advertising
   it" is already true, "delete it" waits for the cloud manifest to land.
2. **Room/callsign move to the PWA settings drawer**, written to
   `/devices/<id>`, which the board already reads.
3. ~~**PWA gains a settings drawer + two tabs.**~~ DONE 2026-08-15 — as a bottom
   TAB BAR (Convoy | Wi-Fi | Images) rather than a drawer, since two of the three
   are full screens rather than settings. `#wifi` / `#img` deep-link to a tab.
   Panels sit at z-index 1100 to clear Leaflet's own controls at 1000.
4. ~~**Photos via the cloud manifest**~~ DONE 2026-08-15. The Images tab crops to
   the round 466 px screen on canvas, converts to raw little-endian RGB565 (the
   board has no image decoder), uploads the `.bin` to Firebase **Storage**, and
   appends `{path,url}` to `photos/files` — which `ota_sync_photos()` already
   reads. `/upload` survives unadvertised. **Storage is a separate Firebase
   product and is not enabled by default**; until it is, the tab reports the
   rejection instead of failing silently. See `docs/convoy/README.md`.

   Also done: **Wi-Fi from the app**, which the "Open decisions" below argued
   against. It is implemented with the exposure narrowed rather than removed —
   the app writes `wssid`/`wpass` into `devices/<id>`, and the board deletes both
   in the same poll that saves them (`convoy_wifi_pair_tick`), so credentials
   live in a world-readable node for seconds rather than indefinitely. The tab
   says so on screen. The board's own hotspot remains the private path.
5. **Phone-GPS relay becomes an explicit toggle** in the Convoy tab. No firmware
   change needed — the board already prefers its own fix and falls back to
   roster entries (`gps_convoy.h`).

## Open decisions

- **Wi-Fi management from the PWA**: adding networks via Firebase would put
  credentials through a world-writable database (`/devices` has no auth today).
  Recommendation: keep credential ENTRY on the AP portal; the settings drawer
  only DISPLAYS saved networks.
- **Should the board keep serving its AP once provisioned**, so re-provisioning
  does not need a factory reset?
