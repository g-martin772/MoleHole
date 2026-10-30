#!/usr/bin/env python3
"""Download the planet textures listed in tools/texture_manifest.yaml into assets/textures/ (kept out of git).

  tools/fetch_textures.py                    # group "core": the maps the templates use
  tools/fetch_textures.py --groups core-2k   # small 2k set
  tools/fetch_textures.py --groups core nasa --force

Existing files are skipped (use --force to download again). Files with a sha256 in the manifest are checked.
An entry with max_width is post-processed (needs Pillow): the download is kept as <target>.src, checked against
source_sha256, converted to 8-bit RGB/grey, downsized to at most max_width pixels wide and saved as JPEG q90 (or
PNG for a .png target); sha256 then covers the final file, which can differ between Pillow/libjpeg versions
(only a warning, the source hash is the strict check).
TIFF downloads are converted to PNG next to the original (needs Pillow), because stb_image cannot read TIFF.
Writes assets/textures/ATTRIBUTION.md with the credit line of every file present.
Sources and licences: docs/notes/texture_sources.md
"""
import argparse
import hashlib
import os
import sys
import urllib.request

import yaml

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
MANIFEST = os.path.join(ROOT, "tools", "texture_manifest.yaml")
TARGET = os.path.join(ROOT, "assets", "textures")


def sha256(path):
    digest = hashlib.sha256()
    with open(path, "rb") as f:
        for chunk in iter(lambda: f.read(1 << 20), b""):
            digest.update(chunk)
    return digest.hexdigest()


def referer_for(url, referers):
    for prefix, referer in referers.items():
        if url.startswith(prefix):
            return referer
    return None


def download(url, path, referer):
    headers = {"User-Agent": "MoleHole texture fetch (tools/fetch_textures.py)"}
    if referer:
        headers["Referer"] = referer
    request = urllib.request.Request(url, headers=headers)
    partial = path + ".part"
    with urllib.request.urlopen(request, timeout=120) as response, open(partial, "wb") as out:
        content_type = response.headers.get("Content-Type", "")
        if content_type.startswith("text/html"):
            raise RuntimeError(f"server sent an HTML page instead of the file ({url})")
        while chunk := response.read(1 << 20):
            out.write(chunk)
    os.replace(partial, path)


def convert_tiff(path):
    from PIL import Image  # only needed for TIFF sources

    png = os.path.splitext(path)[0] + ".png"
    with Image.open(path) as image:
        image.save(png)
    return png


def process(source, path, max_width):
    from PIL import Image

    Image.MAX_IMAGE_PIXELS = None  # sources are hash-checked
    with Image.open(source) as image:
        image.load()
        if image.mode not in ("L", "RGB"):
            image = image.convert("RGB")
        if image.width > max_width:
            image = image.resize((max_width, image.height * max_width // image.width), Image.LANCZOS)
        partial = path + ".part"
        if path.lower().endswith(".png"):
            image.save(partial, "PNG")
        else:
            image.save(partial, "JPEG", quality=90, optimize=True)
    os.replace(partial, path)


def write_attribution(entries):
    lines = ["# Texture attribution", "",
             "Fetched by tools/fetch_textures.py. Sources and licences: docs/notes/texture_sources.md", ""]
    for entry in entries:
        if os.path.exists(os.path.join(TARGET, entry["target"])):
            lines.append(f"- `{entry['target']}`: {entry['attribution']} ({entry['licence']})")
    with open(os.path.join(TARGET, "ATTRIBUTION.md"), "w") as f:
        f.write("\n".join(lines) + "\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--groups", nargs="+", default=["core"], help="manifest groups to fetch (default: core)")
    parser.add_argument("--force", action="store_true", help="download files that already exist")
    args = parser.parse_args()

    with open(MANIFEST) as f:
        manifest = yaml.safe_load(f)
    referers = manifest.get("requires_referer", {})
    entries = manifest["textures"]
    selected = [e for e in entries if e.get("group") in args.groups]
    if not selected:
        print(f"no textures in groups {args.groups}", file=sys.stderr)
        return 1

    failures = 0
    for entry in selected:
        path = os.path.join(TARGET, entry["target"])
        os.makedirs(os.path.dirname(path), exist_ok=True)
        if os.path.exists(path) and not args.force:
            print(f"have  {entry['target']}")
        else:
            print(f"fetch {entry['target']} <- {entry['url']}")
            max_width = entry.get("max_width")
            source = path + ".src" if max_width else path
            try:
                download(entry["url"], source, referer_for(entry["url"], referers))
                expected_source = entry.get("source_sha256")
                if expected_source and sha256(source) != expected_source:
                    raise RuntimeError(f"source sha256 mismatch for {entry['target']}")
                if max_width:
                    print(f"  process -> max {max_width} px wide")
                    process(source, path, max_width)
                    os.remove(source)
            except Exception as error:  # keep going: one dead link should not stop the rest
                print(f"  FAILED: {error}", file=sys.stderr)
                failures += 1
                continue
        expected = entry.get("sha256")
        if expected and sha256(path) != expected:
            if entry.get("max_width"):
                print(f"  warning: re-encoded {entry['target']} differs from the manifest hash (Pillow version?)")
            else:
                print(f"  FAILED: sha256 mismatch for {entry['target']}", file=sys.stderr)
                failures += 1
                continue
        if path.lower().endswith((".tif", ".tiff")):
            png = os.path.splitext(path)[0] + ".png"
            if args.force or not os.path.exists(png):
                print(f"  convert -> {os.path.relpath(png, TARGET)}")
                convert_tiff(path)

    write_attribution(entries)
    print(f"done, {failures} failed" if failures else "done")
    return 1 if failures else 0


if __name__ == "__main__":
    sys.exit(main())
