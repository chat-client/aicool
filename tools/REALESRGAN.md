# Real-ESRGAN runtime assets

AI video enhancement expects the official `realesrgan-ncnn-vulkan` runtime in
each platform directory:

```
tools/mac/realesrgan-ncnn-vulkan
tools/linux/realesrgan-ncnn-vulkan
tools/windows/realesrgan-ncnn-vulkan.exe
models/realesrgan/ncnn/
```

The model directories contain the official NCNN `.param` and `.bin` files.
Paths can be overridden with `AICOOL_REALESRGAN` and
`AICOOL_REALESRGAN_MODELS`.

Download and checksum-verify the shared NCNN model files on any platform with:

```bash
python3 tools/download_realesrgan_models.py
```

The downloader extracts only model data from the official Real-ESRGAN v0.2.5.0
Ubuntu archive. These NCNN model files are identical across macOS, Ubuntu, and
Windows; platform executables remain under `tools/<platform>/`.

## Apple Silicon / Core ML

On macOS, live-action enhancement (`realesrgan-x4plus`) automatically prefers
the native Core ML runner and compiled model:

```
tools/mac/coreml-realesrgan
models/realesrgan/coreml/
```

The runner is built from `webcool/coreml/coreml_realesrgan.swift`. It requests
all Core ML compute units and processes multiple frames concurrently according
to the selected performance profile. Every worker owns a separate `MLModel`,
serial dispatch queue, `CIContext`, and reusable `CVPixelBufferPool`, as required
for safe concurrent Core ML predictions. In normal video mode AVFoundation feeds
decoded `CVPixelBuffer` frames directly into Core ML and writes the enhanced
frames through VideoToolbox; no intermediate PNG sequence is created. FFmpeg is
used only to attach the original audio track to the silent enhanced video.
Anime enhancement continues to use the NCNN model.

The macOS UI exposes five live-action comparison choices:

- `coreml-x2plus`: official `RealESRGAN_x2plus`, FP16 ML Program, 256 tile,
  true 2x output.
- `coreml-general-x4v3`: official tiny `realesr-general-x4v3`, FP16 ML
  Program, 512 tile, 4x output.
- `coreml-general-x4v3-w8a8`: the tiny model with calibrated 8-bit weights
  and activations, intended for M4 Neural Engine throughput comparisons.
- `coreml-x4plus-int8`: high-quality x4 model with 8-bit linear weight
  quantization, 512 tile.
- `realesrgan-x4plus`: the original unquantized x4 quality baseline.

All Core ML models are inspected at runtime for their input feature, output
feature, tile dimensions, and scale. Tile stitching uses overlapping context
and retains only each prediction's center region to prevent visible grid seams.

Five throughput controls are implemented for Apple Silicon:

1. Target-aware input sizing reduces the frame before inference when the model
   output would otherwise be larger than the requested result. This avoids
   generating pixels that would immediately be discarded by downscaling.
2. `quantize_mlprogram_w8a8.py` creates a calibrated W8A8 ML Program from the
   lightweight model. Calibration images should represent the real video set.
3. Tile inputs can be submitted through Core ML's batch prediction API. Batch
   size is automatic by model or can be set to 1, 2, or 4.
4. Worker count is selected independently from NCNN threads (one for heavy
   RRDB models, two for lightweight models by default), and overlap can be set
   to low, balanced, or quality.
5. Optional temporal reuse can infer every second or third frame and repeat the
   previous enhanced frame at skipped timestamps. It is disabled by default
   because fast motion can appear less smooth.

Inference input sizing has three explicit quality levels:

- `target`: speed-first and may substantially downscale before model inference.
- `balanced`: never reduces either source dimension below 75%.
- `source`: preserves every source pixel before inference.

The Web UI quality profile selects the x2plus live-action model, source-sized
inference, every-frame processing, and quality overlap. A comparison action
runs two 10-second jobs sequentially (W8A8 speed-first and x2plus quality), so
the original video and both outputs can be reviewed together without competing
for the ANE at the same time.

The second optimization pass adds direct tile rendering into the final video
pixel buffer, avoiding a full-frame Core Image composition graph. Adaptive
temporal reuse compares a 16x9 luminance signature, reuses only near-static
frames, and forces inference after at most two reused frames. The runner emits
`timing_ms=` diagnostics for tile preparation, Core ML inference, composition,
and encoder submission.

## Old-video preprocessing

The AI path can optionally run a restoration stage before super-resolution.
The Web UI provides off, conservative, balanced, and strong presets plus
individual controls for deinterlacing, deblocking, `hqdn3d` denoising, and mild
pre-sharpening. This stage keeps the source dimensions, drops the temporary
audio track, and uses H.264 CRF 12 as a visually near-lossless intermediate.
The original audio is attached after Core ML processing. Temporary preprocessed
video is stored inside the task directory and is removed on completion,
cancellation, or parent-process loss.

When source weights are available, generate target-aware model variants with:

```sh
python3 webcool/coreml/convert_realesrgan_models.py \
  --kind general-x4v3 --weights /path/to/weights.pth \
  --tiles 256 384 512 \
  --output 'models/realesr-general-x4v3-{tile}.mlpackage'
```

Compile the packages with `xcrun coremlcompiler compile`. WebCool searches for
the smallest suitable `-256`, `-384`, or `-512` compiled sibling and falls back
to the existing unsuffixed model when a variant is not installed.

The Web UI also exposes Core ML compute-unit comparison modes. `auto` maps to
`.all`, `gpu` to `.cpuAndGPU`, `ane` to `.cpuAndNeuralEngine`, and `cpu` to
`.cpuOnly`. GPU and ANE modes still permit CPU fallback for unsupported model
operations. Output names include the selected non-auto mode for comparison.

Override the installed paths with `AICOOL_COREML_REALESRGAN` and
`AICOOL_COREML_REALESRGAN_MODEL`. The bundled fixed-shape 512x512 Core ML model
is a community conversion of Real-ESRGAN x4plus from
https://github.com/john-rocky/CoreML-Models; the original Real-ESRGAN project is
BSD-3-Clause licensed (see `REALESRGAN-LICENSE`).

On macOS the WebCool `Makefile` builds the native runner automatically when
`webcool/coreml/coreml_realesrgan.swift` is newer than the binary or the binary
is missing. Run `make coreml-runner` to build only this component. The output is
an arm64 executable at `tools/mac/coreml-realesrgan` with macOS 13 as its
minimum deployment target. `make clean` removes it and the next `make` rebuilds
it.
