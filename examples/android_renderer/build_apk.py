"""Build the Njin regression APK with an existing matching Android engine build.

Uses SDK/NDK tools directly, without downloading Gradle or Maven dependencies.
Does not install or run the APK automatically.
"""
import argparse
import os
from pathlib import Path
import re
import subprocess
import sys
import zipfile


def run(args, **kwargs):
    subprocess.run([str(value) for value in args], check=True, **kwargs)


def version(path):
    return tuple(int(v) for v in re.findall(r"\d+", path.name))


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--engine-build", required=True, type=Path)
    parser.add_argument("--sdk", type=Path, default=os.environ.get("ANDROID_HOME") or os.environ.get("ANDROID_SDK_ROOT"))
    parser.add_argument("--ndk", type=Path)
    parser.add_argument("--out", type=Path)
    parser.add_argument("--animation-asset", type=Path, help="Optional local GLB for the person suite; never vendored into source")
    args = parser.parse_args()
    source = Path(__file__).resolve().parent
    root = source.parent.parent
    engine = args.engine_build.resolve()
    sdk = args.sdk
    if sdk is None:
        parser.error("Set ANDROID_HOME or pass --sdk")
    sdk = sdk.resolve()
    ndk = args.ndk or max((sdk / "ndk").iterdir(), key=version)
    tools = max((sdk / "build-tools").iterdir(), key=version)
    android_jar = sdk / "platforms/android-35/android.jar"
    output = (args.out or root / "build/android-renderer").resolve()
    output.mkdir(parents=True, exist_ok=True)
    cache = (engine / "CMakeCache.txt").read_text(encoding="utf-8")
    # Imported libraries have no source dependency edges in the APK build.
    # NJIN_CTX_INLINE makes context layout changes affect every feature library.
    # Rebuild all of them before linking, so stale member offsets cannot survive.
    run(["cmake", "--build", engine, "--parallel", "3"])
    match = re.search(r"^FETCHCONTENT_SOURCE_DIR_SDL3:(?:PATH|UNINITIALIZED)=(.+)$", cache, re.M)
    if match is None or not match[1].strip():
        match = re.search(r"^SDL3_SOURCE_DIR:STATIC=(.+)$", cache, re.M)
    if match is None:
        raise RuntimeError("Engine cache must identify its matching SDL3 source")
    sdl = Path(match[1].strip())
    if not (sdl / "android-project/app/src/main/java/org/libsdl/app/SDLActivity.java").is_file():
        raise RuntimeError("Matching SDL3 Android Java sources are missing: " + str(sdl))
    shaderc_match = re.search(r"^NJIN_BGFX_SHADERC:(?:FILEPATH|UNINITIALIZED)=(.+)$", cache, re.M)
    bgfx_match = re.search(r"^BGFX_DIR:(?:PATH|UNINITIALIZED)=(.+)$", cache, re.M)
    if shaderc_match is None or bgfx_match is None:
        raise RuntimeError("APK shader fixture requires matching host NJIN_BGFX_SHADERC and BGFX_DIR in engine cache")
    shader_assets = output / "shader-test-assets"
    run([sys.executable, root / "src/engine/tests/compile_shader_fixture.py",
         "--shaderc", shaderc_match[1].strip(), "--bgfx-source", bgfx_match[1].strip(), "--output", shader_assets])
    native = output / "native"
    run(["cmake", "-S", source, "-B", native, "-G", "Ninja",
         f"-DCMAKE_TOOLCHAIN_FILE={ndk}/build/cmake/android.toolchain.cmake",
         "-DANDROID_ABI=arm64-v8a", "-DANDROID_PLATFORM=android-26",
         "-DCMAKE_BUILD_TYPE=Release", "-DNJIN_PCH=OFF", "-DNJIN_CCACHE=OFF",
         f"-DNJIN_PREBUILT={engine}"])
    run(["cmake", "--build", native, "--target", "main", "--parallel", "3"])
    java_sources = list((sdl / "android-project/app/src/main/java").rglob("*.java"))
    java_sources.append(source / "NjinActivity.java")
    classes = output / "classes"
    classes.mkdir(exist_ok=True)
    run(["javac", "-source", "8", "-target", "8", "-Xlint:-options", "-classpath", android_jar,
         "-d", classes, *java_sources])
    jar = output / "classes.jar"
    run(["jar", "cf", jar, "-C", classes, "."])
    dex = output / "dex"
    dex.mkdir(exist_ok=True)
    run(["java", "-cp", tools / "lib/d8.jar", "com.android.tools.r8.D8",
         "--min-api", "26", "--lib", android_jar, "--output", dex, jar])
    executable = ".exe" if os.name == "nt" else ""
    unsigned = output / "unsigned.apk"
    run([tools / ("aapt2" + executable), "link", "-o", unsigned,
         "--manifest", source / "AndroidManifest.xml", "-I", android_jar])
    libraries = {"libmain.so": native / "libmain.so", "libSDL3.so": engine / "bin/libSDL3.so"}
    if not libraries["libSDL3.so"].is_file():
        raise RuntimeError("Build the Android engine with shared SDL3 before packaging")
    host = "windows-x86_64" if os.name == "nt" else "darwin-x86_64" if os.uname().sysname == "Darwin" else "linux-x86_64"
    packaged = output / "libraries"
    packaged.mkdir(exist_ok=True)
    strip = ndk / f"toolchains/llvm/prebuilt/{host}/bin/llvm-strip{executable}"
    for name, file in list(libraries.items()):
        destination = packaged / name
        run([strip, "--strip-unneeded", "-o", destination, file])
        libraries[name] = destination
    cpp = ndk / f"toolchains/llvm/prebuilt/{host}/sysroot/usr/lib/aarch64-linux-android/libc++_shared.so"
    if cpp.is_file():
        libraries[cpp.name] = cpp
    with zipfile.ZipFile(unsigned, "a", zipfile.ZIP_DEFLATED) as apk:
        apk.write(dex / "classes.dex", "classes.dex")
        for name, file in libraries.items():
            apk.write(file, "lib/arm64-v8a/" + name)
        for file in sorted((root / "third_party/licenses").glob("*.txt")):
            apk.write(file,"assets/licenses/" + file.name)
        apk.write(root / "THIRD_PARTY_NOTICES.md","assets/licenses/THIRD_PARTY_NOTICES.md")
        for file in sorted((root / "src/engine/tests/assets").iterdir()):
            if file.is_file():
                apk.write(file, "assets/model-test-assets/" + file.name)
        for file in sorted(shader_assets.iterdir()):
            if file.is_file():
                apk.write(file, "assets/shader-test-assets/" + file.name)
        if args.animation_asset:
            if not args.animation_asset.is_file():
                raise FileNotFoundError(args.animation_asset)
            apk.write(args.animation_asset, "assets/model-test-assets/external-animation.glb")
    aligned = output / "aligned.apk"
    run([tools / ("zipalign" + executable), "-f", "4", unsigned, aligned])
    key = output / "debug.keystore"
    if not key.exists():
        run(["keytool", "-genkeypair", "-keystore", key, "-storepass", "android",
             "-keypass", "android", "-alias", "androiddebugkey", "-keyalg", "RSA",
             "-validity", "3650", "-dname", "CN=Njin Android Test"])
    apk = output / "njin-renderer-test.apk"
    run(["java", "-jar", tools / "lib/apksigner.jar", "sign", "--ks", key,
         "--ks-pass", "pass:android", "--key-pass", "pass:android", "--out", apk, aligned])
    run(["java", "-jar", tools / "lib/apksigner.jar", "verify", apk])
    print(apk)


if __name__ == "__main__":
    main()
