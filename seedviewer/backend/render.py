import subprocess
import os
from pathlib import Path
from PIL import Image

RENDERER_PATH = os.environ.get("RENDERER_PATH", "/app/renderer/render_map")
THUMB_DIR = Path(os.environ.get("THUMB_DIR", "/data/thumbnails"))

THUMB_DIR.mkdir(parents=True, exist_ok=True)

def _ppm_to_png(ppm_path: str, png_path: str):
    with open(ppm_path, "rb") as f:
        img = Image.open(f)
        img = img.convert("RGB")
        img.save(png_path, "PNG")

def render_thumb(seed: int, x: int, z: int, mode: str = "sb") -> bool:
    large = "lb" in mode
    thumb_name = f"{seed}_{x}_{z}_thumb.png"
    thumb_path = THUMB_DIR / thumb_name
    if thumb_path.exists():
        return True

    ppm_path = f"/tmp/thumb_{seed}_{x}_{z}.ppm"

    try:
        result = subprocess.run([
            RENDERER_PATH,
            "--seed", str(seed),
            "--x", str(x),
            "--z", str(z),
            "--width", "256",
            "--height", "256",
            "--scale", "4",
            "--zoom", "8",
            "--sample",
            "--output", ppm_path,
        ] + (["--large-biomes"] if large else []), capture_output=True, text=True, timeout=60)
        if result.returncode != 0:
            print(f"Renderer failed for {seed}: {result.stderr}")
            return False
        _ppm_to_png(ppm_path, str(thumb_path))
        os.unlink(ppm_path)
        return True
    except Exception as e:
        print(f"Thumb render error for {seed}: {e}")
        return False
