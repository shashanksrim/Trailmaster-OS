#pragma once
// L2: fake FFat (internal flash filesystem) — the emulator doesn't need a
// second filesystem distinct from /sd_card; begin() just succeeds.
class FFatClass {
public:
    bool begin(bool /*formatOnFail*/ = false) { return true; }
};
extern FFatClass FFat;
