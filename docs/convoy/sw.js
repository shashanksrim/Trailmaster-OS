// Minimal, safe service worker: caches the app shell so it installs as a PWA and
// loads instantly. Same-origin = network-first (fall back to cache offline);
// cross-origin (Firebase, map tiles, CDN) always goes straight to the network.
const CACHE = "cvy-v2";
const SHELL = [
  "./", "./index.html", "./app.js", "./config.js",
  "./radar.svg", "./manifest.webmanifest",
];

self.addEventListener("install", (e) => {
  e.waitUntil(caches.open(CACHE).then((c) => c.addAll(SHELL)).then(() => self.skipWaiting()));
});
self.addEventListener("activate", (e) => {
  e.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.filter((k) => k !== CACHE).map((k) => caches.delete(k))))
      .then(() => self.clients.claim())
  );
});
self.addEventListener("fetch", (e) => {
  const url = new URL(e.request.url);
  if (url.origin !== location.origin) return;   // let Firebase/tiles/CDN hit network
  // `no-cache` = always revalidate with the server, not "never cache". Without
  // it this is network-first in name only: GitHub Pages sends max-age=600, so
  // fetch() is answered from the browser's own HTTP cache and a fresh deploy
  // stays invisible for ten minutes. Revalidation costs a 304 when unchanged.
  e.respondWith(
    fetch(e.request, { cache: "no-cache" })
      .then((r) => {
        const copy = r.clone();
        caches.open(CACHE).then((c) => c.put(e.request, copy)).catch(() => {});
        return r;
      })
      .catch(() => caches.match(e.request))
  );
});
