"""Run the installed Njin test app and retrieve its own engine captures.

--lifecycle backgrounds/resumes this app; --rotate then rotates its Activity.
No global display/rotation settings or unrelated application data are changed.
"""
import argparse
from pathlib import Path
import re
import struct
import subprocess
import time

PACKAGE = "org.njin.renderertest"
ACTIVITY = PACKAGE + "/.NjinActivity"


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--serial", required=True)
    parser.add_argument("--renderer", choices=["vulkan", "gles"], required=True)
    parser.add_argument("--suite", choices=["models", "shadows", "animation", "environment", "materials", "post", "ssao", "ssr", "effects", "smaa", "exposure", "oit", "mipmaps", "streaming", "residency", "temporal", "render_textures", "atlas", "virtual_screen", "video", "dynamic_mesh", "lod", "person", "custom_shader", "sprite_fx", "gizmos", "crisp_text", "world_fx", "renderer_batch", "world3d", "advanced_material", "backdrop", "light2d", "gpu_particles", "clustered", "raytrace", "terrain_relief", "ocean", "occlusion", "instancing", "atmosphere", "atmosphere_full", "lightmap", "lightmap_model"], default="models")
    parser.add_argument("--apk", type=Path)
    parser.add_argument("--out", type=Path, required=True)
    parser.add_argument("--timeout", type=float, default=180, help="Native test deadline in seconds (180 by default)")
    parser.add_argument("--lifecycle", action="store_true")
    parser.add_argument("--rotate", action="store_true")
    parser.add_argument("--input", action="store_true", help="Inject a swipe into this test Activity and assert Njin touch events")
    parser.add_argument("--audio", action="store_true", help="Initialize audio and upload/play/unload a silent PCM loop")
    parser.add_argument("--ssr", action="store_true", help="Enable SSR in the supplied-person animation suite")
    args = parser.parse_args()
    if not 0 < args.timeout <= 3600:
        parser.error("--timeout must be greater than zero and at most 3600 seconds")
    if args.ssr and args.suite!="person":
        parser.error("--ssr belongs to the person suite; the ssr suite already tests reflections")
    if args.rotate and not args.lifecycle:
        parser.error("--rotate requires --lifecycle")
    if args.input and not args.rotate:
        parser.error("--input requires --lifecycle --rotate")
    if args.suite!="models" and (args.input or args.audio):
        parser.error("Input/audio controls belong to the models suite")
    if args.suite not in ("models", "shadows") and args.lifecycle:
        parser.error("Lifecycle controls are available in models/shadows; these suites test two contexts")
    output = args.out.resolve()
    output.mkdir(parents=True, exist_ok=True)
    (output / "result.txt").write_text("RUNNING\n", encoding="utf-8")
    prefix = ["adb", "-s", args.serial]

    def adb(*command, required=True):
        result = subprocess.run(prefix + list(command), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        if required and result.returncode:
            raise RuntimeError(result.stderr.decode("utf-8", errors="replace"))
        return result.stdout

    if args.apk:
        # --no-incremental: an incremental (streamed-later) install left the activity
        # unresolvable on a vivo Android 13 device ("Activity class does not exist").
        print(adb("install", "-r", "-t", "--no-incremental", str(args.apk.resolve())).decode(), flush=True)
    adb("shell", "am", "force-stop", PACKAGE)
    # These are only this harness's own status files, never application data
    # from another package. Removing them prevents a stale PASS being accepted.
    # On a fresh install (Android 13+) the data directory only exists after the first
    # launch, so there is nothing stale to remove and run-as fails: not an error.
    adb("shell", "run-as", PACKAGE, "rm", "-f", "files/result.txt", "files/frame-ready.txt", "files/input-ready.txt", required=False)
    command = ["shell", "am", "start", "-W", "-n", ACTIVITY, "--es", "renderer", args.renderer, "--es", "suite", args.suite]
    if args.ssr:
        command.extend(["--ez", "ssr", "true"])
    if args.lifecycle:
        command += ["--ei", "frames", "360", "--ez", "lifecycle", "true"]
    if args.input:
        command += ["--ez", "input_smoke", "true"]
    if args.audio:
        command += ["--ez", "audio_smoke", "true"]
    print(adb(*command).decode(), flush=True)
    deadline = time.monotonic() + args.timeout
    lifecycle_done = False
    input_sent = False
    process = None
    last_alive = time.monotonic()
    controller_passed = False
    result = ""
    if args.suite in ("models", "shadows"):
        capture_prefix=("shadows-" if args.suite=="shadows" else "model-")+args.renderer+".png"
    else:
        capture_stem={"lightmap_model":"lightmap-model", "render_textures":"render-target", "virtual_screen":"virtual", "dynamic_mesh":"dynamic-mesh", "custom_shader":"custom-shader", "sprite_fx":"sprite-fx", "crisp_text":"crisp-text", "world_fx":"world-fx", "renderer_batch":"renderer-batch", "advanced_material":"advanced-materials", "gpu_particles":"gpu-particles", "terrain_relief":"terrain-relief", "atmosphere_full":"atmosphere-full"}.get(args.suite,args.suite)
        separator=".png" if args.suite in ("materials","mipmaps","ocean") else "-" if args.suite in ("virtual_screen","crisp_text","terrain_relief","occlusion","lightmap") else "."
        capture_prefix=capture_stem+"-"+args.renderer+separator

    def collect_captures():
        if args.suite in ("streaming", "residency"):
            directory = "files/stream-fixtures" if args.suite == "streaming" else "files/residency-fixtures"
            names = adb("shell", "run-as", PACKAGE, "ls", directory, required=False).decode().splitlines()
            for name in names:
                if name.endswith(("-stream.png", "-sync.png", "-resident.png", "-reference.png", "-metrics.txt")) and re.fullmatch(r"[a-zA-Z0-9_.-]+", name):
                    (output / name).write_bytes(adb("exec-out", "run-as", PACKAGE, "cat", directory + "/" + name))
            return
        names = adb("shell", "run-as", PACKAGE, "ls", "files/captures", required=False).decode().splitlines()
        for name in names:
            if name.startswith(capture_prefix) and re.fullmatch(r"[a-zA-Z0-9_.-]+", name):
                (output / name).write_bytes(adb("exec-out", "run-as", PACKAGE, "cat", "files/captures/" + name))

    try:
        while time.monotonic() < deadline:
            current = adb("shell", "pidof", PACKAGE, required=False).decode().strip()
            if current and process is None:
                process = current
            if current:
                last_alive = time.monotonic()
            if current and process and current != process:
                raise RuntimeError("Njin process restarted during the test")
            result = adb("exec-out", "run-as", PACKAGE, "cat", "files/result.txt", required=False).decode(errors="replace").strip()
            if result.startswith("FAIL"):
                raise RuntimeError(result)
            if args.lifecycle and not lifecycle_done:
                ready = adb("exec-out", "run-as", PACKAGE, "cat", "files/frame-ready.txt", required=False)
                if ready.strip() == b"READY":
                    print("Backgrounding the initialized Njin Activity", flush=True)
                    adb("shell", "input", "keyevent", "KEYCODE_HOME")
                    time.sleep(2)
                    print(adb("shell", "am", "start", "-W", "-n", ACTIVITY).decode(), flush=True)
                    if args.rotate:
                        print(adb("shell", "am", "start", "-W", "-n", ACTIVITY, "--es", "orientation", "portrait").decode(), flush=True)
                    lifecycle_done = True
            if args.input and lifecycle_done and not input_sent:
                metrics = adb("exec-out", "run-as", PACKAGE, "cat", "files/input-ready.txt", required=False).decode().split()
                if len(metrics)==2 and all(value.isdigit() for value in metrics):
                    width, height = map(int, metrics)
                    adb("shell", "input", "touchscreen", "swipe", str(width//2), str(height//3), str(width//2), str(height//2), "400")
                    input_sent = True
            if result.startswith("PASS 0"):
                if args.lifecycle and not lifecycle_done:
                    raise RuntimeError("Test ended before the lifecycle transition was exercised")
                if args.input and not input_sent:
                    raise RuntimeError("Test ended before its touch gesture was injected")
                break
            if process and not current and time.monotonic() - last_alive > 5:
                raise RuntimeError("Njin process exited before completing the test")
            time.sleep(0.5)
        else:
            raise RuntimeError("Njin Android test timed out; see captured logs")
        collect_captures()
        if args.rotate:
            image = (output / (capture_prefix+".lifecycle.png")).read_bytes()
            width, height = struct.unpack(">II", image[16:24])
            if height <= width:
                raise RuntimeError("Lifecycle capture did not acknowledge portrait orientation")
        backend_logs = adb("logcat", "-d", "--pid=" + (process or "0"), "-v", "brief").decode(errors="replace")
        expected = "Renderer: Vulkan (" if args.renderer == "vulkan" else "Renderer: OpenGL ES "
        if expected not in backend_logs:
            raise RuntimeError("bgfx did not confirm the requested backend in this process's log")
        if args.ssr and "Real-asset SSR: enabled" not in backend_logs:
            raise RuntimeError("The native person suite did not confirm SSR was enabled")
        controller_passed = True
        (output / "result.txt").write_text(result + "\n", encoding="utf-8")
        print(result, "captures:", output, flush=True)
    finally:
        if not controller_passed:
            # Keep failure evidence too; a native assertion must not discard
            # the images needed to diagnose a backend-specific regression.
            try:
                collect_captures()
            except Exception as capture_error:
                (output / "capture-error.txt").write_text(str(capture_error), encoding="utf-8")
        logs = adb("logcat", "-d", "--pid=" + process, "-v", "brief", required=False) if process else adb("logcat", "-d", "-v", "brief", "-s", "NjinAndroid", required=False)
        (output / "logcat.txt").write_bytes(logs)
        (output / "status.txt").write_text("renderer=" + args.renderer + "\nprocess=" + str(process) + "\nlast_result=" + result + "\nlifecycle=" + str(lifecycle_done) + "\ninput_sent=" + str(input_sent) + "\naudio_requested=" + str(args.audio) + "\ncontroller_passed=" + str(controller_passed) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
