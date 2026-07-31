# Traccar → Firebase relay

A ~100-line Cloudflare Worker that lets a phone report its position to the
convoy **from the background**, with the screen off and the phone in a pocket.

## Why

The convoy web app both *reports* position and *displays* the convoy, so it has
to stay awake to do the first job. A locked phone stops running it — exactly
what happens on a drive. Nothing on the web platform fixes this: service workers
cannot reach `navigator.geolocation`, and iOS suspends the tab outright.

[Traccar Client](https://www.traccar.org/client/) (free, open source, App Store
+ Play Store) does background location properly on both platforms. This Worker
is the only missing piece — it translates what Traccar sends into the member
shape the convoy already uses.

**Nothing else changes.** The board keeps reading `convoys/<room>/members` as it
always has, and the web app becomes a pure viewer you can open and close freely.

## Deploy

```sh
cd relay
npx wrangler login
npx wrangler secret put RELAY_TOKEN     # invent a token; paste when prompted
npx wrangler deploy
```

Wrangler prints the URL, e.g. `https://trailmaster-relay.<you>.workers.dev`.

## Set up a phone

Install **Traccar Client**, then set exactly two fields:

| Field | Value |
|---|---|
| Server URL | `https://trailmaster-relay.<you>.workers.dev/<ROOM>/<TOKEN>` |
| Device identifier | the driver's callsign — **5 characters max** |

`<ROOM>` is the convoy room code (e.g. `AENP`). Set frequency to ~10 s and turn
the service on. That's it — no account, no login.

The 5-character cap is not arbitrary: the published database rules reject a
longer callsign. The Worker truncates, but it is clearer to type it short.

## Tests

```sh
cd relay && node test.mjs
```

Runs the Worker's request handling against a stubbed Firebase — nothing is
written. Covers token rejection, both Traccar payload formats, the knots→m/s
conversion, seconds-vs-milliseconds timestamps, callsign sanitising, no-fix
pings, and Firebase errors surfacing as 502 rather than a bare 500.

## Check it works

```sh
# Simulate a phone
curl "https://trailmaster-relay.<you>.workers.dev/AENP/<TOKEN>?id=TM1&lat=12.9716&lon=77.5946&timestamp=$(date +%s)&speed=10&bearing=90"

# See what landed
curl "https://trailmaster-e43b1-default-rtdb.asia-southeast1.firebasedatabase.app/convoys/AENP/members.json"
```

Add `&debug=1` to any request to have the Worker echo what it parsed instead of
guessing. For a POST it echoes the raw body — the cheapest way to learn a new
Traccar version's payload shape if the format ever changes.

## OwnTracks shows the convoy on its own map — including on iOS

In HTTP mode the app never polls, so the response to its position POST is the
only chance to hand it anything. This Worker replies with the rest of the convoy
as `_type: location` + `_type: card` objects, which the app renders as **friends**
on its own map. One app then both reports and displays the convoy.

**Verified on a real iPhone 2026-07-31.** This is worth recording because
[owntracks/ios#478](https://github.com/owntracks/ios/issues/478) reads as though
friends-over-HTTP is an unimplemented request on iOS. It is not — the booklet is
right and the issue is misleading. A friend published through this relay appeared
in the iOS Friends tab.

Friends only arrive in response to a publish, so nothing shows up until the app
sends something. Force a publish if the list looks empty.

## Gotchas

- **Don't join the same convoy in the web app while testing.** The app creates
  its own member under a random id, so you would appear twice — once from
  Traccar, once from the browser. Use the web app as a viewer only, or test with
  a different callsign.
- **What Traccar Client actually sends**, captured from a real phone, is the
  OsmAnd fields **form-encoded in the POST body** — not query parameters, not
  JSON:

  ```
  id=TM1&lat=12.936833&lon=77.6992084&timestamp=1785430293&accuracy=68.5&batt=47
  ```

  The Worker also accepts query parameters and the nested JSON payload
  (`{device_id, location:{coords:{...}}}`) that newer builds and Home Assistant
  document, since which one you get depends on version.
- **Speed units differ by format.** The OsmAnd fields send knots; the nested JSON
  sends m/s. The Worker normalises to m/s, which is what the board's movement
  threshold expects. Getting this wrong makes a parked car look like it is doing
  15 km/h.
- **Timestamps come in three shapes**: epoch seconds, epoch milliseconds, and ISO
  8601. `parseFloat("2026-07-30T07:32:51Z")` returns `2013`, which becomes a 1970
  date and marks the member permanently offline — so they are parsed explicitly.
- **Deploys take a few seconds to propagate.** A request immediately after
  `wrangler deploy` can still hit the old version, which looks exactly like the
  fix not working.
- **Timestamps come from the device, not arrival time.** Traccar buffers fixes
  while offline and replays them later; using arrival time would make a whole
  backlog look freshly current.
- **The token is the only thing protecting the endpoint.** There is no auth
  beyond it. Treat the URL as a secret, and rotate with
  `wrangler secret put RELAY_TOKEN` if it leaks.

## Reverting

Delete the Worker (`npx wrangler delete`) and stop using the URL. Nothing in the
firmware or the web app depends on this — remove this directory and the project
is exactly as it was.
