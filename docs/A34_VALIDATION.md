# Android stability pass — 21 October 2026

Branch: `fix/a34-stability-pacing`, based on `44ad6a3`.

## Changes

- Audio: a disconnected stream reports no usable device clock. Otherwise a
  stopped ring above the AI queue limit prevents the producer from ever
  reaching the reconnect code. Failed pause/resume requests also schedule
  reconnection; the producer stops/closes the old stream before replacement.
- Gamepad: retain held right-stick axes, integrate pointer movement using
  monotonic elapsed time, preserve direct-touch priority, and clear axes on
  pause. Android need not repeat motion events for a held stick to keep working.
- Vulkan: consume an orphaned render semaphore before signaling it again.
  `vkQueueWaitIdle` completes GPU work but does not unsignal binary semaphores.
  The recovery now uses a GPU semaphore wait rather than an idle CPU wait.
- Build: target native API 28, matching the APK's declared minimum. This is
  build compatibility, not a claim of validation on every Android version.
- Packaging: allow a separate tester application ID while retaining the
  original Java/JNI namespace, and create/reuse the local signing key on a
  clean checkout.

The earlier safe memory reservations, isolated `:game` process, monotonic
frame-pacer sleep and acquisition-stall diagnostics remain in this branch.

## Local verification

- NDK r25c (`25.2.9519653`), ARM64, API 28, `Release`: `libgame.so`,
  `libmain.so`, `libreserve.so`, `libinstaller.so` compiled and linked.
- `tools/tests`: 64 tests, 2 fixture-dependent skips.
- `tests`: 71 tests, 1 asset-dependent skip.
- The native runtime tests execute the production audio, gamepad, pacing and
  render-submission code in 13 scenarios with mocked Android/Vulkan services
  and the NDK's real API declarations. They do not emulate a Mali driver.
- Include-case and maintained-file size checks pass.
- The separate tester APK was compiled with JDK 17/platform 34 and checked
  with `apksigner verify`, the payload gate, `aapt2` and `zipalign -c -p 4`.
  It contains ARM64 code and declares the separate install ID and API 28.
  APK SHA-256: `57e5425a8d3b97c59e92a476470a76e0ed58410f561a37c9b449cdd35a1596df`.
- The extraction reference test now normalizes both path separators, as the
  production extractor does, so its Windows traversal case works on Linux.

```bash
export ANDROID_NDK_HOME=/path/to/android-ndk-r25c
python3 -m unittest discover -s tools/tests -p 'test_*.py'
python3 -m unittest discover -s tests -p 'test_*.py'
python3 tools/check_include_case.py
python3 tools/check_loc.py
CONFIG=Release bash build_mobile.sh -j2 game main reserve installer
```

A separate locally signed APK can be packaged with JDK 17 and Android SDK
platform 34/build-tools 34 installed:

```bash
GALAXY_ALLOW_DEBUGGABLE=1 GALAXY_ALLOW_NEW_KEY=1 \
  python3 package_mobile_apk.py --channel tester \
  --application-id com.openport.galaxyandroid.a34test
```

This produces a separate **Galaxy Android (Test)** app. Keep the local signing
key in `out/dev.keystore` for subsequent updates; it is ignored by Git.

## Device verification still required

No game assets, Android device, gameplay capture or A34 frame-time trace were
available for this pass. No FPS gain is measured and no upstream issue is
declared resolved from source checks alone.

On Galaxy A34, check cold start, exit to setup/relaunch, pause/resume, speaker
and Bluetooth/USB audio switching, and held controller camera/pointer input.
Compare 480p/60 and 480p/30 in the Observatory and water-heavy scenes for at
least 15 minutes, recording frame times, thermal behavior and Vulkan stalls.
THP movies, water/sun/star-bit rendering, controller gyro/rumble/remapping and
model-specific crashes remain open investigations.

References: [upstream reports](https://github.com/OpenPort-Labs/Galaxy-Android/issues),
[Vulkan semaphore rules](https://docs.vulkan.org/spec/latest/chapters/synchronization.html#synchronization-semaphores),
[AAudio disconnect handling](https://developer.android.com/ndk/guides/audio/aaudio/aaudio#disconnected_audio_stream).


## Graphics preparation follow-up — 0.0.4-a34.2

The user's screenshots show missing/dark geometry and an abnormal bright
horizon band. They establish visual defects, not the source of frame-time
stalls. The following source defects were reproduced or checked separately:

- Runtime shader misses used substitute material pipelines. Rendering now
  waits until every exact pipeline in the submitted frame is ready; it does
  not draw a partially shaded scene. A worker compiles both SPIR-V and the
  driver pipeline, then the render thread publishes completed handles.
- Requests are deduplicated by the full pipeline key until publication,
  including distinct blend/depth variants sharing one shader UID. Unpublished
  handles are disposed before shutdown or incompatible render-pass changes.
- Graphics preparation has its own pause owner. Completing it cannot resume
  an Android activity that is still paused. The previous complete frame (or
  an initial cleared view) stays visible with a preparation message.
- Inventory discoveries count unique keys and do not write files during a
  draw. Dirty entries, including fewer than 32, are flushed at pause/shutdown.
  Shader-source hashes are memoized by full UID. Draw recording reuses the
  handles obtained by the frame preflight instead of doing another lookup.
- GX TEV A/B/C and final RGB now use 8-bit wrap semantics rather than
  saturation. This matches the reference operations in
  [Dolphin PixelShaderGen](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/PixelShaderGen.cpp).
  For example, an input of 256 wraps to 0 instead of saturating to 255.
- Swapchain acquisition is bounded so lifecycle events can be processed;
  the FPS counter only advances for a submitted game frame accepted for
  presentation, rather than counting loading-screen redraws.

Verification: ARM64/API 28 Release libraries compiled and linked; 66 tool tests
and 71 regression tests discovered, with 134 passes and 3 missing-asset skips.
The production-code probes cover 18 scenarios, including full-key deduplication,
off-thread builds, worker shutdown, pipeline creation failure and independent pause
owners. Six generated vertex/fragment shaders (untextured, textured, and
16-stage materials with indirect texturing/fog/depth) compiled to SPIR-V using
a host glslang WebAssembly build. Include-case and size checks pass.

The signed APK has version code 15, the same tester application ID, and the
same certificate as the previous test APK, allowing an update. Signature,
archive payload, package metadata and alignment checks pass.

APK SHA-256: `271b43960159691198fbb43fccfb48a5dc0b1b0c60550ec0ffd4a08197e805c9`.

This moves previously hidden preparation into an explicit loading interval;
it does not eliminate first-visit compilation time or preload every game
asset. No A34 frame-time trace or post-change gameplay screenshots are
available. The specific bright-band defect and other graphics problems are
not declared solved without on-device verification.
