# Restormer runtime for WebCool

WebCool uses Restormer as an optional deblurring stage before Real-ESRGAN.
The macOS package reuses `coreml-realesrgan` because that runner supports
same-size Core ML image-to-image models as well as super-resolution models.

Expected compiled models:

- `models/restormer/coreml/restormer-motion-deblur.mlmodelc`
- `models/restormer/coreml/restormer-defocus-deblur.mlmodelc`

Generate a model from the official Restormer repository and checkpoint:

```bash
python3 webcool/coreml/convert_restormer_models.py \
  --restormer-root /path/to/Restormer \
  --weights /path/to/motion_deblurring.pth \
  --task motion-deblur \
  --tile 128 \
  --output /tmp/restormer-motion-deblur.mlpackage
xcrun coremlcompiler compile /tmp/restormer-motion-deblur.mlpackage \
  models/restormer/coreml
```

The official project and checkpoints are distributed under the MIT license:
https://github.com/swz30/Restormer
