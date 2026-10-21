#include <android/input.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include "mobile/input/GamepadInput.h"
#include "mobile/ui/UIOverlay.h"

struct AInputEvent { float axes[48] = {}; int key = 0; int action = 0; };
extern "C" int32_t AInputEvent_getSource(const AInputEvent*) { return AINPUT_SOURCE_JOYSTICK; }
extern "C" float AMotionEvent_getAxisValue(const AInputEvent* e, int32_t axis, size_t) { return e->axes[axis]; }
extern "C" int32_t AKeyEvent_getKeyCode(const AInputEvent* e) { return e->key; }
extern "C" int32_t AKeyEvent_getAction(const AInputEvent* e) { return e->action; }

static void near(float actual, float expected) { assert(std::fabs(actual - expected) < 0.0001f); }

int main(int argc, char** argv) {
    assert(argc == 2);
    mobile_input::GamepadInput input;
    auto& ui = mobile_ui::UIOverlay::get();
    PortPadState pad{};
    int spin = 0;
    AInputEvent event;
    event.axes[AMOTION_EVENT_AXIS_Z] = 0.5f;
    event.axes[AMOTION_EVENT_AXIS_RZ] = -0.5f;
    assert(input.handleMotionEvent(&event, pad, spin, false));
    if (!strcmp(argv[1], "camera")) {
        ui.settings().rightStickMode = 0;
        input.update(pad, false, 1000000000);
        float heldX = pad.rightStickX, heldY = pad.rightStickY;
        assert(heldX != 0.0f && heldY != 0.0f);
        for (int i = 1; i <= 100; i++) {
            pad.rightStickX *= 0.82f; pad.rightStickY *= 0.82f;
            input.update(pad, false, 1000000000LL + i * 1000000LL);
            near(pad.rightStickX, heldX); near(pad.rightStickY, heldY);
        }
    } else if (!strcmp(argv[1], "pointer")) {
        ui.settings().rightStickMode = 1;
        input.update(pad, false, 1000000000);
        for (int i = 1; i <= 100; i++) input.update(pad, false, 1000000000LL + i * 1000000LL);
        float x = pad.pointerX, y = pad.pointerY;
        assert(x > 0.0f && y < 0.0f && pad.pointerValid);
        // Same elapsed time at 50 Hz instead of 1000 Hz: same displacement.
        input.reset(); ui.setPointerPos(0, 0);
        input.handleMotionEvent(&event, pad, spin, false);
        input.update(pad, false, 2000000000);
        for (int i = 1; i <= 5; i++) input.update(pad, false, 2000000000LL + i * 20000000LL);
        near(pad.pointerX, x); near(pad.pointerY, y);
        // Direct touch takes priority, and release does not accumulate a jump.
        input.update(pad, true, 3000000000);
        near(pad.pointerX, x);
        input.update(pad, false, 3001000000);
        assert(pad.pointerX < x + 0.01f);
        for (int i = 1; i <= 200; i++) input.update(pad, false, 3001000000LL + i * 50000000LL);
        near(pad.pointerX, 1.0f); near(pad.pointerY, -1.0f);
    } else if (!strcmp(argv[1], "reset")) {
        event.axes[AMOTION_EVENT_AXIS_X] = 1.0f;
        event.axes[AMOTION_EVENT_AXIS_LTRIGGER] = 1.0f;
        input.handleMotionEvent(&event, pad, spin, false);
        assert(input.stickX() != 0.0f && input.buttons() != 0);
        input.reset();
        near(input.stickX(), 0); near(input.stickY(), 0);
        assert(input.buttons() == 0);
        ui.settings().rightStickMode = 0;
        input.update(pad, false, 1000000000);
        near(pad.rightStickX, 0); near(pad.rightStickY, 0);
    } else { return 2; }
}
