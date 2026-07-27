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
    }
  }
}
```

> MVP-grade: anyone with a room code can read/write that room. To harden later,
> add Firebase **Anonymous Auth** and gate `.write` on `auth != null` +
> `$mid === auth.uid`, and rotate codes. Fine for a private convoy to start.

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
