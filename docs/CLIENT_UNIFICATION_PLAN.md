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

1. **Shrink the board portal to provisioning only.** Remove the `/convoy` and
   `/photos` routes and their tab bars from `PhotoFrameApp.cpp`; `/wifi` becomes
   the whole page. Add a post-save success state showing the cloud URL + QR.
   Frees flash rather than costing it.
2. **Room/callsign move to the PWA settings drawer**, written to
   `/devices/<id>`, which the board already reads.
3. **PWA gains a settings drawer + two tabs.** Convoy tab is largely today's
   `docs/convoy/app.js`; Images tab is new.
4. **Photos via the cloud manifest** (`ota_sync_photos()` already exists) rather
   than AP upload. Keep `/upload` as an offline fallback, stop advertising it.
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
