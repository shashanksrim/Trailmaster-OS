// ─────────────────────────────────────────────────────────────────────────────
// Firebase config for the Trailmaster Convoy web app.
//
// SETUP (one time, ~3 min):
//   1. Go to https://console.firebase.google.com  →  Add project.
//   2. Build → Realtime Database → Create Database → start in "test mode"
//      (we'll tighten the rules later — see README.md).
//   3. Project settings (gear) → General → Your apps → Web (</>) → register app.
//   4. Copy the `firebaseConfig` values it shows you into the object below.
//
// These values are NOT secret for a client web app — anyone can read them in the
// browser. Access is controlled by Realtime Database SECURITY RULES, not by
// hiding this config. See README.md for the rules to paste.
// ─────────────────────────────────────────────────────────────────────────────

export const firebaseConfig = {
  apiKey:      "AIzaSyDxLEon51wet2zagr1FvJYlq1ypJtLqUFI",
  authDomain:  "trailmaster-e43b1.firebaseapp.com",
  databaseURL: "https://trailmaster-e43b1-default-rtdb.asia-southeast1.firebasedatabase.app",
  projectId:   "trailmaster-e43b1",
  appId:       "1:596252336039:web:7c7da42df859301fd62082",
};

// How often (ms) each phone pushes its position to the room. 4 s matches the
// Meshtastic broadcast cadence and is easy on battery + Firebase quota.
export const PUSH_INTERVAL_MS = 4000;

// A member is considered "online" if we've heard from them within this window.
// 120 s matches convoy_ui.h's online window on the Trailmaster board.
export const ONLINE_WINDOW_MS = 120000;

// A member we haven't heard from for this long is dropped from the map/roster.
// Between ONLINE_WINDOW_MS and here they show greyed ("offline") — so a phone
// that briefly locks or loses signal fades rather than vanishing instantly.
export const STALE_DROP_MS = 600000;   // 10 min

// ── Background tracking relay (see repo relay/) ──────────────────────────────
// A phone that locks stops running this page, which stops watchPosition. The
// OwnTracks app reports from the background instead, posting to this Worker,
// which writes into the same convoy room. Reporting is then independent of this
// page being open at all.
//
// The URL has NO room in it on purpose: the room is looked up per callsign from
// assign/<callsign>, which this app writes when you join. That is what lets a
// driver paste the URL once and never touch it again, even though the room code
// changes every convoy.
//
// The token is a shared secret — without it the endpoint would be world-
// writable into your convoys. Rotate with: wrangler secret put RELAY_TOKEN
export const RELAY_URL   = "https://trailmaster-relay.shashank-srim.workers.dev";
export const RELAY_TOKEN = "fed848d5c27743acc8a3";
