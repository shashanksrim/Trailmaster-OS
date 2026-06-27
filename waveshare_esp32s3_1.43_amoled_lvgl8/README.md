# Surface Level Project with Waveshare ESP32-S3 1.43inch AMOLED Touch Display
<a href="https://www.buymeacoffee.com/thelastoutpostworkshop" target="_blank">
<img src="https://www.buymeacoffee.com/assets/img/custom_images/orange_img.png" alt="Buy Me A Coffee">
</a>

## Youtube Tutorial
[<img src="https://github.com/thelastoutpostworkshop/images/blob/main/Surface%20Level-1.png" width="500">](https://youtu.be/HT6sG39-vrk)

## Project Description
This project is a ready-to-run digital surface level example for the Waveshare ESP32-S3 1.43 inch AMOLED touch display using LVGL 9. It combines the onboard AMOLED screen, capacitive touch controller, and QMI8658 6-axis IMU to create a responsive level interface with a moving bubble, angle readouts, and a target indicator when the device is close to level.

The repository also includes the generated SquareLine Studio UI source, making it a useful starting point both for flashing the included demo and for building your own touch-based LVGL interface on this board.

## Features
- LVGL 9 based project structure configured for the Waveshare ESP32-S3 Touch AMOLED 1.43 board
- Built-in surface level demo that reads accelerometer data from the QMI8658 IMU
- Real-time bubble movement based on board orientation
- On-screen angle labels for the current level position
- Visual target indicator that changes state when the board is close to level
- Capacitive touch support integrated with LVGL input handling
- Included SquareLine Studio project and generated UI source files for further editing
- Display driver setup, LVGL buffer allocation, and rendering callbacks already wired up

## What You Can Customize
- Replace the included `ui_init()` interface with your own SquareLine Studio or hand-built LVGL UI
- Change the bubble behavior by tuning smoothing, movement scaling, and update timing in the sketch
- Adjust the level detection threshold with `TARGET_THRESHOLD_PX`
- Change IMU sampling and UI refresh timing with `READ_SAMPLE_INTERVAL_MS` and `MOVE_BUBBLE_INTERVAL_MS`
- Swap or invert axes to match your preferred board orientation using `swap_axes`, `inv_pitch`, and `inv_roll`
- Modify labels, graphics, and screens using the included SquareLine Studio project in `Squareline_studio_project`
- Use the project as a hardware/software base for other AMOLED touch applications on the same board

## Trailmaster Version History
- **Trailmaster v2.3.24** (Current Milestone):
  - Injected `"Trailmaster v2.3.24"` branding dynamically to launcher menu version label.
  - Increased image frame long press menu text size by **20%** using premium Montserrat 20.
  - Resolved long press menu accidental triggering using input device reset (`lv_indev_wait_release`) and a C++ safety debounce timer (300ms guard).
  - Cleaned up preprocessor swap warnings in `lv_conf.h`.
  - **Critical PSRAM Cache Sync Fix**: Eliminated the initial "TV Static Noise" bug when loading `.bin` images by enforcing 64-byte aligned memory allocation (`heap_caps_aligned_alloc`) and explicit memory-to-cache invalidation (`esp_cache_msync`) after SD card DMA transfers.
  - **Memory Pre-initialization**: Enforced `memset` zeroing of PSRAM buffers prior to file reads to ensure black screens instead of random static if an SD card read fails or is partial during intense boot IO.
  - **Client-Side Image Uploader Restored**: Restored the beautiful HTML5 Canvas image cropping and compression tool directly on the ESP32 web server (`index_html`), allowing phones to resize and convert standard images to RGB565 binary on-the-fly without an external service.
  - **QR Code & Typography Scaling**: Magnified the WiFi setup QR code size and dynamically adjusted IP address font hierarchies for significantly improved legibility.
  - **Consecutive Deletion & Index Translation Crash Fixed**: Resolved the C-side file system crash caused by out-of-bounds memory deletion by routing `unlink()` to the translated `get_image_index()` array mapping.
  - **Fluid Delete Snapping Transition**: Engineered instant snapped layout recreation, dot highlights, and rolling cache loading upon photo deletion so the next file dynamically scrolls into place instead of hanging.
  - **Black Screen / Page Layout Synchronization**: Solved the black screen bug on entering by calling `lv_obj_update_layout(pf_pages)` synchronously to force immediate layout calculation before attempting the horizontal scroll offset snapping.
  - **Scrolling Container Event Bindings**: Resolved swallowed touches (gestures and long press) by attaching both the exit-gesture and the deletion-dialog callbacks directly to `pf_pages` instead of the base screen, yielding flawless touch responses.
  - **Splash Screen Boot-Loop Fix**: Safe-guarded the boot process by removing the experimental asynchronous screen deletion timer, avoiding heap/timer linked-list collision and rendering extremely reliable boot animations.
  - **Dynamic Upload Snapping Rebuild**: Purged the legacy uploader loop check that competed with scrolling layouts and caused scrambling. Now, the new image upload is 100% silent and does not affect the view. The instant the user initiates a swipe to *leave* the QR code settings screen, the system detects the new file, rebuilds the snapped layouts silently, and scrolls seamlessly to show the new image with fluid responsiveness.
  - **Target Image Cache Invalidation**: Eliminated the split-second "first image flash" bug when uploading or swiping by replacing standard `lv_img_cache_invalidate_src(NULL)` (which did not clear custom buffers in LVGL v8) with targeted `lv_img_cache_invalidate_src(&pf_dscs[buf_idx])` calls, forcing LVGL to immediately eject and redraw the updated descriptor.
