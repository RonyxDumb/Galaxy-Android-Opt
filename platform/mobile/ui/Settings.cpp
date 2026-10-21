#include "Settings.h"

#include <android/log.h>
#include <stdio.h>
#include <string.h>
#include <algorithm>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "MobileUI", __VA_ARGS__)

namespace mobile_ui {

void Settings::load(const std::string& path) {
    (void)path;
    // This build always uses the original game profile, including on upgrade.
    // Old HD, camera, cheat and pacing preferences must not survive migration.
    *this = Settings{};

}

void Settings::save(const std::string& path) const {
    FILE* f = fopen(path.c_str(), "w");
    if (!f) return;
    fprintf(f, "show_controls=%d\n", showControls ? 1 : 0);
    fprintf(f, "fullscreen=%d\n", fullscreen ? 1 : 0);
    fprintf(f, "full_touch_mode=%d\n", fullTouchMode ? 1 : 0);
    fprintf(f, "perf_hud=%d\n", perfHudMode);
    fprintf(f, "graphics_backend=%d\n", graphicsBackend);
    fprintf(f, "relative_pointer=%d\n", relativePointer ? 1 : 0);
    fprintf(f, "screen_orientation=%d\n", screenOrientationChoice);
    fprintf(f, "target_fps=%d\n", targetFps);
    fprintf(f, "display_refresh_rate=%d\n", preferredRefreshRate);
    fprintf(f, "battery_saver=%d\n", batterySaver ? 1 : 0);
    fprintf(f, "gyro_aiming=%d\n", gyroAiming ? 1 : 0);
    fprintf(f, "gyro_invert_x=%d\n", gyroInvertX ? 1 : 0);
    fprintf(f, "gyro_invert_y=%d\n", gyroInvertY ? 1 : 0);
    fprintf(f, "star_stick=%d\n", starStick ? 1 : 0);
    fprintf(f, "stick_sensitivity=%.2f\n", stickSensitivity);
    fprintf(f, "star_stick_sensitivity=%.2f\n", starStickSensitivity);
    fprintf(f, "right_stick_mode=%d\n", rightStickMode);
    fprintf(f, "touch_camera=%d\n", touchCamera ? 1 : 0);
    fprintf(f, "opacity=%.2f\n", opacity);
    fprintf(f, "scale=%.2f\n", scale);
    fprintf(f, "dynamic_stick=%d\n", dynamicStick ? 1 : 0);
    fprintf(f, "stick_offset_x=%.1f\n", stickOffsetX);
    fprintf(f, "stick_offset_y=%.1f\n", stickOffsetY);
    fprintf(f, "button_offset_x=%.1f\n", buttonOffsetX);
    fprintf(f, "button_offset_y=%.1f\n", buttonOffsetY);
    fprintf(f, "btn_a_offset_x=%.1f\n", btnAOffsetX);
    fprintf(f, "btn_a_offset_y=%.1f\n", btnAOffsetY);
    fprintf(f, "btn_b_offset_x=%.1f\n", btnBOffsetX);
    fprintf(f, "btn_b_offset_y=%.1f\n", btnBOffsetY);
    fprintf(f, "btn_z_offset_x=%.1f\n", btnZOffsetX);
    fprintf(f, "btn_z_offset_y=%.1f\n", btnZOffsetY);
    fprintf(f, "btn_spin_offset_x=%.1f\n", btnSpinOffsetX);
    fprintf(f, "btn_spin_offset_y=%.1f\n", btnSpinOffsetY);
    fprintf(f, "star_stick_offset_x=%.1f\n", starStickOffsetX);
    fprintf(f, "star_stick_offset_y=%.1f\n", starStickOffsetY);
    fprintf(f, "show_dpad=%d\n", showDpad ? 1 : 0);
    fprintf(f, "dpad_on_right=%d\n", dpadOnRight ? 1 : 0);
    fprintf(f, "dpad_offset_x=%.1f\n", dpadOffsetX);
    fprintf(f, "dpad_offset_y=%.1f\n", dpadOffsetY);
    fprintf(f, "mod_shield_textures=%d\n", modShieldTextures ? 1 : 0);
    fprintf(f, "mod_shield_cutscenes=%d\n", modShieldCutscenes ? 1 : 0);
    fprintf(f, "mod_shield_controls=%d\n", modShieldControls ? 1 : 0);
    fprintf(f, "mod_classic_gamepad_ui=%d\n", modClassicGamepadUI ? 1 : 0);
    fprintf(f, "mod_riivolution=%d\n", modRiivolution ? 1 : 0);
    fprintf(f, "mod_fly=%d\n", modFly ? 1 : 0);
    fprintf(f, "mod_skip_cutscenes=%d\n", modSkipCutscenes ? 1 : 0);
    fprintf(f, "mod_infinite_health=%d\n", modInfiniteHealth ? 1 : 0);
    fprintf(f, "mod_infinite_lives=%d\n", modInfiniteLives ? 1 : 0);
    fprintf(f, "mod_infinite_starbits=%d\n", modInfiniteStarBits ? 1 : 0);
    fprintf(f, "game_speed=%d\n", gameSpeed);
    fprintf(f, "language_choice=%d\n", languageChoice);
    fprintf(f, "render_scale=%d\n", renderScale);
    fprintf(f, "haptics=%d\n", haptics ? 1 : 0);
    fprintf(f, "target_fps=%d\n", targetFps);
    fprintf(f, "adaptive_fps=%d\n", adaptiveFps ? 1 : 0);
    fprintf(f, "speedrun_hud=%d\n", speedrunHud ? 1 : 0);
    fclose(f);
    LOGI("Saved settings to %s", path.c_str());
}

}  // namespace mobile_ui
