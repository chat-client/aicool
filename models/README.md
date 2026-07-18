# WebCool AI models

This directory is the single project-level location for large AI model data.
Platform-specific executables and dynamic libraries remain under `tools/`.

- `codeformer/weights/`: CodeFormer face restoration and facelib weights.
- `codeformer/coreml/`: native FP32 CodeFormer models for Apple Silicon.
- `realesrgan/ncnn/`: shared NCNN Real-ESRGAN `.bin` and `.param` models.
- `realesrgan/coreml/`: compiled Core ML Real-ESRGAN models for macOS.
- `restormer/coreml/`: compiled Core ML Restormer deblurring models for macOS.

The NCNN models are shared by macOS, Ubuntu, and Windows packages. Packaging
copies models from here into the layout expected by each installed runtime.

Download or verify the shared Real-ESRGAN NCNN models with:

```bash
python3 tools/download_realesrgan_models.py
```

Restormer currently has only an Apple Core ML runtime. Its `.mlmodelc` files
are macOS-only and are intentionally not downloaded or packaged on Ubuntu and
Windows.
