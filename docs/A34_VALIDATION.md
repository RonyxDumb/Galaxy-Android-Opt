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

## Material and texture follow-up — 0.0.4-a34.3

New user screenshots still show the bright horizon band, pale/white characters
and incorrect material colors. The following corrections address independently
reproduced shader defects; the screenshots alone do not establish their causes.

- XF lighting now accumulates ambient and light contributions, clamps that
  illumination, then modulates the material using the 8-bit XF multiply. The
  old shader added unmodulated ambient and could turn a red material white.
  RGB/alpha retain separate material and ambient sources. Disabled raster
  channels are zero instead of a synthetic white vertex color.
- Signed diffuse lighting retains negative contributions. Specular attenuation
  uses the surface normal/half-vector angle, and spot attenuation uses the
  direction already converted by `GXInitLightDir`. Recorded light positions
  are preserved rather than rescaled, which changed distance attenuation.
- Texgen reads raw position/normal attributes and decodes projection bit 1
  separately from input-form bit 2. Matrix zero remains valid, post-matrix
  normalization is applied when requested, color texgens use lit colors, and
  emboss texgens retain the transformed tangent scale. Both halves of each
  indirect scale register are included in the shader UID.
- TEV uses arithmetic shifts for signed intermediate division and consistent
  interpolation rounding for alpha. Zero-valued channel swap tables are
  honored. Alpha-test generation returns owned strings, removing shared
  scratch buffers accessed by concurrent prewarm workers.

