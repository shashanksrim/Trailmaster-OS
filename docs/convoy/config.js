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
