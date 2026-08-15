# Can the "Enable Wi-Fi" toggle be removed? — investigation

**Status: investigation only. No behaviour changed.** Written 2026-07-31.

The toggle on the Wi-Fi/QR overlay costs a screen row that the onboarding QR
wants, and costs the user a tap with no decision behind it. It exists as a
workaround for a display bug. This is an attempt to establish whether that bug
still exists before touching anything.

## What the toggle is working around

Launching the AP automatically — from the photo screen or the settings screen —
left the display garbled *after the overlay was closed*. The workaround was to
never start the AP on open, and make the user ask for it:

```c
// start_photoframe_wifi(); // Deferred until user toggles switch to avoid LVGL rendering collision
```

Note the stated theory in that comment: "LVGL rendering collision". That theory
turns out to be wrong, which matters, because it is what makes the toggle look
irreplaceable.

## Root cause — already found, and already fixed

The garble was **not** a collision between the radio and the display. It was a
**dropped panel flush**, documented in `amoled.cpp` `pushToPanel()`:

> Sending the whole area in one shot is what broke full-screen redraws: a 466x466
> frame is 434,312 bytes against a max_transfer_sz of TRANSFER_SIZE (4092), and
> the driver answered ESP_ERR_NO_MEM because it cannot build a DMA descriptor
> chain that long for a PSRAM source. The flush was then dropped and the panel
> silently kept the previous frame — **which is what the "garble" actually was.**

So the panel was not corrupted. It was showing a *stale frame*, because the
refresh never reached it.

### Why closing the overlay is exactly the trigger

`pf_close_upload_overlay()` deletes the overlay and forces a full redraw, and
`pf_apply_carousel_refresh_after_upload()` → `pf_invalidate_full_screen()` sets
`full_refresh = 1` and calls `lv_refr_now()`. That is a **full 466x466 frame** —
precisely the case that exceeded `max_transfer_sz` and got dropped.

### Why Wi-Fi correlated with it

DMA descriptors for a PSRAM source are allocated from **internal** RAM. Bringing
up the AP consumes internal RAM, so the oversized descriptor chain failed more
often with Wi-Fi running than without. Hence the appearance that the radio caused
it. Wi-Fi did not corrupt anything; it made an already-too-large allocation
likelier to fail.

That also explains why the toggle "fixed" it: a user who opened the overlay,
looked at the QR and closed it without enabling Wi-Fi never had the memory
pressure, so never saw the drop.

### The fix

`47b053b` ("Convoy: fix dropped panel flushes") changed `pushToPanel()` to split
transfers into row bands that fit `TRANSFER_SIZE`. Each transfer is now ≤4092
bytes with a short descriptor chain, which should succeed regardless of Wi-Fi
memory pressure.

**The toggle (`2a3237a`) predates that fix by a long way.** The workaround
outlived its cause because the fix landed during unrelated convoy work and nobody
revisited this screen.

## Why this is still not enough to just delete it

Two gaps between "root cause fixed" and "safe to remove":

1. **The fix was never tested on this path.** It was made and verified for the
   convoy radar. Nothing has exercised close-the-overlay-with-AP-running since.
2. **Blocking is a separate concern.** `start_photoframe_wifi()` contains
   `delay(500)` + `delay(100)` before the AP even starts, and runs on the LVGL
   task. The 400ms "deferred start" timer does **not** move it off that task —
   `lv_timer` callbacks run inside `lv_timer_handler()`. So auto-starting stalls
   the UI for ~600ms+ at open. That is a responsiveness problem, not a
   correctness one, but it is real and it is not what the deferral claims to fix.

## Verification protocol — needs NO code change

The toggle itself reproduces the exact failing condition, so the hypothesis can
be tested before changing anything:

1. Open the Wi-Fi overlay **from the photo screen**. Turn the toggle ON. Wait for
   the AP. Close the overlay. Inspect: does the carousel redraw cleanly?
2. Repeat **from the settings screen** — the second reported entry point.
3. Repeat each 3-5 times, including closing quickly after enabling.

- **Clean every time** → the banding fix covers this path, the toggle is
  vestigial, removal is safe.
- **Any stale/garbled frame** → something else is going on; stop, keep the
  toggle, investigate further.

### Optional instrumentation, if a naked-eye check feels too soft

`pushToPanel()` currently returns `false` on a failed transfer and **nobody logs
it**. A one-line `Serial.printf` on the `esp_lcd_panel_draw_bitmap() != ESP_OK`
branch converts "did it look wrong?" into a definite signal. Worth adding for the
test even if it comes out again afterwards.

## Plan, if verification passes

1. Remove the toggle row (label + switch + handler).
2. Start the AP on overlay open using the existing 400ms deferred timer — the
   same path first-run onboarding already uses.
3. **Move the `delay(500)`/`delay(100)` out of the LVGL task**, or drop them.
   They exist to let the OBD worker finish its Wi-Fi cycle; that handshake now
   has a better mechanism in `convoy_radio_mode`/`convoy_obd_released`. Blocking
   the UI for 600ms on every open is worth avoiding independently of this change.
4. Regenerate the QR at 7px modules (259x259) into the freed space — larger than
   the 6.86px original that scanned.
5. Teardown is unchanged: `pf_close_upload_overlay()` already calls
   `stop_photoframe_wifi()` unconditionally, so the toggle only ever gated
   starting.

## If verification fails

Keep the toggle. The QR stays at 222x222 (29 modules @6px + quiet zone), which is
the largest that fits above the row and is confirmed to decode. Record the
failure here so the next person does not re-litigate it from scratch.
