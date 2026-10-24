# Exporting Renders

## Image

```sh
./build/MoleHole --export-image out.png --width 1920 --height 1080 --scene templates/test-scene.yaml
```

## Video

```sh
./build/MoleHole --export-video out.mp4 --width 1920 --height 1080 --duration 10 --fps 60
```

## Quality
Exports use the **Export** context's render profile (default `Lensed`). Pick another with `--profile`, adjust single settings with `--set`:

```sh
./build/MoleHole --export-image out.png --width 3840 --height 2160 --profile Reference
./build/MoleHole --export-image out.png --profile Traced --set transport.max_steps=8000 --set post.bloom=off
./build/MoleHole --list-settings
```

Profiles: `Flat`, `Lensed`, `Traced`, `Reference` (see User Guide → Render profiles and settings), plus user profiles stored in the scene.
Reference at 1920x1080 takes minutes; `Traced` takes seconds.

## Sidecar
Next to every export: `out.render.yaml` with the scene hash, the profile and **every** resolved setting, overrides, camera, git commit, GPU and driver, resolution, wall time. Use its `Settings` block to reproduce the image.
The disk noise still follows the wall clock, so two exports of the same scene are not byte-identical yet (see `docs/physics-roadmap/PROGRESS.md`, P0-1).

### All flags

| Flag | Applies to | Default | Meaning |
|---|---|---|---|
| `--export-image <path>` | — | — | Request a single-frame image export to `<path>` (PNG) |
| `--export-video <path>` | — | — | Request a video export to `<path>` (MP4, via ffmpeg) |
| `--export-sequence <dir>` | — | — | Request a PNG sequence into `<dir>` (created if missing), files `<name>_00001.png`, `<name>_00002.png`, ...; uses `--duration` and `--fps` like a video |
| `--sequence-name <name>` | sequence | `frame` | File name prefix of the sequence |
| `--width <px>` | both | 1920 | Output width |
| `--height <px>` | both | 1080 | Output height |
| `--duration <seconds>` | video | 10.0 | Length of the output video |
| `--fps <n>` | video | 60 | Output framerate; also the capture rate from the live sim |
| `--sim-start <s>` | both | scene clock (0) | Simulation clock `t_sim` [physical s] of the first frame; the disk phase follows it |
| `--time-scale <x>` | both | scene Time Scale | Physical seconds per second of video; frame k of a video is placed at `t_sim = start + k x / fps` |
| `--scene <path>` | both | `templates/test-scene.yaml` | Scene file to load before rendering |
| `--scene-camera` | both | off | Render through the scene's primary camera (the one with `Primary: true`) instead of the free viewport camera |
| `--profile <name>` | both | Export context's profile | Render profile (built-in or stored in the scene) |
| `--set <key=value>` | both | — | Override one render setting; repeatable; bad keys or values abort with exit code 2 |
| `--export-timeout <seconds>` | both | 120 | Give up when the export makes no progress (scene load, shader compile, a frame, encoding) for this long; `0` disables |
| `--list-settings` | — | — | Print every setting key with default and options, then exit |
| `--ray-step-size <f>` | both | profile | Same as `--set transport.step_size=<f>` |
| `--max-ray-steps <n>` | both | profile | Same as `--set transport.max_steps=<n>` |
| `--gpu-precision <f32\|f64>` | both | f32 | Same as `--set transport.gpu_precision=<p>` |
| `--physically-accurate` | both | off | Same as `--set transport.mode=Integrated` |
| `--metric <0-3>` | both | profile | Same as `--set spacetime.solution=<n>` (0 Schwarzschild, 1 Kerr, 2 Reissner–Nordström, 3 Kerr–Newman) |

### Exit codes

| Code | Meaning |
|---|---|
| 0 | Export finished |
| 2 | Bad command-line value (`--set`, `--export-timeout`) |
| 3 | A shader the render needs failed to compile (the `[error]` line names the shader and the compiler message) |
| 4 | No progress for `--export-timeout` seconds |
| 5 | Writing the image or encoding the video failed |

Step size is in units of the render frame's gravitational radius (`camera.length_unit`).
Not available yet: `--spp`, `--seed`, `--format`, `--denoise`, `--time-scale`, `--sim-start` (no accumulation or sampler yet).
