#pragma once
#include <atomic>

// Independent pause owners: completing graphics preparation must never resume
// an activity that Android has put in the background.
class PortPauseState {
public:
    bool setLifecyclePaused(bool paused) { return mLifecycle.exchange(paused) != paused; }
    void setRenderLoading(bool loading) { mLoading.store(loading); }
    bool isPaused() const { return mLifecycle.load() || mLoading.load(); }
private:
    std::atomic<bool> mLifecycle{false};
    std::atomic<bool> mLoading{false};
};
