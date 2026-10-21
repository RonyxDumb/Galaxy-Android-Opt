#include "FramePacer.h"
#include <errno.h>
#include <time.h>
#include "port/port.h"

namespace mobile_display {

void FramePacer::sync(android_app* app, int targetFps, float gameSpeed) {
    (void)app;
    (void)gameSpeed;
    if (targetFps < 30) targetFps = 30;
    if (targetFps > 144) targetFps = 144;

    // Presentation cadence is locked to display target refresh rate.
    // Game simulation speed (gameSpeed multiplier) drives physics/tick rates,
    // NOT the display presentation cadence.
    int effectiveFps = targetFps;
    int64_t framePeriodNs = 1000000000ll / (effectiveFps > 0 ? effectiveFps : 60);

    int64_t nowNs = port_host_time_ns();
    if (mLastPresentNs == 0 || nowNs < mLastPresentNs || (nowNs - mLastPresentNs > framePeriodNs * 2)) {
        mLastPresentNs = nowNs;
        return;
    }

    const int64_t targetNs = mLastPresentNs + framePeriodNs;
    if (nowNs < targetNs) {
        // Sleep until an absolute CLOCK_MONOTONIC deadline. In particular,
        // avoid the former sched_yield / CPU busy-spin loop: keeping an A34
        // efficiency core active just to wait for a frame wastes power and
        // raises sustained thermal pressure on Mali devices.
        struct timespec deadline{};
        deadline.tv_sec = (time_t)(targetNs / 1000000000ll);
        deadline.tv_nsec = (long)(targetNs % 1000000000ll);
        const int64_t waitStartNs = nowNs;
        int status;
        do {
            status = clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &deadline, nullptr);
        } while (status == EINTR);
        const int64_t endNs = port_host_time_ns();
        if (endNs > waitStartNs) port_perf_pace(endNs - waitStartNs);
        // Preserve phase across normal wake-up jitter instead of advancing
        // from an already-late wakeup every frame.
        mLastPresentNs = (status == 0) ? targetNs : endNs;
    } else {
        // We missed this slot: do not accumulate a backlog of catch-up frames.
        mLastPresentNs = nowNs;
    }
}

bool FramePacer::shouldPresentSlot(int targetFps, float gameSpeed, int64_t nowNs) const {
    (void)gameSpeed;
    if (targetFps <= 60) return false;
    int effectiveFps = targetFps;
    int64_t framePeriodNs = 1000000000ll / (effectiveFps > 0 ? effectiveFps : 60);
    return (nowNs - mLastPresentNs >= framePeriodNs);
}

void FramePacer::reset() {
    mLastPresentNs = 0;
}

}  // namespace mobile_display
