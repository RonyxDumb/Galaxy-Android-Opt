#include <cassert>
#include <cerrno>
#include <cstdint>
#include <cstring>
#include <initializer_list>
#include <time.h>
#include "mobile/display/FramePacer.h"

static int64_t now = 1000000000, slept = 0, deadline = 0;
static int interruptions = 0, sleepCalls = 0;
extern "C" int64_t port_host_time_ns() { return now; }
extern "C" void port_perf_pace(int64_t duration) { slept += duration; }
extern "C" int clock_nanosleep(clockid_t clock, int flags, const timespec* ts, timespec*) {
    assert(clock == CLOCK_MONOTONIC && flags == TIMER_ABSTIME);
    sleepCalls++;
    int64_t requested = ts->tv_sec * 1000000000LL + ts->tv_nsec;
    if (interruptions) {
        interruptions--; deadline = requested; now += 1000; return EINTR;
    }
    if (deadline) assert(deadline == requested);
    now = requested + 100000; // A normal 0.1 ms wake-up overshoot.
    return 0;
}

int main(int argc, char** argv) {
    assert(argc == 2);
    mobile_display::FramePacer pacer;
    if (!strcmp(argv[1], "cadence")) {
        for (int fps : {30, 60, 144}) {
            pacer.reset(); now = 1000000000; slept = 0;
            const int64_t period = 1000000000LL / fps;
            pacer.sync(nullptr, fps, 1.0f);
            assert(pacer.lastPresentNs() == now);
            for (int frame = 1; frame <= 10; frame++) {
                now += 2000000;
                pacer.sync(nullptr, fps, 1.0f);
                assert(pacer.lastPresentNs() == 1000000000 + frame * period);
            }
            assert(slept > 0);
        }
    } else if (!strcmp(argv[1], "interrupted")) {
        pacer.sync(nullptr, 60, 1.0f);
        interruptions = 3; now += 1000000;
        pacer.sync(nullptr, 60, 1.0f);
        assert(sleepCalls == 4 && interruptions == 0);
        assert(pacer.lastPresentNs() == deadline);
    } else if (!strcmp(argv[1], "stall")) {
        pacer.sync(nullptr, 60, 1.0f);
        now += 500000000;
        pacer.sync(nullptr, 60, 1.0f);
        assert(sleepCalls == 0 && pacer.lastPresentNs() == now);
        now -= 100000000;
        pacer.sync(nullptr, 60, 1.0f);
        assert(pacer.lastPresentNs() == now);
    } else { return 2; }
}
