#!/usr/bin/env python3
"""Generate the surface maps of Interstellar's planets into assets/textures/ (kept out of git).

  tools/gen_planet_textures.py                 # all planets, 4096x2048
  tools/gen_planet_textures.py --planets mann --width 2048

The film's assets are not available, so these are original, procedural maps built from what the film and the
sources describe (docs/notes/interstellar_planets.md): Miller's planet is a shallow ocean world with sparse
clouds, Mann's planet an ice world under dense frozen cloud, Edmunds' planet an arid rocky world. Every map is
equirectangular in MoleHole's convention (u = 0 at longitude -180 deg growing east, v = 0 at the north pole) and
is sampled from 3D noise on the unit sphere, so there is no seam at the date line and no pinching at the poles.
The output is deterministic for a given seed and size.
"""
import argparse
import os
import sys

import numpy as np
from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
TARGET = os.path.join(ROOT, "assets", "textures")


def sphere_points(width, height):
    """Unit vectors for every texel centre, in the body frame (+Y north, +X longitude 0, -Z longitude +90)."""
    u = (np.arange(width) + 0.5) / width
    v = (np.arange(height) + 0.5) / height
    longitude = (u - 0.5) * 2.0 * np.pi
    latitude = (0.5 - v) * np.pi
    lon, lat = np.meshgrid(longitude, latitude)
    return np.stack([np.cos(lat) * np.cos(lon), np.sin(lat), -np.cos(lat) * np.sin(lon)], axis=-1).astype(np.float32)


class Noise:
    """3D value noise with smoothstep interpolation over a hashed integer lattice."""

    def __init__(self, seed):
        self.table = np.random.default_rng(seed).random(1 << 20, dtype=np.float32) * 2.0 - 1.0

    def __call__(self, p):
        cell = np.floor(p).astype(np.int64)
        f = p - cell
        f = f * f * (3.0 - 2.0 * f)
        result = 0.0
        for dx in (0, 1):
            for dy in (0, 1):
                for dz in (0, 1):
                    c = cell + np.array([dx, dy, dz])
                    h = (c[..., 0] * 73856093) ^ (c[..., 1] * 19349663) ^ (c[..., 2] * 83492791)
                    value = self.table[h & ((1 << 20) - 1)]
                    wx = f[..., 0] if dx else 1.0 - f[..., 0]
                    wy = f[..., 1] if dy else 1.0 - f[..., 1]
                    wz = f[..., 2] if dz else 1.0 - f[..., 2]
                    result = result + value * wx * wy * wz
        return result

    def fbm(self, p, octaves, frequency, ridged=False, warp=None):
        if warp is not None:
            p = p + warp
        total = np.zeros(p.shape[:-1], np.float32)
        weight, norm = 0.5, 0.0
        for k in range(octaves):
            n = self(p * frequency + 17.31 * k)
            total += weight * ((1.0 - np.abs(n)) ** 2 if ridged else n)
            norm += weight
            weight *= 0.5
            frequency *= 2.03
        return total / norm


