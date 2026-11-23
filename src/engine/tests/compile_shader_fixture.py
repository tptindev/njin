"""Compile the same custom shader fixture for an APK using its pinned host shaderc."""
import argparse
import json
from pathlib import Path
import subprocess


def main():
    parser = argparse.ArgumentParser(__doc__)
    parser.add_argument("--shaderc", required=True, type=Path)
    parser.add_argument("--bgfx-source", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    args = parser.parse_args()
    assets = Path(__file__).parent / "assets"
    varying = Path(__file__).parents[1] / "runtime/shaders/bgfx/varying.def.sc"
    args.output.mkdir(parents=True, exist_ok=True)
    manifest = json.loads((assets / "custom-shader.json").read_text(encoding="utf8"))
    manifest.update(version=1, profiles={})
    for name, profile in [("spirv", "spirv"), ("essl", "300_es")]:
        destination = args.output / f"custom.{name}.bin"
        temporary = destination.with_suffix(".tmp")
        subprocess.run([str(args.shaderc), "-f", str(assets / "fs_custom.sc"),
                        "-o", str(temporary), "--type", "fragment", "--platform", "android",
                        "-p", profile, "-i", str(args.bgfx_source / "src"),
                        "--varyingdef", str(varying)], check=True)
        temporary.replace(destination)
        manifest["profiles"][name] = {"fragment": destination.name}
    (args.output / "custom.njshader").write_text(json.dumps(manifest, indent=2)+"\n", encoding="utf8")


if __name__ == "__main__":
    main()
