#include "ScenePresenter.h"

#include <atomic>
#include <unistd.h>
#include "../ui/UIOverlay.h"
#include "port/port.h"

extern std::atomic<uint64_t> gPresentedFrames;

namespace mobile_render {

void ScenePresenter::presentVulkan(gpu::VkRenderer& vkr, gpu::VkTarget& target,
                                  int screenW, int screenH, bool gameBooted,
                                  mobile_display::ViewportManager& vpMgr,
                                  mobile_display::FramePacer& framePacer,
                                  android_app* app) {
    const int64_t updateStart = port_host_time_ns();
    const bool newFrame = vkr.update();
    port_perf_update(port_host_time_ns() - updateStart);
    auto& ui = mobile_ui::UIOverlay::get();
    const auto& settings = ui.settings();
    bool rendered = false;

    if (newFrame || vkr.waitingForPipelines()) {
        int width = 0, height = 0;
        mobile_display::ViewportManager::computeTargetSize(screenW, screenH,
            settings.renderScale, settings.batterySaver, settings.fullscreen, width, height);
        if (!target.framebuffer || target.width != width || target.height != height) {
            gpu::VkTarget replacement = vkr.createTarget(width, height);
            if (replacement.framebuffer) {
                if (target.framebuffer) vkr.destroyTarget(target);
                target = replacement;
            }
        }
        const int64_t renderStart = port_host_time_ns();
        rendered = vkr.render(target, nullptr, nullptr, gpu::HudMode::Inline);
        const int64_t duration = port_host_time_ns() - renderStart;
        port_perf_render(duration, 0);
        if (rendered) ui.reportGpuRenderTime(duration);
    }

    const bool loading = vkr.waitingForPipelines();
    port_set_render_loading(loading ? 1 : 0);
    static bool wasLoading = false;
    if (loading && !wasLoading) ui.showToast("Preparing graphics...");
    if (!loading && wasLoading) ui.showToast("Graphics ready");
    wasLoading = loading;

    const bool redrawUi = ui.needsRedraw() &&
        (!gameBooted || port_is_paused() || ui.isSettingsOpen());
    if (rendered || loading || redrawUi) {
        const int fps = settings.batterySaver ? 30 : settings.targetFps;
        framePacer.sync(app, fps, port_get_game_speed());
        vpMgr.update(screenW, screenH, settings.fullscreen);
        const int64_t presentStart = port_host_time_ns();
        const bool presented = vkr.present(target, vpMgr.x(), vpMgr.y(), vpMgr.w(), vpMgr.h());
        port_perf_present(port_host_time_ns() - presentStart);
        if (rendered && presented) {
            gPresentedFrames.store(vkr.frameNumber());
            ui.reportFramePresented();
            if (gameBooted) port_vi_retrace();
        }
    } else {
        usleep(1000);
    }
}

}  // namespace mobile_render
