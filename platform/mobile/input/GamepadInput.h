#pragma once

#include <android/input.h>
#include <stdint.h>
#include "port/input.h"

namespace mobile_input {

class GamepadInput {
public:
    GamepadInput() = default;

    // Handles physical joystick motion events (analog sticks, triggers, D-pad hat)
    bool handleMotionEvent(AInputEvent* event, PortPadState& pad, int& spinFrames, bool touchscreenDown);

    // Handles physical gamepad / keyboard button events
    bool handleKeyEvent(AInputEvent* event, PortPadState& pad, int& spinFrames);

    // Applies held axes once per host update, independently of event frequency.
    void update(PortPadState& pad, bool touchscreenDown, int64_t nowNs);

    // Resets held buttons and axes on pause.
    void reset();

    // Returns whether a physical controller has been detected
    bool isGamepadDetected() const { return mGamepadDetected; }

    // Physical analog stick values
    float stickX() const { return mPhysicalStickX; }
    float stickY() const { return mPhysicalStickY; }

    // Combined physical button masks
    uint32_t buttons() const { return mPhysicalButtons | mPhysicalAxisButtons; }

private:
    bool mGamepadDetected = false;
    uint32_t mPhysicalButtons = 0;
    uint32_t mPhysicalAxisButtons = 0;
    float mPhysicalStickX = 0.0f;
    float mPhysicalStickY = 0.0f;
    float mRightStickX = 0.0f;
    float mRightStickY = 0.0f;
    int64_t mLastUpdateNs = 0;
};

}  // namespace mobile_input
