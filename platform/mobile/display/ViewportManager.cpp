#include "ViewportManager.h"

#include <algorithm>
#include <math.h>

namespace mobile_display {

void ViewportManager::update(int screenW, int screenH, bool fullscreen) {
    if (screenW <= 0 || screenH <= 0) {
        mViewport = {0, 0, 0, 0};
        return;
    }

    if (fullscreen) {
        mViewport = {0, 0, screenW, screenH};
        return;
    }

    float targetAspect = 16.0f / 9.0f;
    float screenAspect = (screenW >= screenH)
        ? ((float)screenW / (float)screenH)
        : ((float)screenH / (float)screenW);

    if (screenW >= screenH) {
        if (screenAspect > targetAspect) {
            mViewport.h = screenH;
            mViewport.w = (int)lroundf(screenH * targetAspect);
            mViewport.x = (screenW - mViewport.w) / 2;
            mViewport.y = 0;
        } else {
            mViewport.w = screenW;
            mViewport.h = (int)lroundf(screenW / targetAspect);
            mViewport.x = 0;
            mViewport.y = (screenH - mViewport.h) / 2;
        }
    } else {
        mViewport.w = screenW;
        mViewport.h = (int)lroundf(screenW / targetAspect);
        mViewport.x = 0;
        mViewport.y = (screenH - mViewport.h) / 2;
    }
}

void ViewportManager::computeTargetSize(int screenW, int screenH, int scaleChoice,
                                       bool batterySaver, bool fullscreen,
                                       int& outW, int& outH) {
    (void)screenW; (void)screenH; (void)scaleChoice;
    (void)batterySaver; (void)fullscreen;
    outW = 640;
    outH = 456;

}

void ViewportManager::computePointerBounds(int screenW, int screenH, bool fullscreen,
                                          const Rect& vp, Rect& outBounds) {
    if (fullscreen) {
        outBounds = {0, 0, screenW, screenH};
    } else {
        outBounds = vp;
    }
}

}  // namespace mobile_display
