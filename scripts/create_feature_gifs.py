"""Create the curated mod-page GIFs from the publisher's original recordings."""
import argparse
import hashlib
import json
from pathlib import Path
import subprocess
from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
SHOTS = {
    "transform_foundations": {"segments": [[0, 5.03]], "speed": 1.0},
    "save_copy": {"segments": [[1, 9], [11, 14], [16, 21.5], [23, 30.4]], "speed": 1.5},
    "transformers": {"segments": [[0.85, 11.47]], "speed": 1.0},
    "rails_with_stick": {"segments": [[1.8, 9], [19, 25.93]], "speed": 1.25},
    "connected_spread": {"segments": [[1.8, 6.1], [8.7, 10.7]], "speed": 1.0},
    "rail_planning": {"segments": [[1, 18], [22, 37.13]], "speed": 2.0},
}


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--ffmpeg", required=True, type=Path)
    parser.add_argument("--videos", type=Path, default=ROOT / "Saved/Publication/0.10.9/videos")
    parser.add_argument("--width", type=int, default=640)
    parser.add_argument("--fps", type=int, default=8)
    args = parser.parse_args()
    out = ROOT / "docs/publishing/media"
    out.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name, edit in SHOTS.items():
        source = args.videos / (name + ".mp4")
        if not source.is_file():
            raise FileNotFoundError(source)
        segments = edit["segments"]
        chain = []
        if len(segments) > 1:
            chain.append(f"[0:v]split={len(segments)}" +
                         "".join(f"[input{i}]" for i in range(len(segments))))
        for i, (start, end) in enumerate(segments):
            pad = f"input{i}" if len(segments) > 1 else "0:v"
            chain.append(f"[{pad}]trim=start={start}:end={end},"
                         f"setpts=(PTS-STARTPTS)/{edit['speed']}[part{i}]")
        chain.append("".join(f"[part{i}]" for i in range(len(segments))) +
                     f"concat=n={len(segments)}:v=1:a=0,"
                     f"fps={args.fps},scale={args.width}:-2:flags=lanczos,"
                     "split[palette_source][frames]")
        chain.append("[palette_source]palettegen=max_colors=96:stats_mode=diff[palette]")
        chain.append("[frames][palette]paletteuse=dither=none:"
                     "diff_mode=rectangle[gif]")
        target = out / (name + ".gif")
        subprocess.run([
            str(args.ffmpeg), "-hide_banner", "-loglevel", "error", "-y",
            "-i", str(source), "-filter_complex", ";".join(chain),
            "-map", "[gif]", "-an", "-loop", "0", str(target),
        ], check=True)
        with Image.open(target) as image:
            duration = 0
            for frame in range(image.n_frames):
                image.seek(frame)
                image.load()
                duration += image.info.get("duration", 0)
            result = {
                "file": target.name, "source": source.name,
                "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
                "segments_seconds": segments, "speed": edit["speed"],
                "width": image.width, "height": image.height,
                "frames": image.n_frames, "duration_seconds": duration / 1000,
                "loop": image.info.get("loop"), "bytes": target.stat().st_size,
                "sha256": hashlib.sha256(target.read_bytes()).hexdigest(),
            }
            assert image.n_frames > 1 and image.info.get("loop") == 0
        manifest.append(result)
        print(json.dumps(result), flush=True)
    (out / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
