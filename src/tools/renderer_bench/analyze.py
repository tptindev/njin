"""Analyze raw trials, preserve per-run percentiles and spatial image errors."""
import argparse
import csv
import json
from pathlib import Path
import statistics
from collections import defaultdict


def percentile(values, p):
    values = sorted(values)
    if not values:
        return None
    index = (len(values) - 1) * p
    lo = int(index)
    return values[lo] + (values[min(lo + 1, len(values) - 1)] - values[lo]) * (index - lo)


def stats(values):
    return {"n": len(values), "median": percentile(values, .5), "p95": percentile(values, .95),
            "p99": percentile(values, .99), "max": max(values) if values else None}


def image_diff(a, b, output, tolerance):
    from PIL import Image, ImageChops
    a, b = Image.open(a).convert("RGBA"), Image.open(b).convert("RGBA")
    if a.size != b.size:
        return {"pass": False, "error": "size mismatch"}
    diff = ImageChops.difference(a, b)
    channels = diff.split()
    peak = ImageChops.lighter(ImageChops.lighter(channels[0], channels[1]), ImageChops.lighter(channels[2], channels[3]))
    mask = peak.point(lambda v: 255 if v > tolerance else 0)
    changed = mask.histogram()[255]
    heat = Image.merge("RGB", (peak.point(lambda v: min(255, v * 16)), mask, Image.new("L", a.size)))
    heat.save(output)
    tiles = []
    for y in range(0, a.height, 32):
        for x in range(0, a.width, 32):
            box = (x, y, min(x + 32, a.width), min(y + 32, a.height))
            n = mask.crop(box).histogram()[255]
            if n:
                tiles.append({"box": box, "changed_pixels": n})
    histogram = peak.histogram()
    return {"pass": changed == 0, "tolerance_max_channel_8bit": tolerance,
            "changed_pixels": changed, "nonzero_pixels": sum(histogram[1:]),
            "max_channel_error": peak.getextrema()[1], "bbox": mask.getbbox(), "tiles_32px": tiles,
            "heatmap": output.name}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--tolerance", type=int, default=4)
    args = parser.parse_args()
    root = args.directory
    manifest = json.loads((root / "manifest.json").read_text(encoding="utf-8"))
    report = {"schema": "njin.renderer-analysis/1", "source_stable": manifest.get("source_stable"),
              "runs": [], "quality": [], "groups": [], "failures": [r for r in manifest["runs"] if r["returncode"] != 0]}
    check = {(r["backend"], r["workload"], r["variant"]): r for r in manifest["runs"] if r["correctness"] and r["returncode"] == 0}
    for backend, workload, variant in check:
        if variant == "cpu":
            continue
        base = check.get((backend, workload, "cpu"))
        if not base:
            continue
        for frame in (0, 30, 60):
            a = root / f"{base['name']}-{frame}.png"
            b = root / f"{check[backend, workload, variant]['name']}-{frame}.png"
            if a.exists() and b.exists():
                result = image_diff(a, b, root / f"heat-{backend}-{workload}-{variant}-{frame}.png", args.tolerance)
            else:
                result = {"pass": False, "error": "missing capture"}
            report["quality"].append({"backend": backend, "workload": workload, "variant": variant, "frame": frame, **result})
    groups = defaultdict(list)
    columns = ["wall_ms", "cpu_render_ms", "cpu_update_ms", "present_ms", "scene_submit_ms", "gpu_ms", "backend_cpu_ms",
               "rss_bytes", "private_bytes", "tracked_bytes", "allocations", "draws", "dispatches", "triangles", "textures", "buffers",
               "gpu_memory_bytes", "texture_estimate_bytes", "rt_estimate_bytes", "cull_pool_bytes"]
    for run in manifest["runs"]:
        if run["correctness"] or run["returncode"] != 0:
            continue
        name = run["name"]
        meta = json.loads((root / f"{name}.json").read_text())
        with (root / f"{name}.csv").open() as f:
            all_rows = list(csv.DictReader(f))
        rows = [r for r in all_rows if r["phase"] == "measure"]
        result = {k: run[k] for k in ("name", "backend", "workload", "variant", "repeat")}
        result["metadata"] = meta
        result["metrics"] = {key: stats([float(row[key]) for row in rows if float(row[key]) >= 0]) for key in columns}
        result["gpu_coverage"] = result["metrics"]["gpu_ms"]["n"] / len(rows) if rows else 0
        result["cold_warmup_wall_ms"] = stats([float(r["wall_ms"]) for r in all_rows if r["phase"] == "warmup"])
        result["warmup_gpu_ms"] = stats([float(r["gpu_ms"]) for r in all_rows if r["phase"] == "warmup" and float(r["gpu_ms"]) >= 0])
        result["first_frame_wall_ms"] = float(all_rows[0]["wall_ms"])
        result["spikes_over_2x_median"] = [int(r["tick"]) for r in rows if float(r["wall_ms"]) > 2 * result["metrics"]["wall_ms"]["median"]]
        result["cull_allocations_during_measurement"] = int(rows[-1]["cull_allocations"]) - int(rows[0]["cull_allocations"])
        report["runs"].append(result)
        groups[run["backend"], run["workload"], run["variant"]].append(result)
    for (backend, workload, variant), runs in groups.items():
        quality = [q for q in report["quality"] if q["backend"] == backend and q["workload"] == workload and q["variant"] == variant]
        metrics = {}
        for key in columns:
            values = [r["metrics"][key]["median"] for r in runs if r["metrics"][key]["n"]]
            metrics[key] = {"median_of_run_medians": statistics.median(values) if values else None,
                            "min_run_median": min(values) if values else None, "max_run_median": max(values) if values else None,
                            "per_run_p95": [r["metrics"][key]["p95"] for r in runs], "per_run_p99": [r["metrics"][key]["p99"] for r in runs]}
        report["groups"].append({"backend": backend, "workload": workload, "variant": variant, "repeats": len(runs),
                                  "quality_pass": all(q["pass"] for q in quality) if quality else None,
                                  "minimum_gpu_coverage": min(r["gpu_coverage"] for r in runs), "metrics": metrics})
    (root / "analysis.json").write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8", newline="\n")
    with (root / "summary.csv").open("w", newline="", encoding="utf-8") as f:
        writer = csv.writer(f, lineterminator="\n")
        writer.writerow(["backend", "workload", "variant", "repeats", "quality_pass", "gpu_coverage"] + columns)
        for group in report["groups"]:
            writer.writerow([group[k] for k in ("backend", "workload", "variant", "repeats", "quality_pass", "minimum_gpu_coverage")]
                            + [group["metrics"][key]["median_of_run_medians"] for key in columns])
    lines = ["# Kết quả benchmark renderer", "",
             "Median tổng hợp là median của median mỗi process; p95/p99 là median của percentiles mỗi process.",
             "CPU = thời gian render main loop, GPU = timestamps ghép đúng submission. Đơn vị timing ms, memory MiB.",
             "Không tính correctness/capture vào performance; không thay đổi power/clock/cache OS.", "",
             f"Source stable: {manifest.get('source_stable')}; executable SHA256: `{manifest['executable_sha256']}`.",
             f"Run failures: {len(report['failures'])}; image comparisons: {len(report['quality'])}; "
             f"image failures: {sum(not q['pass'] for q in report['quality'])}.", "",
             "| Backend | Workload | Variant | Runs | CPU median/p95/p99 | GPU median/p95/p99 | RSS | GPU driver usage | Alloc/frame | Dispatch | GPU coverage |",
             "|---|---|---|---:|---|---|---:|---:|---:|---:|---:|"]
    def number(value):
        return "N/A" if value is None else f"{value:.3f}"
    for g in report["groups"]:
        m = g["metrics"]
        def timing(key):
            metric = m[key]
            p95 = [v for v in metric["per_run_p95"] if v is not None]
            p99 = [v for v in metric["per_run_p99"] if v is not None]
            return "/".join(number(v) for v in (metric["median_of_run_medians"], statistics.median(p95) if p95 else None,
                                                  statistics.median(p99) if p99 else None))
        def median(key):
            return m[key]["median_of_run_medians"]
        gpu_memory = median("gpu_memory_bytes")
        lines.append(f"| {g['backend']} | {g['workload']} | {g['variant']} | {g['repeats']} | {timing('cpu_render_ms')} | {timing('gpu_ms')} | "
                     f"{median('rss_bytes')/1048576:.1f} | {number(gpu_memory/1048576 if gpu_memory is not None else None)} | "
                     f"{median('allocations'):.0f} | {median('dispatches'):.0f} | {g['minimum_gpu_coverage']:.1%} |")
    lines += ["", "## Chênh lệch trong cùng workload", "",
              "Δ = (GPU Hi-Z / CPU frustum − 1) × 100%; âm là giảm thời gian. Chỉ dùng khi quality pass và đủ GPU coverage.", "",
              "| Backend | Workload | Δ CPU render | Δ GPU | CPU run median range (A / B) | GPU run median range (A / B) | Quality |",
              "|---|---|---:|---:|---|---|---|"]
    lookup = {(g['backend'], g['workload'], g['variant']): g for g in report['groups']}
    for (backend, workload, variant), b in lookup.items():
        if variant != "gpu" or (backend, workload, "cpu") not in lookup:
            continue
        a = lookup[backend, workload, "cpu"]
        def delta(key):
            av, bv = a['metrics'][key]['median_of_run_medians'], b['metrics'][key]['median_of_run_medians']
            return "N/A" if av is None or bv is None or av <= 0 else f"{100*(bv/av-1):+.1f}%"
        def ranges(key):
            return " / ".join(f"{number(g['metrics'][key]['min_run_median'])}–{number(g['metrics'][key]['max_run_median'])}" for g in (a, b))
        lines.append(f"| {backend} | {workload} | {delta('cpu_render_ms')} | {delta('gpu_ms')} | {ranges('cpu_render_ms')} | {ranges('gpu_ms')} | {b['quality_pass']} |")
    lines += ["", "## Giới hạn", "", "- Nhiệt độ/background load không được khóa; không suy ra quan hệ nhân quả từ vài phần trăm dao động.",
              "- GPU driver memory usage và bgfx texture/RT estimates có định nghĩa khác nhau, không cộng chúng.",
              "- Snapshot counters bgfx có độ trễ; indirect triangle count không phải phép đếm visible triangles.",
              "- Startup/first-use, spikes và percentiles từng process nằm trong analysis.json; raw frame samples trong CSV gốc.",
              "- Atmosphere, lightmap, streaming, Android performance, Linux GPU vật lý và nhiều local lights chưa đo.",
              "- Không xếp hạng toàn renderer, không kết luận production-ready từ benchmark ngắn.", ""]
    (root / "report.md").write_text("\n".join(lines), encoding="utf-8", newline="\n")
    for group in report["groups"]:
        m = group["metrics"]
        print(group["backend"], group["workload"], group["variant"], "n=", group["repeats"],
              "CPU", m["cpu_render_ms"]["median_of_run_medians"], "GPU", m["gpu_ms"]["median_of_run_medians"],
              "coverage", group["minimum_gpu_coverage"], "quality", group["quality_pass"])
    print("quality failures:", sum(not q["pass"] for q in report["quality"]), "run failures:", len(report["failures"]))
    if report["failures"] or any(not q["pass"] for q in report["quality"]):
        raise SystemExit(1)


if __name__ == "__main__":
    main()
