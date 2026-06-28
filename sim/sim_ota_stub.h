#pragma once
// Hardware-free fake implementation of OTAManager's public API, so the real,
// shared ota_overlay_ui.h can drive it in the simulator. Uses the REAL
// OTAState/OTAStatus types from OTAManager.h (compiled with LV_SIM_BUILD
// defined, which skips its Arduino.h include) — not a parallel redefinition.
#define LV_SIM_BUILD
#include "OTAManager.h"
