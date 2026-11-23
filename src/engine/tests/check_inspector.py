"""Exercise Njin's real local inspector protocol and GPU-backed GIF recorder."""
import argparse
import json
from pathlib import Path
import re
import socket
import struct
import subprocess
import time


def gif_frames(path):
    data = path.read_bytes()
    assert data[:6] in (b"GIF87a", b"GIF89a"), "invalid GIF header"
    width, height = struct.unpack_from("<HH", data, 6)
    offset = 13 + (3 * (2 << (data[10] & 7)) if data[10] & 128 else 0)
    frames = 0
    while offset < len(data):
        tag = data[offset]
        offset += 1
        if tag == 0x3B:
            return width, height, frames
        if tag == 0x21:
            offset += 1
        elif tag == 0x2C:
            packed = data[offset + 8]
            offset += 9 + (3 * (2 << (packed & 7)) if packed & 128 else 0)
            offset += 1  # LZW minimum code size.
            frames += 1
        else:
            raise AssertionError(f"unexpected GIF block {tag}")
        while data[offset]:
            offset += 1 + data[offset]
        offset += 1
    raise AssertionError("GIF trailer missing")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--exe", required=True, type=Path)
    parser.add_argument("--renderer", required=True)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--gpu-validation", action="store_true")
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    complete = output / "complete"
    complete.unlink(missing_ok=True)
    with socket.socket() as reservation:
        reservation.bind(("127.0.0.1", 0))
        port = reservation.getsockname()[1]
    command = [str(args.exe.resolve()), f"--renderer={args.renderer}", f"--port={port}", f"--output={output}"]
    if args.gpu_validation:
        command.append("--gpu-validation")
    seen, records = set(), []
    pause, stepped, resumed, selected = False, False, False, False
    step_requested = False
    recorded, resources, scene = None, False, False
    with (output / "native.log").open("w", encoding="utf8") as log:
        process = subprocess.Popen(command, stdout=log, stderr=subprocess.STDOUT)
        try:
            deadline = time.monotonic() + 60
            while True:
                try:
                    link = socket.create_connection(("127.0.0.1", port), timeout=.2)
                    break
                except OSError:
                    if process.poll() is not None or time.monotonic() > deadline:
                        raise AssertionError("native inspector server did not start")
                    time.sleep(.05)
            with link:
                link.settimeout(.5)
                def send(**message):
                    link.sendall((json.dumps(message) + "\n").encode())
                pending = b""
                while time.monotonic() < deadline:
                    try:
                        chunk = link.recv(65536)
                    except socket.timeout:
                        if process.poll() is not None:
                            break
                        continue
                    if not chunk:
                        break
                    pending += chunk
                    while b"\n" in pending:
                        line, pending = pending.split(b"\n", 1)
                        message = json.loads(line)
                        records.append(message)
                        kind = message.get("t")
                        seen.add(kind)
                        if kind == "hello":
                            assert message["v"] == 7
                            send(cmd="pause", value=True)
                            send(cmd="rec", value=True, fps=20, scale=.5, max_s=1)
                            send(cmd="doc_list", src="renderer")
                            send(cmd="doc_get", src="renderer", id="pipeline")
                        elif kind == "stats":
                            pause |= bool(message["paused"])
                            if stepped and not message["paused"]:
                                resumed = True
                        elif kind == "watch" and pause:
                            dt = message.get("values", {}).get("dt")
                            # Observe an actual paused update before requesting one step.
                            # A transient stats.paused=False can be missed between snapshots.
                            if dt == 0 and not step_requested:
                                step_requested = True
                                send(cmd="step")
                            elif step_requested and dt is not None and dt > 0 and not stepped:
                                stepped = True
                                send(cmd="pause", value=False)
                            elif stepped and dt is not None and dt > 0:
                                resumed = True
                        elif kind == "world":
                            scene |= bool(message.get("scene3d", {}).get("items"))
                            if message.get("ents") and not selected:
                                selected = True
                                send(cmd="select", id=message["ents"][0]["id"])
                        elif kind == "res":
                            resources |= (any(row["k"]=="target" and row["b"]>=32*32*8 for row in message["items"]) if args.renderer=="opengl" else message.get("estimated") is True and any(row["k"] == "target" and row["b"] == 32 * 32 * 12 for row in message["items"]))
                        elif kind == "rec" and not message["on"] and message["frames"] >= 3:
                            assert not message["err"], message["err"]
                            recorded = Path(message["file"])
                    required = {"hello", "stats", "world", "entity", "watch", "prof", "res", "doc_list", "doc"}
                    if recorded and resources and scene and pause and stepped and resumed and required <= seen:
                        complete.write_text("PASS\n", encoding="utf8")
                        break
                else:
                    raise AssertionError("inspector protocol timed out")
            (output / "protocol.json").write_text(json.dumps(records,indent=2),encoding="utf8")
            assert required <= seen, "missing inspector message types"
            assert any(m.get("t")=="watch" and m["values"].get("dt")==0 for m in records), "pause did not zero engine delta"
            assert any(m.get("t")=="prof" and any(s["calls"]>0 for s in m["systems"]) for m in records), "profiling did not run"
            assert any(m.get("t")=="entity" and any(c["name"]=="model3d" for c in m.get("components",m.get("comps",[]))) for m in records), "3D component views missing"
            assert recorded and resources and scene and pause and stepped and resumed, f"missing inspector/recording controls: recorded={recorded}, resources={resources}, scene={scene}, pause={pause}, stepped={stepped}, resumed={resumed}"
            assert process.wait(timeout=10) == 0, "native inspector regression failed"
            assert recorded.resolve().is_relative_to(output), "recording escaped the test directory"
            width, height, frames = gif_frames(recorded)
            assert (width, height) == (80, 60) and frames >= 3, "GPU recording has invalid dimensions/frame count"
        finally:
            if process.poll() is None:
                process.terminate()
                process.wait(timeout=10)
    text = (output / "native.log").read_text(encoding="utf8", errors="replace")
    assert not re.search(r"[Vv]alidation [Ee]rror|VUID-", text), "GPU validation diagnostics"
    expected = {"opengl":"OpenGL", "vulkan": "Vulkan", "bgfx-opengl": "OpenGL", "d3d12": "Direct3D 12"}[args.renderer]
    if args.renderer=="opengl":
        assert "Njin bgfx renderer:" not in text and "GL: OpenGL device information" in text, "wrong legacy backend"
    else:
        assert f"Njin bgfx renderer: {expected}" in text, "wrong backend"
    (output / "protocol.json").write_text(json.dumps(records, indent=2), encoding="utf8")
    print(f"PASS {args.renderer}: inspector protocol, controls, 3D resources and {frames}-frame GPU GIF")


if __name__ == "__main__":
    main()
