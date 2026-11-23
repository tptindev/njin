"""Configure an isolated Release build using Njin's existing pinned checkouts."""
import argparse
import os
from pathlib import Path
import shutil
import subprocess

ROOT = Path(__file__).resolve().parents[3]
parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument("--build", type=Path, default=ROOT / "build-renderer-bench")
parser.add_argument("--deps", type=Path, default=ROOT / "build" / "_deps")
parser.add_argument("--toolchain", type=Path, default=Path("C:/Dev/raylib/w64devkit/bin"))
args = parser.parse_args()
deps = args.deps.resolve()
cmd = ["cmake", "-S", str(ROOT), "-B", str(args.build.resolve()), "-G", "Ninja", "-DCMAKE_BUILD_TYPE=Release",
       "-DNJIN_BUILD_RENDERER_BENCHMARK=ON", "-DNJIN_BUILD_RHI_SLICE=ON", "-DNJIN_BUILD_EXAMPLES=OFF",
       "-DNJIN_BUILD_INSPECTOR=OFF", "-DNJIN_BUILD_UI_EDITOR=OFF", "-DNJIN_BUILD_ANIM_EDITOR=OFF",
       "-DCMAKE_DISABLE_PRECOMPILE_HEADERS=ON", "-DNJIN_PCH=OFF", "-DNJIN_CCACHE=OFF",
       "-DFETCHCONTENT_FULLY_DISCONNECTED=ON"]
for name in ("asio", "enet", "entt", "joltphysics", "lua", "meshoptimizer", "pl_mpeg", "raylib",
             "recastnavigation", "smaa", "sol2", "tinybvh", "xatlas", "njin_bgfx", "sdl3"):
    folder = "bgfx-cmake-src" if name == "njin_bgfx" else "sdl3-3.4.18-src" if name == "sdl3" else name + "-src"
    path = deps / folder
    if not path.is_dir():
        raise SystemExit(f"Missing existing checkout: {path}. Populate pinned dependencies with the normal Njin configure first.")
    cmd.append(f"-DFETCHCONTENT_SOURCE_DIR_{name.upper()}={path.as_posix()}")
for name in ("bgfx", "bimg", "bx"):
    cmd.append(f"-D{name.upper()}_DIR={(deps / (name + '-src')).as_posix()}")
if os.name == "nt":
    for variable, name in (("CMAKE_C_COMPILER", "cc.exe"), ("CMAKE_CXX_COMPILER", "c++.exe"), ("CMAKE_MAKE_PROGRAM", "ninja.exe")):
        path = args.toolchain / name
        if not path.exists():
            raise SystemExit(f"Tool not found: {path}")
        cmd.append(f"-D{variable}={path.as_posix()}")
shaderc = deps.parent / "bin" / ("shaderc.exe" if os.name == "nt" else "shaderc")
if shaderc.exists():
    cmd.append(f"-DNJIN_BGFX_SHADERC={shaderc.as_posix()}")
print(subprocess.list2cmdline(cmd), flush=True)
subprocess.run(cmd, cwd=ROOT, check=True)
