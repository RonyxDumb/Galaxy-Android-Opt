#include <aaudio/AAudio.h>
#include <cassert>
#include <cstdint>
#include <cstring>
#include <vector>
#include "revolution/types.h"

extern "C" void port_audio_submit(const s16*, u32, u32);
extern "C" int64_t port_audio_queued_ns();
extern "C" void port_audio_pause();
extern "C" void port_audio_resume();
extern "C" void port_log(const char*, ...) {}
extern "C" int port_is_paused() { return 0; }
extern "C" int port_skip_fast_forwarding() { return 0; }
void port_audio_mix_speaker(s16*, u32, u32) {}

struct AAudioStreamBuilderStruct {
    AAudioStream_dataCallback data = nullptr;
    AAudioStream_errorCallback error = nullptr;
};
struct AAudioStreamStruct : AAudioStreamBuilderStruct {
    int rate = 48000, size = 576;
};
static AAudioStream* live = nullptr;
static int opens = 0, closes = 0, stops = 0, rate = 48000, failures = 0;
static bool failStart = false, failPause = false;

extern "C" aaudio_result_t AAudio_createStreamBuilder(AAudioStreamBuilder** b) { *b = new AAudioStreamBuilder; return AAUDIO_OK; }
extern "C" aaudio_result_t AAudioStreamBuilder_delete(AAudioStreamBuilder* b) { delete b; return AAUDIO_OK; }
extern "C" void AAudioStreamBuilder_setFormat(AAudioStreamBuilder*, aaudio_format_t) {}
extern "C" void AAudioStreamBuilder_setChannelCount(AAudioStreamBuilder*, int32_t count) { assert(count == 2); }
extern "C" void AAudioStreamBuilder_setSampleRate(AAudioStreamBuilder*, int32_t) {}
extern "C" void AAudioStreamBuilder_setPerformanceMode(AAudioStreamBuilder*, aaudio_performance_mode_t) {}
extern "C" void AAudioStreamBuilder_setSharingMode(AAudioStreamBuilder*, aaudio_sharing_mode_t) {}
extern "C" void AAudioStreamBuilder_setUsage(AAudioStreamBuilder*, aaudio_usage_t) {}
extern "C" void AAudioStreamBuilder_setContentType(AAudioStreamBuilder*, aaudio_content_type_t) {}
extern "C" void AAudioStreamBuilder_setDataCallback(AAudioStreamBuilder* b, AAudioStream_dataCallback f, void*) { b->data = f; }
extern "C" void AAudioStreamBuilder_setErrorCallback(AAudioStreamBuilder* b, AAudioStream_errorCallback f, void*) { b->error = f; }
extern "C" aaudio_result_t AAudioStreamBuilder_openStream(AAudioStreamBuilder* b, AAudioStream** stream) {
    opens++;
    if (failures) { failures--; *stream = nullptr; return AAUDIO_ERROR_UNAVAILABLE; }
    live = new AAudioStream;
    static_cast<AAudioStreamBuilderStruct&>(*live) = *b;
    live->rate = rate; *stream = live;
    return AAUDIO_OK;
}
extern "C" aaudio_result_t AAudioStream_close(AAudioStream* s) { closes++; delete s; live = nullptr; return AAUDIO_OK; }
extern "C" aaudio_result_t AAudioStream_requestStop(AAudioStream*) { stops++; return AAUDIO_OK; }
extern "C" aaudio_result_t AAudioStream_requestStart(AAudioStream*) { return failStart ? AAUDIO_ERROR_DISCONNECTED : AAUDIO_OK; }
extern "C" aaudio_result_t AAudioStream_requestPause(AAudioStream*) { return failPause ? AAUDIO_ERROR_DISCONNECTED : AAUDIO_OK; }
extern "C" int32_t AAudioStream_getSampleRate(AAudioStream* s) { return s->rate; }
extern "C" int32_t AAudioStream_getFramesPerBurst(AAudioStream*) { return 192; }
extern "C" int32_t AAudioStream_getBufferCapacityInFrames(AAudioStream*) { return 4096; }
extern "C" int32_t AAudioStream_getBufferSizeInFrames(AAudioStream* s) { return s->size; }
extern "C" aaudio_result_t AAudioStream_setBufferSizeInFrames(AAudioStream* s, int32_t n) { return s->size = n; }
extern "C" const char* AAudio_convertResultToText(aaudio_result_t) { return "test"; }

static void submit(u32 frames = 3200, u32 sourceRate = 32000) {
    std::vector<s16> data(frames * 2);
    for (u32 i = 0; i < frames; i++) { data[i * 2] = 1000; data[i * 2 + 1] = -2000; }
    port_audio_submit(data.data(), frames, sourceRate);
}

int main(int argc, char** argv) {
    assert(argc == 2 && port_audio_queued_ns() == -1);
    if (!strcmp(argv[1], "reconnect")) {
        submit();
        assert(live && port_audio_queued_ns() > 48000000);
        live->error(live, nullptr, AAUDIO_ERROR_DISCONNECTED);
        // aiClock must leave backlog pacing so that the producer can run again.
        assert(port_audio_queued_ns() == -1);
        rate = 44100;
        submit(320);
        assert(opens == 2 && closes == 1 && stops == 1);
        assert(live->rate == rate);
        assert(port_audio_queued_ns() > 0 && port_audio_queued_ns() < 12000000);
        s16 output[128] = {};
        live->data(live, nullptr, output, 64);
        bool audible = false;
        for (s16 v : output) audible |= v != 0;
        assert(audible);
    } else if (!strcmp(argv[1], "resume") || !strcmp(argv[1], "pause")) {
        submit();
        if (!strcmp(argv[1], "resume")) { failStart = true; port_audio_resume(); }
        else { failPause = true; port_audio_pause(); }
        assert(port_audio_queued_ns() == -1);
        failStart = failPause = false;
        submit(320);
        assert(opens == 2 && closes == 1);
    } else if (!strcmp(argv[1], "retry")) {
        failures = 2;
        submit(320);
        assert(!live && opens == 2 && port_audio_queued_ns() == -1);
        for (int i = 0; i < 29; i++) submit(320);
        assert(!live && opens == 2);
        submit(320);
        assert(live && opens == 3);
    } else if (!strcmp(argv[1], "channels")) {
        rate = 32000;
        submit(32);
        s16 output[128] = {};
        live->data(live, nullptr, output, 64);
        for (int i = 0; i < 32; i++) { assert(output[i * 2] == -2000); assert(output[i * 2 + 1] == 1000); }
        for (int i = 64; i < 128; i++) assert(output[i] == 0);
        assert(port_audio_queued_ns() == 0);
    } else { return 2; }
    if (live) AAudioStream_close(live);
}
