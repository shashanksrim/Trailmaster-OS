# Trailmaster Convoy — web app (Phase 0)

A phone web app / PWA: drivers join a **convoy room** by code, share GPS through
Firebase, and see each other live on a map. This is the network (online) convoy
path that complements the Meshtastic LoRa setup — see `../../CONVOY_NETWORK_PLAN.md`.

**Live URL (after deploy):** `https://shashanksrim.github.io/Trailmaster-OS/convoy/`

Phase 0 is phone-only. Phase 1 relays the same roster into the Trailmaster board
over Web Bluetooth (Android) → the on-device radar.

---

## One-time Firebase setup (~3 min)

1. Go to <https://console.firebase.google.com> → **Add project** (any name, you can
   disable Analytics).
2. **Build → Realtime Database → Create Database.** Pick a region, start in
   **test mode** (we replace the rules below).
3. **Project settings** (gear icon) → **General** → scroll to *Your apps* →
   **Web (`</>`)** → register an app (nickname "convoy", no hosting needed).
4. Copy the shown `firebaseConfig` values into **`config.js`** (replace every
   `PASTE_ME`). `databaseURL` must be the one ending in `...firebaseio.com`.

### Security rules

In **Realtime Database → Rules**, paste this and Publish. It scopes writes to a
member's own node, validates the shape, and keeps positions readable only within
a room (the room code is the shared secret):

```json
{
  "rules": {
    "convoys": {
      "$code": {
        "members": {
          ".read": true,
          "$mid": {
            ".write": true,
            ".validate": "newData.hasChildren(['callsign','ts']) && newData.child('callsign').val().length <= 5"
          }
        }
      }
    },
    "devices": {
      ".read": true,
      "$id": {
        ".write": true,
        ".validate": "$id.length <= 24",
        "name":     { ".validate": "newData.isString() && newData.val().length <= 24" },
        "ts":       { ".validate": "newData.isNumber()" },
        "room":     { ".validate": "newData.isString() && newData.val().length <= 15" },
        "callsign": { ".validate": "newData.isString() && newData.val().length <= 11" },
        "$other":   { ".validate": false }
      }
    }
  }
}
```

**`devices` is not optional — omit it and board pairing breaks.** Realtime
Database rules are deny-by-default, so a rule set that only mentions `convoys`
denies everything else. An earlier version of this file did exactly that, which
would have silently killed the Connect button.

Note that `.write` is granted at `$code`/`$id`, never at the `convoys` or
`devices` root, so no one can wipe a whole collection in a single write. The
`$other` rule rejects unknown fields, so the database cannot be used as free
storage.

> **MVP-grade, and worth being honest about the limits.** There is no auth, so
> these rules constrain the *shape* of writes, not *who* makes them: anyone with
> a room code can read and write that room, and anyone can list `devices` and
> assign a room to a board that is currently listed.
>
> The board bounds that exposure itself rather than relying on these rules — it
> only publishes `devices/<id>` for a few minutes after you open the Tracker,
> and deletes the node when the window closes or you leave (see
> `CONVOY_WIFI_PAIR_WINDOW_MS` in `convoy_wifi.h`). So an idle board is not
> listed and cannot be re-pointed; an attacker has to be writing during the same
> window you are pairing in.
>
> The real fix is Firebase **Anonymous Auth**, gating `.write` on `auth != null`,
> scoping `$mid === auth.uid`, and recording an owner uid on each device so only
> that uid can assign it a room. Worth doing before this is used by anyone
> outside a private group.

---

## See it without any setup (demo mode)

Add `?demo` to the URL, tap **Try a demo (no setup)** on the join screen, or the
demo link on the setup overlay. It shows the map with mock cars drifting around —
no Firebase, no login. Uses your real GPS for "you" if you allow location, else a
default centre. Good for checking the map works before wiring Firebase.

## Run locally

Any static server works (a plain `file://` open will fail on ES modules + geo):

```sh
cd docs/convoy
python3 -m http.server 8000
# open http://localhost:8000  (geolocation works on localhost)
```

## Deploy

It's already under `docs/`, which GitHub Pages serves from `main`. Commit +
push and it's live at the URL above. Share that link with the convoy.

---

## Phase 1 — relay onto the Trailmaster screen (BLE)

The board runs as a BLE **peripheral** (`convoy_net.h`, firmware); the phone (this
app, Chrome/Android) connects over **Web Bluetooth** and pushes the roster so the
convoy appears on the device's own radar. The connected phone is the board's own
car. Web Bluetooth needs a secure context — use the deployed HTTPS URL (not
`file://`).

- Tap the **Bluetooth button** (top of the FAB stack) → pick "Trailmaster" → the
  button turns cyan when connected; the app pushes the roster ~1×/s.
- Board: open the **Tracker** screen (it powers WiFi off and advertises as
  "Trailmaster"); it shows **PAIR PHONE** until the app connects, then the radar.
- Wire format the board parses (`convoy_net.h`): `S,lat,lon,hdg,spd,fix` (self) +
  `C,callsign,lat,lon,online` per other car. UUIDs are shared between `app.js`
  and `convoy_net.h`.
- iPhones (no Web Bluetooth) stay map/radar participants; only the board owner's
  phone needs the relay.

## Two views

- **Map** — Leaflet map with everyone as markers (default).
- **Radar** — the same scope the Trailmaster device shows (`convoy_ui.h`): ME
  pulsing at centre, other cars by true bearing + distance, range rings, N/E/S/W,
  heading-up/north-up. Tap the **⊙ radar button** (above the locate button, lower
  right) to switch; tap the heading pill to flip north-up ⇄ heading-up.
- The **bottom sheet** lists the convoy; tap it to expand when there are more cars.

## How it works

- Identity (`member id`, name, callsign) lives in `localStorage` — no login.
- Each phone writes only its own node under `convoys/{code}/members/{id}` and
  listens to the whole `members` subtree (Firebase realtime fan-out).
- Position is pushed every `PUSH_INTERVAL_MS` (4 s) from
  `geolocation.watchPosition`; a heartbeat refreshes `ts` when parked.
- "Online" = heard within `ONLINE_WINDOW_MS` (120 s, matches the board).
- `onDisconnect().remove()` drops you from the room when the tab closes.
- Colors mirror `convoy_ui.h` so the phone map and the board radar agree.

## Files

| File | Purpose |
|---|---|
| `index.html` | Join screen + map screen + setup overlay |
| `app.js` | Firebase + geolocation + Leaflet map/roster |
| `config.js` | **Your** Firebase config + tunables |
| `manifest.webmanifest`, `sw.js`, `radar.svg` | PWA (installable, app-shell cache) |