Reference behavior was checked against the repository's `GXLight.c`,
`GXAttr.c` and `GXTev.c`, and Dolphin's
[lighting](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/LightingShaderGen.cpp),
[vertex](https://github.com/dolphin-emu/dolphin/blob/master/Source/Core/VideoCommon/VertexShaderGen.cpp)
and pixel shader generators. The implementation remains in separate small
lighting and texgen generator files.

Verification: 85 tool tests and 71 regression tests, **153 passed, 3 skipped**
for unavailable game fixtures. Seventeen numerical shader cases run on Mesa
lavapipe. Each first compiles the original production vertex/fragment shader
to SPIR-V, then executes its complete body through a compute-stage interface
adapter and checks the returned colors/coordinates. This is not a rasterized
gameplay test or a Mali driver test. CI installs software Vulkan and requires
these tests rather than silently skipping them. An eight-thread test also
checks 32,000 generated shader strings for deterministic alpha tests.

The same execution harness was run against the previous committed generator:
white ambient plus a red material produced `(1,1,1)` before and `(1,0,0)` after;
an STQ coordinate with matrix Q=2 incorrectly produced Q=1 before and Q=2 after;
the signed half-scale/wrap regression produced zero before and 255 after.

ARM64/API 28 Release libraries compile/link, include-case and size checks pass,
and APK signature, alignment, package identity and certificate continuity
were checked. Version code **16**, tester package unchanged. The first graphics
preparation may take longer because changed shader sources need compilation.

APK SHA-256: `7a0c8fb12ea428db42afc0510fbd86a8087de62866f423dc853f66ac17203a65`.

The new APK has not been run on the A34. The horizon band, remaining white
objects and gameplay slowdown require another on-device check; none is
declared fully resolved by these numerical tests.

## Original game profile and hybrid shaders — 0.0.4-a34.4

The launcher is now **Super Mario Galaxy**. It offers one action, selecting
and extracting the user's ROM, then opens the game. Existing usable data opens
directly. Installation uses the document picker grant and app-owned storage;
the freshly extracted app-owned disc takes precedence over legacy shared data.
Movies are included. The launcher no longer presents resolution presets,
custom-driver controls, telemetry, Discord prompts or an install options panel.

The mobile profile ignores old touch-settings files and uses original assets,
the game's native EFB dimensions, its 16:9 presentation and the normal 60 Hz
simulation/presentation target. Shield textures/movies/control changes,
Riivolution, cheats, adaptive FPS and resolution scaling are disabled. The
added orbital-camera code is excluded from the mobile build. The touch stick
is fixed, screen touch drives the star pointer, and the four direction buttons
drive the game's original camera. The original pause button is available;
the port's settings cog, quick-save dock and drawer are absent. Tap handling
now distinguishes pointer taps from stick/button releases, and B does not
also press A during gameplay.

The launcher cover comes from the user's supplied image. Android 8+ adaptive
resources use that image, and Android 13+ resources include a monochrome Luma
mask derived from it, for launchers with themed icons enabled. No generated
replacement illustration is used.

Graphics preparation no longer freezes the game or displays a mid-game shader
loading toast. Universal vertex/fragment shaders interpret the actual recorded
GX material state while specialized shaders compile on the worker. The normal
draw buffers carry a separate packed material block per unique UID. Pipeline
families known to the inventory are prepared at startup with their real depth,
blend, vertex layout and raster state; specialized shaders replace the general
ones when available. These are functional material interpreters, rather than
flat-color substitute pipelines.

During a new ROM extraction, the installer now scans the cooked disc's actual
models and writes its material manifest before reporting completion. It ships
the canonical CRAR reader separately from the older conversion reader. The
renderer prefers this installed-disc manifest to the small APK seed, and
initializes caches using the selected data root rather than a fixed path.
The APK seed is copied before NativeActivity starts, avoiding its startup race.
Indirect bump raster channels and normalized bump alpha also now follow their
GX values instead of incorrectly aliasing vertex color channels.

Validation: **89 tool tests and 71 regression tests, 157 passed and 3 asset
skips**. Nineteen shader tests perform 102 software-Vulkan executions, comparing
the specialized and universal shader bodies, including 32 deterministic
multistage textured materials, alpha/depth behavior and indirect bump channels.
Native probes cover the locked profile, fixed-stick and multitouch behavior,
pointer/camera separation, button semantics, non-blocking shader completion
and installed-disc manifest preference. A separate isolated-Python test runs
the shipped flat module layout against a synthetic cooked archive. ARM64/API
28 Release libraries build and link; the APK's resources, label, payload,
alignment, certificate continuity and signature are checked.

Remaining limits: software Vulkan is not the A34's Mali driver, and no ROM or
physical Android device is available here. First startup still prepares caches.
A previously unseen fixed-function pipeline state can require driver creation
on first use, and a new vertex layout can require a universal variant; that can
still hitch. Universal shaders can also be slower while the specialized ones
are pending. This removes the intentional shader-loading pause without claiming
zero stutter, a measured FPS improvement, or complete resolution of the latest
white/black material defects. Existing installations retain their data and
saves; the per-disc manifest is generated when the ROM is extracted again.

APK SHA-256 (version code 17): `f1cdeb73eabc22cce37cd896c5d068465cce35d403eaf081231477893044f899`.

## ROM installation correction — 0.0.4-a34.5

The actual converter scripts shipped in a34.4 deterministically raise
`NameError: name 'process' is not defined` when conversion reaches its worker
loop. `cook.py` now imports that worker. Its module also imports `shutil` and
`thp`, which its copy/movie paths use. These errors are reproduced without
redistributing a game disc, using a synthetic RARC archive, text and THP movie.

The Java task now completes from the synchronous JNI return value. The native
bridge does not invoke `onFinished`; waiting for that callback left a successful
conversion unfinished in the launcher. Every normal/error path reports one
result and removes temporary extracted/cached disc files. A pending-install
marker prevents partially written output from being accepted on next launch.
Converted core files are checked before that marker is removed. Failure text
retains the converter's diagnostic and is selectable on the setup screen.

Material inventory is an optional cache optimization: scanner/write failures
fall back to the existing runtime shader path, rather than invalidating valid
converted data. Manifest writes use a temporary file and atomic replacement;
a failed write preserves an earlier valid inventory. This does not suppress
conversion failures or accept a missing/uncooked Coin archive.

Validation: 13 focused tests pass, including seven executions of the production
Java task with a synchronous fake JNI backend. They cover native success without
a callback, document-URI caching, conversion/extraction/preparation/linkage
failures, incomplete output, diagnostics and cleanup. Isolated Python tests
exercise actual packaged converter modules, original-byte preservation,
archive/copy/movie conversion and invalid-movie failure. The final APK's own
scripts also pass the conversion probe in three modes.

The available host suites report 144 passes and 9 skips (missing native/software
Vulkan tooling or game fixtures); Java compilation/dexing, APK signature,
alignment, payload and label checks pass. No renderer/input native sources were
changed: all 15 ARM64 shared libraries are byte-identical to the validated a34.4
APK. They were reused locally while Java/resources/scripts were repackaged.
No physical A34 or user ROM is available, so a complete on-device extraction is
still unverified.

Version code 18 uses the separate tester ID
`com.openport.galaxyandroid.a34test2`: the prior local signing key is unavailable
after the workspace reset. It installs alongside a34test instead of replacing
it, without requiring deletion of that app or its data. The ROM must be selected
in the new copy. The new tester key is retained privately for future updates;
APK and signing material are excluded from Git.

APK SHA-256: `791e42525c01f60ad816be22b95f216125db00bf1e92f0be6b9f7c9ff3e7943a`.

## Sound-animation conversion correction — 0.0.4-a34.6

The new screenshot establishes a nonzero conversion result, not which asset
failed. A separate before/after reproduction with the actual a34.5 APK scripts
shows that a RARC containing a `.bas` audio animation fails with code 1:
`j3d.swap_sound_anim` was not exported. Embedded sound records in BCK animations
also need the missing `u16` import in `j3d_anim`. Both are now available. A scan
of all 19 bundled Python modules also identified missing `f32`/`normalize`
imports in the collision builder; these imports are fixed without enabling
the disabled model-to-collision replacement option.

Worker and system-data errors now print each diagnostic as one complete write.
The JNI output logger previously split print's separate arguments into separate
messages. Java preserves the first specific failure when the bridge later logs
its generic `cook.convert returned non-zero code` exception. Failed conversion
still fails installation; its error is not suppressed to make setup appear ready.

The converter integration fixture no longer mocks out `cook_dol_data`. It
creates an original synthetic DOL containing a hierarchical error-message RARC
and both required BCSV tables, plus a game archive containing standalone BAS
records and BCK animations with embedded audio. Assertions check the actual
little-endian tables and sound-record bytes, original-input preservation and
clear rejection of a missing system table or invalid movie. The input fails
with the previous APK scripts and succeeds with the corrected ones.

Validation: 18 focused tests pass. The full available tools suite reports 79
passes and 8 skips for unavailable native/software-Vulkan tooling or game
fixtures. All 19 bundled Python modules pass an undefined-name scan. The final
APK's extracted scripts pass the complete conversion fixture in four modes,
including both failure cases. Java/dex, APK signature, identity, label and
alignment checks pass. All 15 ARM64 native libraries remain byte-identical to
a34.5. The user ROM and full physical-device extraction are still unavailable.

Version code 19 retains `com.openport.galaxyandroid.a34test2` and the a34.5
certificate, so this installs as an update to that second tester app without
uninstalling it. APK and signing key remain outside Git.

APK SHA-256: `0a7d513286176737746ef82ef3851567f0de64d37b96a757ddbd603c402fdb92`.