def save_rgb(path, rgb):
    Image.fromarray((np.clip(rgb, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "RGB").save(path)


def save_grey(path, grey):
    Image.fromarray((np.clip(grey, 0.0, 1.0) * 255.0 + 0.5).astype(np.uint8), "L").save(path)


def save_height(path, height):
    Image.fromarray((np.clip(height, 0.0, 1.0) * 65535.0 + 0.5).astype(np.uint16)).save(path)


def mix(a, b, t):
    t = np.asarray(t)[..., None] if np.ndim(t) else t
    return a * (1.0 - t) + b * t


def smoothstep(e0, e1, x):
    t = np.clip((x - e0) / (e1 - e0), 0.0, 1.0)
    return t * t * (3.0 - 2.0 * t)


def normalise(x, low=2.0, high=98.0):
    """Stretch x so its low/high percentiles land on 0 and 1."""
    a, b = np.percentile(x, [low, high])
    return np.clip((x - a) / max(b - a, 1e-6), 0.0, 1.0)


def cover(x, fraction, softness=0.08):
    """Coverage map: about `fraction` of the texels (by count) end up cloudy, with a soft edge."""
    threshold = np.percentile(x, 100.0 * (1.0 - fraction))
    spread = softness * (np.percentile(x, 98.0) - np.percentile(x, 2.0))
    return smoothstep(threshold - spread, threshold + spread, x)


def miller(p, seed):
    """Knee-deep ocean over a pale sandy floor: the water colour varies with the depth of the floor."""
    noise = Noise(seed)
    floor = noise.fbm(p, 6, 3.0)
    depth = smoothstep(-0.4, 0.5, floor)  # 0 shallow, 1 deeper
    shallow = np.array([0.42, 0.50, 0.50], np.float32)
    deep = np.array([0.18, 0.30, 0.36], np.float32)
    albedo = mix(shallow, deep, depth)
    albedo *= (0.94 + 0.06 * noise.fbm(p, 4, 24.0))[..., None]
    gloss = np.ones(p.shape[:-1], np.float32)  # all water: the specular mask is white
    warp = 0.6 * np.stack([noise.fbm(p, 3, 2.0 + k) for k in range(3)], axis=-1)
    clouds = cover(noise.fbm(p, 7, 2.5, warp=warp), 0.2)  # sparse, about a fifth of the sky
    return {"albedo.png": ("rgb", albedo), "specular.png": ("grey", gloss), "clouds.png": ("grey", clouds)}


def mann(p, seed):
    """Glacial ice with ridges and crevasse lines, under thick frozen cloud."""
    noise = Noise(seed)
    ridges = noise.fbm(p, 7, 2.0, ridged=True)
    height = normalise(ridges)
    crevasses = cover(noise.fbm(p, 5, 12.0, ridged=True), 0.06, 0.03)
    ice = np.array([0.86, 0.90, 0.95], np.float32)
    blue = np.array([0.55, 0.68, 0.80], np.float32)
    rock = np.array([0.32, 0.32, 0.34], np.float32)
    albedo = mix(ice, blue, 0.35 * smoothstep(0.3, 0.0, height) + 0.5 * crevasses)
    albedo = mix(albedo, rock, 0.25 * smoothstep(0.85, 1.0, height))
    roughness = 0.35 + 0.4 * crevasses  # polished ice, rougher where it is broken
    warp = 0.8 * np.stack([noise.fbm(p, 3, 1.5 + k) for k in range(3)], axis=-1)
    clouds = cover(noise.fbm(p, 7, 2.0, warp=warp), 0.7)  # mostly overcast
    return {"albedo.png": ("rgb", albedo), "roughness.png": ("grey", roughness), "height.png": ("height", height),
            "clouds.png": ("grey", clouds)}


def edmunds(p, seed):
    """Arid rocky world: sand basins, darker eroded uplands, sparse thin cloud."""
    noise = Noise(seed)
    relief = noise.fbm(p, 8, 2.5)
    uplands = smoothstep(-0.05, 0.35, relief)
    ridges = noise.fbm(p, 6, 6.0, ridged=True)
    height = normalise(0.35 * uplands + 0.45 * uplands * ridges + 0.1 * relief)
    sand = np.array([0.72, 0.58, 0.42], np.float32)
    rock = np.array([0.42, 0.30, 0.24], np.float32)
    dark = np.array([0.24, 0.20, 0.18], np.float32)
    albedo = mix(sand, rock, uplands)
    albedo = mix(albedo, dark, 0.5 * cover(ridges, 0.25) * uplands)
    albedo *= (0.9 + 0.1 * noise.fbm(p, 4, 30.0))[..., None]
    roughness = 0.85 + 0.1 * uplands
    warp = 0.5 * np.stack([noise.fbm(p, 3, 2.0 + k) for k in range(3)], axis=-1)
    clouds = cover(noise.fbm(p, 6, 3.0, warp=warp), 0.08)
    return {"albedo.png": ("rgb", albedo), "roughness.png": ("grey", roughness), "height.png": ("height", height),
            "clouds.png": ("grey", clouds)}


PLANETS = {"miller": (miller, 1401), "mann": (mann, 2014), "edmunds": (edmunds, 1970)}


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--planets", nargs="+", default=list(PLANETS), choices=list(PLANETS))
    parser.add_argument("--width", type=int, default=4096, help="map width; the height is half of it")
    args = parser.parse_args()

    p = sphere_points(args.width, args.width // 2)
    for name in args.planets:
        generate, seed = PLANETS[name]
        directory = os.path.join(TARGET, name)
        os.makedirs(directory, exist_ok=True)
        for file, (kind, data) in generate(p, seed).items():
            path = os.path.join(directory, file)
            {"rgb": save_rgb, "grey": save_grey, "height": save_height}[kind](path, data)
            print(os.path.relpath(path, ROOT))
    return 0


if __name__ == "__main__":
    sys.exit(main())
