#pragma once

#include <stdint.h>
struct android_app;

namespace mobile_display {

class FramePacer {
public:
    FramePacer() = default;

    // Synchronizes presentation with an absolute monotonic sleep deadline.
    void sync(android_app* app, int targetFps, float gameSpeed);

    // Checks if a high refresh rate presentation slot is available for catch-up
    bool shouldPresentSlot(int targetFps, float gameSpeed, int64_t nowNs) const;

    // Resets the pacing timestamp
    void reset();

    // Returns the timestamp of the last presented frame in nanoseconds
    int64_t lastPresentNs() const { return mLastPresentNs; }

private:
    int64_t mLastPresentNs = 0;
};

}  // namespace mobile_display
