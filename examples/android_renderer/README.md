# Android renderer regression

This APK runs `src/engine/tests/models3d.cpp` through the real Njin main loop,
SDLActivity/SDL3 and bgfx. It uses the same scene and public rendering API as
desktop tests. It is a development test package, not a production game template.

Mobile verification baseline (2026-11-18): Pixel 6a is the required device for
Vulkan and GLES runs. Older devices are optional compatibility coverage; their
absence does not block this baseline. Keep historical device results labeled
with the device and backend that actually ran.

`--suite atmosphere_full` exercises multiple scattering, RGB aerial perspective,
asynchronous SH9/GGX, lunar phases, stars, IBL update cadence and resource cleanup.
It requires both native `PASS 0` and `controller_passed=True` on Pixel 6a.

Requirements: Python 3, CMake 3.28+, Ninja, JDK 21, Android SDK platform 35 and
build-tools, Android NDK (29 tested), platform-tools/adb, and a desktop build of
bgfx shaderc. Set `ANDROID_HOME`, `ANDROID_NDK_HOME`, `JAVA_HOME`, and include
Java, CMake, Ninja and adb in PATH. No Gradle/Maven downloads are required.

First build host `shaderc` and the arm64/API 26 engine as described in
[renderer-migration.md](../../docs/renderer-migration.md). Android uses shared
SDL3 so Java and Njin share one SDL/JNI state. The packaging tool discovers the
matching SDL sources from the engine CMake cache; keep that dependency checkout
available when packaging.

The packaging tool builds all engine targets before linking its imported static
archives. This is required after context layout changes in Release builds with
`NJIN_CTX_INLINE`; a selective core/3D rebuild can leave stale offsets in other
features. Additional suites now include `world3d`, `advanced_material`,
`backdrop`, `light2d`, `gpu_particles` and `clustered`; see the
[integration scope](../../docs/renderer-completion-batch.md).

`--suite instancing` runs the desktop instancing fixture with a fixed 640x480
render target, so pixel controls do not depend on the phone's fullscreen size.
`--suite occlusion` runs image A/B controls and reads actual GPU counts through
Njin's debug API; `occlusion-<backend>-checks.txt` is collected beside captures.
An unsupported capability is a native failure, never a controller pass.

```powershell
python examples/android_renderer/build_apk.py --engine-build build-android
adb devices -l
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer vulkan `
  --apk build/android-renderer/njin-renderer-test.apk --out build/device-vulkan
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer gles `
  --out build/device-gles --lifecycle --rotate --input --audio
```

The first run installs the debug-signed `org.njin.renderertest` package. The
controller force-stops only that package, clears its own status files, launches
the selected backend and collects its private logs and Njin PNG captures. A
normal run checks two consecutive engine contexts. `--lifecycle` uses one longer
run, backgrounds/resumes the initialized Activity, and `--rotate` requests
portrait for that Activity. It verifies the PID did not restart, native pixel
assertions passed, and the final capture acknowledged portrait. It does not
change global display settings. `--input` requires rotation/lifecycle and injects a
swipe into the test Activity; Njin asserts press/motion/release and logical window
coordinates. `--audio` uploads/plays/unloads a silent PCM loop through the public
audio API. Outputs include `result.txt`, `status.txt` and
`logcat.txt`; a native PASS alone is insufficient if `controller_passed=False`.

The controller confirms the backend from the `I/NjinBgfx` "Renderer:" line. Some
vendor images keep only errors in logcat (`getprop log.tag` prints `E`, seen on a
vivo Android 13 phone); allow the tag for the session with
`adb shell setprop log.tag.NjinBgfx I` (it resets on reboot). APKs are installed
with `--no-incremental`, since an incremental install left the activity
unresolvable on that phone.

Verified on 2026-11-03: one connected arm64 device reporting Android API 30 and
Mali-T880, Vulkan 1.0.82. Vulkan and GLES both passed textured sprites, blending,
static/procedural meshes, depth, constant PBR/HDR, local lights, resource unload,
two-context startup/shutdown, background/resume and portrait resize. Captures
were retrieved through the engine API. Android Vulkan validation layers were
not enabled. Touch press/motion/release and silent audio passed on both backends,
including two-context audio startup/shutdown. This exposed a real raylib teardown
race: its mixer mutex was destroyed before the audio callback stopped.
A guarded source fix to raylib stopped/joined callbacks before releasing that
lock; sound now goes through Njin's own miniaudio backend
(`njin_audio_backend.cpp`), which uninitialises the device before anything its
callback reads, so that fix is gone with raudio. The first
audio run crashed and is retained in `build/android-vulkan-input-audio.log`;
subsequent fixed runs passed, not an uninterrupted first attempt.
Gamepad, audible output quality, device loss, production packaging and other
devices/ABIs remain unverified; this is not complete Android platform support.

The APK keeps assets in app-private files extracted by the Activity. Its CMake
adapter removes raylib's NativeActivity-specific `fopen` wrapper only for this
SDLActivity consumer. The engine still retains the independent legacy renderer;
this APK exercises bgfx Vulkan/GLES, not raylib's Android window lifecycle.


