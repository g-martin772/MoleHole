#!/usr/bin/env bash
# Renders every picture of the README with the headless exporter, so the gallery can be refreshed after renderer
# changes with one command. Each shot is one line below: name, scene, exporter flags and an optional crop.
#
# Usage: tools/gen_readme_images.sh [shot ...]   (default: all shots)
# Needs a built ./build/MoleHole, a GPU and Pillow (python3 -m pip install pillow). The planet maps for model-showcase
# come from tools/fetch_textures.py. Output: docs/img/readme/<shot>.jpg; the PNG and its .render.yaml sidecar (every
# resolved setting) are kept in .gpp/readme/. Don't rebuild while it runs: the exporter hot-reloads rebuilt plugins.
set -euo pipefail
cd "$(dirname "${BASH_SOURCE[0]}")/.."

out=docs/img/readme
raw=.gpp/readme
mkdir -p "$out" "$raw"

# name | scene | flags [| crop as left,top,right,bottom fractions of the image]
# gargantua-film keeps bloom and flares off, so the mosaic switches them on (flare threshold lowered) to have something to show. m1-orbits is slow
# the first time (several minutes building the integrated pipelines on a cold driver cache).
shots=(
    "lensing-split|gargantua-film|--scene-camera --width 1600 --height 900 --set compare.profile=Flat"
    "checkerboard|gargantua-film|--scene-camera --width 1280 --height 720 --set debug.sky=Checkerboard --set disk.enabled=false"
    "redshift|gargantua-film|--scene-camera --width 1280 --height 720 --set debug.view=Redshift"
    "image-order|gargantua-film|--scene-camera --width 1280 --height 720 --set debug.view=ImageOrder"
    "pipeline-mosaic|gargantua-film|--scene-camera --width 1800 --height 1012 --set debug.stage=Mosaic --set post.bloom=true --set post.lens_flare=true --set post.flare_threshold=0.5"
    "orbits|m1-orbits|--scene-camera --width 1280 --height 720"
    "planets|model-showcase|--scene-camera --width 2560 --height 1440|0.433,0.33,0.875,0.66"
    "near-horizon|gargantua-orbits|--scene-camera --width 1280 --height 720"
)

# The three debug views side by side
strips=(
    "debug-views|checkerboard redshift image-order"
)

wanted=" $* "

for shot in "${shots[@]}"; do
    IFS='|' read -r name scene flags crop <<<"$shot"
    if [[ $# -gt 0 && "$wanted" != *" $name "* ]]; then continue; fi
    echo "== $name ($scene)"
    # shellcheck disable=SC2086
    ./build/MoleHole --export-image "$raw/$name.png" --scene "templates/$scene.yaml" --export-timeout 600 $flags \
        >"$raw/$name.log" 2>&1 || { echo "   failed, see $raw/$name.log"; continue; }
    python3 - "$raw/$name.png" "$out/$name.jpg" "${crop:-}" <<'EOF'
import sys
from PIL import Image
image = Image.open(sys.argv[1]).convert("RGB")
if sys.argv[3]:
    left, top, right, bottom = (float(v) for v in sys.argv[3].split(","))
    w, h = image.size
    image = image.crop((round(left * w), round(top * h), round(right * w), round(bottom * h)))
image.save(sys.argv[2], quality=88, optimize=True, progressive=True)
EOF
done

for strip in "${strips[@]}"; do
    IFS='|' read -r name parts <<<"$strip"
    if [[ $# -gt 0 && "$wanted" != *" $name "* ]]; then continue; fi
    # shellcheck disable=SC2086
    python3 - "$out/$name.jpg" $(for part in $parts; do echo "$raw/$part.png"; done) <<'EOF'
import os, sys
from PIL import Image
paths = [p for p in sys.argv[2:] if os.path.exists(p)]
if not paths:
    sys.exit(0)
images = [Image.open(p).convert("RGB") for p in paths]
width = 640
scaled = [im.resize((width, round(im.height * width / im.width)), Image.LANCZOS) for im in images]
gap = 6
height = max(im.height for im in scaled)
sheet = Image.new("RGB", (len(scaled) * width + (len(scaled) - 1) * gap, height), (13, 17, 23))
for i, im in enumerate(scaled):
    sheet.paste(im, (i * (width + gap), 0))
sheet.save(sys.argv[1], quality=88, optimize=True, progressive=True)
EOF
    echo "== $name (strip)"
done
