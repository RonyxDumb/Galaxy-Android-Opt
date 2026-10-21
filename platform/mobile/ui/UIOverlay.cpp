#include "UIOverlay.h"

#include <android/log.h>
#include <sys/system_properties.h>
#include <time.h>
#include <math.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)

extern "C" {
void port_set_speedrun_hud_enabled(int enabled);
void port_set_fly_mode(int enabled);
void port_set_skip_cutscenes(int enabled);
void port_set_infinite_health(int enabled);
void port_set_infinite_lives(int enabled);
void port_set_infinite_starbits(int enabled);
}

namespace mobile_ui {

UIOverlay& UIOverlay::get() {
    static UIOverlay instance;
    return instance;
}

UIOverlay::UIOverlay() {

}

bool UIOverlay::init() {
    port_set_speedrun_hud_enabled(mSettings.speedrunHud ? 1 : 0);
    port_set_fly_mode(mSettings.modFly ? 1 : 0);
    port_set_skip_cutscenes(mSettings.modSkipCutscenes ? 1 : 0);
    port_set_infinite_health(mSettings.modInfiniteHealth ? 1 : 0);
    port_set_infinite_lives(mSettings.modInfiniteLives ? 1 : 0);
    port_set_infinite_starbits(mSettings.modInfiniteStarBits ? 1 : 0);
    return true;
}

void UIOverlay::setSavePath(const std::string& path) {
    mSave.setSavePath(path);
    mSettings.load(path);
    port_set_speedrun_hud_enabled(mSettings.speedrunHud ? 1 : 0);
    port_set_fly_mode(mSettings.modFly ? 1 : 0);
    port_set_skip_cutscenes(mSettings.modSkipCutscenes ? 1 : 0);
    port_set_infinite_health(mSettings.modInfiniteHealth ? 1 : 0);
    port_set_infinite_lives(mSettings.modInfiniteLives ? 1 : 0);
    port_set_infinite_starbits(mSettings.modInfiniteStarBits ? 1 : 0);
}

void UIOverlay::update(PortPadState& pad, int& spinFrames) {
    mTouch.update(pad, spinFrames, mSettings.fullTouchMode);
    mPerf.update(mSettings.targetFps, mSettings.batterySaver);

    std::string toast;
    if (mSave.update(toast)) {
        showToast(toast);
    }

    if (mToastTimer > 0.0f) {
        mToastTimer -= 0.016f;
        if (mToastTimer < 0.0f) mToastTimer = 0.0f;
        mDirty = true;
    }
}

void UIOverlay::showToast(const std::string& msg) {
    mToastMessage = msg;
    mToastTimer = 2.8f;
    mDirty = true;
}

void UIOverlay::triggerSaveAnywhere() {
    showToast(mSave.triggerSaveAnywhere(mSettings.haptics));
}

void UIOverlay::triggerSaveSlot(int slot) {
    showToast(mSave.triggerSaveSlot(slot, mSettings.haptics));
}

void UIOverlay::triggerLoadSlot(int slot) {
    showToast(mSave.triggerLoadSlot(slot, mSettings.haptics));
}

bool UIOverlay::handleInput(const AInputEvent* event, PortPadState& pad, int& spinFrames,
                            int screenW, int screenH, int vpX, int vpY, int vpW, int vpH) {
    if (AInputEvent_getType(event) != AINPUT_EVENT_TYPE_MOTION) return false;

    // 3. Normal on-screen touch controls
    bool openSettings = false;
    bool quickSave = false;
    bool res = mTouch.handleTouch(event, pad, spinFrames, screenW, screenH,
                                 vpX, vpY, vpW, vpH, mSettings, mLayout,
                                 openSettings, quickSave);
    mDirty = true;
    return res;
}

void UIOverlay::generateVertices(int screenW, int screenH, std::vector<UIVertex>& outVerts) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    int64_t now = (int64_t)ts.tv_sec * 1000000000ll + ts.tv_nsec;
    if (mLastAnimTime == 0) mLastAnimTime = now;
    float dt = (float)(now - mLastAnimTime) / 1e9f;
    mLastAnimTime = now;
    if (dt < 0.0001f) dt = 0.016f;
    if (dt > 0.1f) dt = 0.1f;

    mMenu.updateAnimation(dt);

    mRenderer.generateVertices(screenW, screenH, mSettings, mLayout, mTouch,
                               mPerf, mMenu, mSave, mActiveBackend,
                               mToastMessage, mToastTimer, outVerts);
}

}  // namespace mobile_ui
