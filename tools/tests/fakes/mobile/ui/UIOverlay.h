#pragma once

// Host test double for the UI service. Input handling uses the production
// Settings type; no renderer or Android window is created by these tests.
#include "mobile/ui/Settings.h"

namespace mobile_ui {
class UIOverlay {
public:
    static UIOverlay& get() { static UIOverlay ui; return ui; }
    Settings& settings() { return mSettings; }
    void showToast(const char*) {}
    bool isSettingsOpen() const { return false; }
    void closeSettings() {}
    void setPointerPos(float x, float y) { mX = x; mY = y; }
    float getPointerX() const { return mX; }
    float getPointerY() const { return mY; }
private:
    Settings mSettings;
    float mX = 0.0f, mY = 0.0f;
};
}
