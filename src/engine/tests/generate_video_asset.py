"""Regenerate the small MPEG-1 color/seek fixture; requires host ffmpeg."""
from pathlib import Path
import subprocess

command = ["ffmpeg", "-hide_banner", "-loglevel", "error", "-y"]
for color in ["red", "lime", "blue"]:
    command += ["-f", "lavfi", "-i", f"color=c={color}:s=128x128:r=25:d=0.4"]
command += ["-filter_complex", "[0:v][1:v][2:v]concat=n=3:v=1:a=0[out]",
            "-map", "[out]", "-r", "25", "-c:v", "mpeg1video", "-g", "1",
            "-bf", "0", "-q:v", "2", "-an", "-f", "mpeg",
            str(Path(__file__).parent / "assets" / "video-colors.mpg")]
subprocess.run(command, check=True)
