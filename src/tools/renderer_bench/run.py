"""Run sequential, alternating A/B trials. No device/power/cache settings changed."""
import argparse
import datetime
import hashlib
import json
import os
from pathlib import Path
import platform
import re
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]


def sha(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def command(args, **kwargs):
    return subprocess.run(args, cwd=ROOT, capture_output=True, **kwargs)


def snapshot():
    paths = sorted(set(p for directory in (ROOT / "src", ROOT / "cmake")
                       for p in directory.rglob("*") if p.is_file() and "__pycache__" not in p.parts))
    paths += [ROOT / "CMakeLists.txt", ROOT / "THIRD_PARTY_NOTICES.md"]
    hashes = {p.relative_to(ROOT).as_posix(): sha(p) for p in paths}
    dependencies = {}
    for name in ("bgfx", "bx", "bimg", "sdl3-3.4.18", "meshoptimizer", "raylib"):
        path = ROOT / "build" / "_deps" / (name + "-src")
        if path.is_dir():
            def git(*args):
                return command(["git", "-C", str(path), *args]).stdout
            files = {p.relative_to(path).as_posix(): sha(p) for p in sorted(path.rglob("*"))
                     if p.is_file() and ".git" not in p.relative_to(path).parts
                     and (p.suffix in (".c", ".cpp", ".h", ".hpp", ".inl", ".sc", ".glsl", ".cmake") or p.name == "CMakeLists.txt")}
            dependencies[name] = {"source_sha256": hashlib.sha256(json.dumps(files, sort_keys=True).encode()).hexdigest()}
            if (path / ".git").exists():
                dependencies[name].update({"head": git("rev-parse", "HEAD").decode().strip(),
                                          "status": git("status", "--porcelain=v1").decode(errors="replace"),
                                          "diff_sha256": hashlib.sha256(git("diff", "HEAD", "--binary")).hexdigest()})
    return {"head": command(["git", "rev-parse", "HEAD"]).stdout.decode().strip(),
            "status": command(["git", "status", "--porcelain=v1"]).stdout.decode(),
            "unstaged_diff_sha256": hashlib.sha256(command(["git", "diff", "--binary"]).stdout).hexdigest(),
            "staged_diff_sha256": hashlib.sha256(command(["git", "diff", "--cached", "--binary"]).stdout).hexdigest(),
            "files": hashes, "dependencies": dependencies,
            "tree_sha256": hashlib.sha256(json.dumps(hashes, sort_keys=True).encode()).hexdigest()}


def system_info():
    info = {"platform": platform.platform(), "processor": platform.processor(),
            "logical_cpus": os.cpu_count(), "thermal": "unavailable",
            "background_load": "uncontrolled; CIM snapshots outside timed runs",
            "power_clock_changes": False}
    if os.name == "nt":
        script = ("@{cpu=@(Get-CimInstance Win32_Processor | Select-Object Name,LoadPercentage,NumberOfCores);"
                  "ram=Get-CimInstance Win32_ComputerSystem | Select-Object TotalPhysicalMemory;"
                  "os=Get-CimInstance Win32_OperatingSystem | Select-Object Caption,Version,BuildNumber;"
                  "gpu=@(Get-CimInstance Win32_VideoController | Select-Object Name,DriverVersion,DriverDate,AdapterRAM)} | ConvertTo-Json -Depth 5")
        result = command(["powershell", "-NoProfile", "-Command", script], timeout=45)
        try:
            info["windows"] = json.loads(result.stdout)
        except (ValueError, UnicodeDecodeError):
            info["cim_error"] = result.stderr.decode(errors="replace")
    return info


def write(path, value):
    path.write_text(json.dumps(value, indent=2, ensure_ascii=False) + "\n", encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--exe", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--backends", nargs="+", default=["vulkan", "bgfx-opengl", "d3d12"])
    parser.add_argument("--workloads", nargs="+", default=["low", "visible", "occluded", "groups", "moving", "shadows"])
    parser.add_argument("--variants", nargs="+", default=["cpu", "gpu"])
    parser.add_argument("--mesh", choices=["cube", "sphere"], default="cube")
    parser.add_argument("--repeats", type=int, default=3)
    parser.add_argument("--warmup", type=int, default=180)
    parser.add_argument("--frames", type=int, default=600)
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--height", type=int, default=480)
    parser.add_argument("--skip-correctness", action="store_true")
    parser.add_argument("--timeout", type=int, default=180)
    args = parser.parse_args()
    args.exe = args.exe.resolve()
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=True)
    manifest_path = args.output / "manifest.json"
    if manifest_path.exists():
        raise SystemExit("Choose a new output directory; existing trials are preserved")
    manifest = {"schema": "njin.renderer-suite/1", "started_utc": datetime.datetime.now(datetime.timezone.utc).isoformat(),
                "executable": str(args.exe), "executable_sha256": sha(args.exe),
                "source_before": snapshot(), "system_before": system_info(), "runs": [],
                "method": "separate processes; correctness first; alternate variant order each repeat; no disk cache flush",
                "unmeasured": ["atmosphere LUT/update/steady state", "lightmap bake/load/runtime", "streaming cold/warm/residency",
                               "physical Linux GPU", "Android performance", "multi-light workloads", "pool/dispatch policy variants"]}
    cache = args.exe.parent.parent / "CMakeCache.txt"
    if cache.exists():
        manifest["cmake_cache_sha256"] = sha(cache)
        (args.output / "CMakeCache.txt").write_bytes(cache.read_bytes())
    compiler_path = "c++"
    if cache.exists():
        for line in cache.read_text().splitlines():
            if line.startswith("CMAKE_CXX_COMPILER:"):
                compiler_path = line.split("=", 1)[1]
    compiler = command([compiler_path, "--version"])
    manifest["compiler"] = compiler.stdout.decode(errors="replace")
    write(manifest_path, manifest)

    def run(backend, workload, variant, repeat, correctness):
        name = f"{backend}-{workload}-{variant}-{'check' if correctness else repeat}"
        prefix = args.output / name
        cmd = [str(args.exe), f"--renderer={backend}", f"--workload={workload}", f"--variant={variant}",
               f"--output={prefix}", f"--warmup={args.warmup}", f"--frames={max(90, min(args.frames, 120)) if correctness else args.frames}",
               f"--width={args.width}", f"--height={args.height}", f"--mesh={args.mesh}", "--seed=17"]
        environment = os.environ.copy()
        if correctness:
            cmd.append("--capture")
            if backend == "vulkan":
                cmd.append("--gpu-validation")
                # The Release bgfx callback uses BX_TRACE, which may be compiled
                # out. Request the layer's independent stdout channel instead.
                environment["VK_LAYER_SETTINGS_PATH"] = str(Path(__file__).parent / "validation")
        print(name, flush=True)
        started = time.monotonic()
        with prefix.with_suffix(".log").open("wb") as log:
            try:
                result = subprocess.run(cmd, cwd=args.exe.parent, env=environment, stdout=log, stderr=subprocess.STDOUT, timeout=args.timeout)
                status = result.returncode
            except subprocess.TimeoutExpired:
                status = "timeout"
        diagnostics = prefix.with_suffix(".log").read_text(errors="replace")
        validation_errors = bool(re.search(r"[Vv]alidation [Ee]rror|VUID-", diagnostics))
        if validation_errors and status == 0:
            status = "validation-error"
        validation_observed = "CURRENT-VALIDATION-ENABLED" in diagnostics
        if correctness and backend == "vulkan" and status == 0 and not validation_observed:
            status = "validation-not-observed"
        manifest["runs"].append({"name": name, "backend": backend, "workload": workload, "variant": variant,
                                  "repeat": repeat, "correctness": correctness, "command": cmd,
                                  "returncode": status, "validation_errors": validation_errors,
                                  "validation_layer_in_log": "VK_LAYER_KHRONOS_validation" in diagnostics,
                                  "validation_observed": validation_observed,
                                  "elapsed_s": time.monotonic() - started})
        write(manifest_path, manifest)
        return status == 0

    for backend in args.backends:
        verified = set(args.workloads)
        for workload in args.workloads:
            if not args.skip_correctness:
                for variant in args.variants:
                    if not run(backend, workload, variant, None, True):
                        verified.discard(workload)
        for repeat in range(args.repeats):
            for workload in args.workloads:
                if workload not in verified:
                    continue
                for variant in (args.variants if repeat % 2 == 0 else list(reversed(args.variants))):
                    run(backend, workload, variant, repeat, False)
    manifest["system_after"] = system_info()
    manifest["source_after"] = snapshot()
    manifest["source_stable"] = (manifest["source_before"]["tree_sha256"] == manifest["source_after"]["tree_sha256"]
                                 and manifest["source_before"]["dependencies"] == manifest["source_after"]["dependencies"])
    manifest["finished_utc"] = datetime.datetime.now(datetime.timezone.utc).isoformat()
    write(manifest_path, manifest)
    if any(r["returncode"] != 0 for r in manifest["runs"]):
        raise SystemExit("Some runs failed; see manifest and logs. No failures are converted to passes.")


if __name__ == "__main__":
    main()