Unavailable-backend reporting was also checked on the device: selecting Metal
returns `requested backend was not compiled: metal`, not a silent GLES/Vulkan
fallback. This differs from testing a missing/broken Vulkan runtime; the device's
Vulkan runtime is available. `build/android-device-captures/unavailable-metal`
contains that negative check's log.


`--suite shadows` selects the shared desktop shadow regression instead of static
models. It checks sun cascades, point/spot maps, six point faces, alpha cutouts,
cast/receive/shadow-only flags, mirrored/moving casters, PCF, HDR, target contents,
resource cleanup and resize. `--lifecycle --rotate` also verifies background/resume
and portrait on that scene. Input/audio options remain in `--suite models` (the
default). See [shadow rendering](../../docs/renderer-shadows.md).


Additional shared scene suites are available: `--suite materials`,
`--suite environment`, `--suite animation` and `--suite post`. Each runs two
consecutive Njin contexts and retrieves its engine captures. Their probes use
actual capture dimensions; the post suite leaves native resize to desktop tests.
Lifecycle/rotation and touch/audio controls remain in the existing models/shadows
suites. Selecting an unsupported controller option reports an error.

```powershell
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer vulkan `
  --suite animation --apk build/android-renderer/njin-renderer-test.apk --out build/device-animation
```

These suites exercise CPU skinning/morphs and GPU instance-buffer reuse, textured
material maps, float16 IBL and HDR bloom/FXAA through the same public APIs used
on desktop. Check each run's `status.txt`: compilation alone is not runtime proof.


`--suite ssr` runs the same portable SSR scene as desktop: perspective and
orthographic colored reflections, nonreflective occluders, roughness/miss/sky
controls, composition with SSAO/shadows/bloom/FXAA, resource accounting and two
engine contexts. Mobile omits the desktop native-window resize assertion.

```powershell
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer vulkan --suite ssr --apk build/android-renderer/njin-renderer-test.apk --out build/device-ssr-vulkan
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer gles --suite ssr --out build/device-ssr-gles
```

Both Vulkan and GLES passed on the available Mali-T880 device. This does not
establish compatibility with every Android GPU or a production game package.

`--suite effects` runs 22 scene controls for depth outlines (width/color/crease/
fade and orthographic projection), sun shafts, depth-occluded flare, halo and
strength, zero-intensity/behind-camera/reset behavior, HDR-off rendering,
SSAO/SSR/shadows/bloom/FXAA composition, later UI and cached resources. It runs
two engine contexts; desktop-only native-window size changes are omitted.

```powershell
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer vulkan --suite effects --apk build/android-renderer/njin-renderer-test.apk --out build/device-effects-vulkan
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer gles --suite effects --out build/device-effects-gles
```


### 3D FX and text suites

`--suite world_fx` exercises manual/ECS flash, dissolve/sweep, camera masking,
HDR/OIT composition and shadow-caster disappearance through the engine's
renderer. `--suite crisp_text` checks virtual-resolution text, clipping,
occlusion and font lifetime. Both accept `--renderer vulkan` or `gles`.

```powershell
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer vulkan --suite world_fx --out build/device-world-fx-vulkan
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer gles --suite world_fx --out build/device-world-fx-gles
```

### Additional migrated renderer suites

The same Njin fixtures are available as `--suite smaa`, `exposure`, `oit`,
`mipmaps` and `temporal`. They check original three-pass SMAA, GPU exposure
adaptation, order-independent transparency and opaque depth rejection,
role-aware glTF mipmaps, and temporal AA/motion vectors respectively. Temporal
controls include camera/object/bone/morph motion, a camera-only instanced
occluder, unchanged static output, later UI and two contexts. Desktop resize
and mobile Activity lifecycle are separate controls; these suites do not accept
`--lifecycle`, `--rotate`, touch/audio or the person-only SSR option.

```powershell
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer vulkan --suite temporal --apk build/android-renderer/njin-renderer-test.apk --out build/device-temporal-vulkan
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer gles --suite temporal --out build/device-temporal-gles
```

Each suite must return native `PASS 0` **and** `controller_passed=True` in its
status file. The controller also verifies the requested bgfx driver in that
process's log; presentation alone is not a passing renderer test. Images are
created by Njin's capture API and retrieved from this test application's private
storage. Vulkan/GLES on one Mali-T880 device does not verify every Android GPU
or provide Vulkan validation-layer coverage.

The `custom_shader`, `sprite_fx` and `gizmos` suites reuse the public desktop
fixtures. `build_apk.py` compiles SPIR-V/ESSL shader-test packages using matching
`NJIN_BGFX_SHADERC` and `BGFX_DIR` entries from the engine cache. Packages are
bundled and extracted into private `shader-test-assets`; no runtime compilation
or absolute development-machine asset paths are used. Run, for example:

```powershell
python examples/android_renderer/run_device.py --serial <serial> --renderer vulkan --suite sprite_fx --out build/android-sprite-fx
```

The three new suites passed on the attached Mali-T880 device with Vulkan and
GLES; those six native suites report `PASS 0` and controller confirmation.
