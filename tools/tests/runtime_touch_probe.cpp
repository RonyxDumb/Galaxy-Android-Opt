#include <android/input.h>
#include <cassert>
#include <cmath>
#include <cstring>
#include <fstream>
#include "mobile/ui/TouchInputHandler.h"
#include "mobile/display/ViewportManager.h"

struct AInputEvent { int action = 0, count = 1; int ids[3]{0, 1, 2}; float x[3]{}, y[3]{}; };
extern "C" int32_t AMotionEvent_getAction(const AInputEvent* e) { return e->action; }
extern "C" size_t AMotionEvent_getPointerCount(const AInputEvent* e) { return e->count; }
extern "C" int32_t AMotionEvent_getPointerId(const AInputEvent* e, size_t i) { return e->ids[i]; }
extern "C" float AMotionEvent_getX(const AInputEvent* e, size_t i) { return e->x[i]; }
extern "C" float AMotionEvent_getY(const AInputEvent* e, size_t i) { return e->y[i]; }
extern "C" int32_t AInputEvent_getType(const AInputEvent*) { return AINPUT_EVENT_TYPE_MOTION; }
extern "C" int32_t AInputEvent_getSource(const AInputEvent*) { return AINPUT_SOURCE_TOUCHSCREEN; }
extern "C" int __android_log_print(int, const char*, const char*, ...) { return 0; }
extern "C" int port_is_title_screen() { return 0; }
extern "C" void port_android_vibrate(int) {}

static void near(float a, float b) { assert(std::fabs(a - b) < 0.001f); }

int main(int argc, char** argv) {
    assert(argc == 2);
    mobile_ui::Settings settings;
    mobile_ui::TouchInputHandler touch;
    mobile_ui::LayoutManager layout;
    PortPadState pad{};
    int spin = 0;
    bool opened = false, saved = false;
    auto event = [&](AInputEvent& e) {
        assert(touch.handleTouch(&e, pad, spin, 1400, 650, 122, 0, 1156, 650,
                                 settings, layout, opened, saved));
        assert(!opened && !saved);
        touch.update(pad, spin, true);
    };
    AInputEvent e;
    if (!strcmp(argv[1], "profile")) {
        const char* path = "/tmp/galaxy-profile-test.ini";
        { std::ofstream f(path); f << "dynamic_stick=1\ntouch_camera=1\ntarget_fps=144\nrender_scale=3\nmod_shield_textures=1\nmod_fly=1\n"; }
        settings.load(path);
        std::remove(path);
        assert(!settings.dynamicStick && !settings.touchCamera && !settings.adaptiveFps);
        assert(!settings.modShieldTextures && !settings.modShieldCutscenes && !settings.modShieldControls);
        assert(!settings.modRiivolution && !settings.modClassicGamepadUI && !settings.modFly);
        assert(settings.targetFps == 60 && settings.renderScale == 0 && settings.gameSpeed == 1);
        int w = 0, h = 0;
        mobile_display::ViewportManager::computeTargetSize(2400, 1080, 0, false, false, w, h);
        assert(w == 640 && h == 456);
    } else if (!strcmp(argv[1], "fixed-stick")) {
        e.x[0] = 205; e.y[0] = 475; event(e);
        near(touch.stickBaseX(), 175); near(touch.stickBaseY(), 475);
        e.action = AMOTION_EVENT_ACTION_MOVE; e.x[0] = 250; event(e);
        assert(pad.stickX > 0 && touch.isStickActive());
        e.action = AMOTION_EVENT_ACTION_UP; event(e);
        assert(!touch.isStickActive() && !(touch.getTouchButtons() & 0x0800));
    } else if (!strcmp(argv[1], "pointer-camera")) {
        e.x[0] = 700; e.y[0] = 325; event(e);
        near(pad.pointerX, 0); near(pad.pointerY, 0);
        e.action = AMOTION_EVENT_ACTION_MOVE; e.x[0] = 900; e.y[0] = 425; event(e);
        assert(pad.pointerX > 0 && pad.pointerY > 0);
        near(pad.rightStickX, 0); near(pad.rightStickY, 0);
    } else if (!strcmp(argv[1], "dpad")) {
        e.x[0] = 95; e.y[0] = 160; event(e);
        assert(touch.getTouchButtons() & 1);
        assert(!(touch.getTouchButtons() & 0x0800));
        near(pad.rightStickX, 0);
    } else if (!strcmp(argv[1], "multitouch")) {
        e.x[0] = 175; e.y[0] = 475; event(e);
        e.action = AMOTION_EVENT_ACTION_POINTER_DOWN | (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT);
        e.count = 2; e.x[1] = 800; e.y[1] = 325; event(e);
        assert(touch.isStickActive() && pad.pointerValid);
        e.action = AMOTION_EVENT_ACTION_POINTER_UP | (1 << AMOTION_EVENT_ACTION_POINTER_INDEX_SHIFT); event(e);
        assert(touch.isStickActive() && pad.pointerValid && (touch.getTouchButtons() & 0x0800));
    } else if (!strcmp(argv[1], "buttons")) {
        auto b = layout.calculateButtonLayout(1400, 650, settings);
        e.x[0] = b.bx; e.y[0] = b.by; event(e);
        assert(touch.getTouchButtons() == 0x0400);
        e.action = AMOTION_EVENT_ACTION_UP; event(e);
        assert(!touch.getTouchButtons());
        e.action = AMOTION_EVENT_ACTION_DOWN; e.x[0] = 1000; e.y[0] = 300; event(e);
        e.action = AMOTION_EVENT_ACTION_UP; event(e);
        assert(pad.pointerValid && (touch.getTouchButtons() & 0x0800));
        touch.reset(); touch.update(pad, spin, true);
        assert(!touch.getTouchButtons());
    } else return 2;
}
