# Supplied character regression

`njin_animation_asset_test` runs the user-supplied `person.glb` through Njin's
actual main loop and public renderer APIs. The source asset is read from a
runtime path, remains unchanged and is not vendored into the repository or
required by default CI.

The inspected asset has 65 joints, 43 animation clips, 8,547 vertices in two
material primitives, four skin influences per vertex and mixed STEP/LINEAR
channels. Its original opaque gold/purple materials are retained. It does not
contain image textures, so textured-material coverage comes from the separate
material fixture.

```powershell
cmake --build build --target njin_animation_asset_test
build/bin/njin_animation_asset_test.exe --model="PATH/TO/person.glb" --renderer=vulkan --gpu-validation
build/bin/njin_animation_asset_test.exe --model="PATH/TO/person.glb" --renderer=bgfx-opengl
build/bin/njin_animation_asset_test.exe --model="PATH/TO/person.glb" --renderer=d3d12
```

The scene draws current-clip and cross-faded characters plus an instanced group,
with animated shadows, SSAO, HDR, bloom and FXAA. It checks all 43 clips, visible
motion, one geometry upload per prepared mesh across shadow/instance passes,
cleanup and two consecutive engine contexts. Desktop runs use 2,048-pixel sun
cascades. Captures are written beside the executable under `captures/person-*`.
The fixture deliberately checks this supplied rig's two material meshes and
gold/purple visibility; it is not a generic acceptance test for arbitrary GLBs.

Windows Vulkan (with validation), bgfx OpenGL and D3D12 passed all 43 clips.
Android Vulkan/GLES passed six representative clips in two contexts: T-pose,
idle, jog, dance, death and sprint. The mobile `--quick` run uses 512-pixel
cascades; this is not a claim that all 43 clips were tested on the phone.

To include the local asset in a development APK explicitly:

```powershell
python examples/android_renderer/build_apk.py --engine-build build-android-pivot --animation-asset "PATH/TO/person.glb"
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer vulkan --suite person --apk build/android-renderer/njin-renderer-test.apk --out build/device-person-vulkan
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer gles --suite person --out build/device-person-gles
```

The optional packager argument places the asset only in the generated APK's
private test assets. Without that argument the default APK contains only the
repository's synthetic fixtures. The original character remains outside the
Njin source tree.

This real scene exposed a sloped-receiver shadow artifact missed by the earlier
axis-aligned fixtures. The fix and automatic plane regression are documented in
[raster shadows](renderer-shadows.md#sloped-receivers).


Optional `--ssr` enables a reflective floor in the same animated scene. Six
representative clips with cross-fade, instanced crowd, shadows, SSAO/HDR/bloom/FXAA
and SSR passed on Windows Vulkan (validation), bgfx OpenGL and ordinary D3D12,
two contexts per backend. The six deformed-mesh uploads per frame are reused
across all passes. D3D12 GPU-validation instrumentation was stopped after a slow
first frame and is not counted as a pass; ordinary D3D12 completed successfully.
This optional SSR run does not replace the earlier full 43-clip animation test.

```powershell
build/bin/njin_animation_asset_test.exe --model="PATH/TO/person.glb" --renderer=vulkan --quick --ssr --gpu-validation
```


The Android controller also accepts `--suite person --ssr`; it verifies the
native log explicitly confirms SSR was enabled. Package the supplied asset
with the optional `--animation-asset` argument first.

```powershell
python examples/android_renderer/run_device.py --serial DEVICE_SERIAL --renderer vulkan --suite person --ssr --apk build/android-renderer/njin-renderer-test.apk --out build/device-person-ssr-vulkan
```

Both Android Vulkan and GLES passed the combined real-asset SSR scene, six
representative clips and two contexts each. Evidence is in
`build/android-device-captures/person-ssr-{vulkan,gles}/status.txt` and `logcat.txt`;
the controller checks both `PASS 0` and the native SSR-enabled marker.
