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

```sh
./build/MoleHole --export-image out.png --width 3840 --height 2160 \
    --ray-step-size 0.002 --max-ray-steps 200000
```

### All flags

| Flag | Applies to | Default | Meaning |
|---|---|---|---|
| `--export-image <path>` | — | — | Request a single-frame image export to `<path>` (PNG) |
| `--export-video <path>` | — | — | Request a video export to `<path>` (MP4, via ffmpeg) |
| `--width <px>` | both | 1920 | Output width |
| `--height <px>` | both | 1080 | Output height |
| `--duration <seconds>` | video | 10.0 | Length of the output video |
| `--fps <n>` | video | 60 | Output framerate; also the capture rate from the live sim |
| `--scene <path>` | both | `templates/test-scene.yaml` | Scene file to load before rendering |
| `--ray-step-size <f>` | both | scene default | Overrides the raymarcher's step size |
| `--max-ray-steps <n>` | both | scene default | Overrides the raymarcher's max step count |
