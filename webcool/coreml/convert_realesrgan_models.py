#!/usr/bin/env python3
"""Convert official Real-ESRGAN weights to fixed-shape Core ML models."""

import argparse
from pathlib import Path

import coremltools as ct
import torch
from torch import nn
from torch.nn import functional as F


class ResidualDenseBlock(nn.Module):
    def __init__(self, features=64, growth=32):
        super().__init__()
        self.conv1 = nn.Conv2d(features, growth, 3, 1, 1)
        self.conv2 = nn.Conv2d(features + growth, growth, 3, 1, 1)
        self.conv3 = nn.Conv2d(features + growth * 2, growth, 3, 1, 1)
        self.conv4 = nn.Conv2d(features + growth * 3, growth, 3, 1, 1)
        self.conv5 = nn.Conv2d(features + growth * 4, features, 3, 1, 1)
        self.act = nn.LeakyReLU(0.2, inplace=True)

    def forward(self, x):
        x1 = self.act(self.conv1(x))
        x2 = self.act(self.conv2(torch.cat((x, x1), 1)))
        x3 = self.act(self.conv3(torch.cat((x, x1, x2), 1)))
        x4 = self.act(self.conv4(torch.cat((x, x1, x2, x3), 1)))
        x5 = self.conv5(torch.cat((x, x1, x2, x3, x4), 1))
        return x5 * 0.2 + x


class RRDB(nn.Module):
    def __init__(self, features=64, growth=32):
        super().__init__()
        self.rdb1 = ResidualDenseBlock(features, growth)
        self.rdb2 = ResidualDenseBlock(features, growth)
        self.rdb3 = ResidualDenseBlock(features, growth)

    def forward(self, x):
        return self.rdb3(self.rdb2(self.rdb1(x))) * 0.2 + x


def pixel_unshuffle(x, scale):
    return F.pixel_unshuffle(x, scale)


class RRDBNetX2(nn.Module):
    def __init__(self):
        super().__init__()
        self.conv_first = nn.Conv2d(12, 64, 3, 1, 1)
        self.body = nn.Sequential(*[RRDB() for _ in range(23)])
        self.conv_body = nn.Conv2d(64, 64, 3, 1, 1)
        self.conv_up1 = nn.Conv2d(64, 64, 3, 1, 1)
        self.conv_up2 = nn.Conv2d(64, 64, 3, 1, 1)
        self.conv_hr = nn.Conv2d(64, 64, 3, 1, 1)
        self.conv_last = nn.Conv2d(64, 3, 3, 1, 1)
        self.lrelu = nn.LeakyReLU(0.2, inplace=True)

    def forward(self, x):
        feat = self.conv_first(pixel_unshuffle(x, 2))
        feat = feat + self.conv_body(self.body(feat))
        feat = self.lrelu(self.conv_up1(F.interpolate(feat, scale_factor=2, mode="nearest")))
        feat = self.lrelu(self.conv_up2(F.interpolate(feat, scale_factor=2, mode="nearest")))
        return self.conv_last(self.lrelu(self.conv_hr(feat)))


class SRVGGNetCompact(nn.Module):
    def __init__(self, num_conv=32, upscale=4):
        super().__init__()
        body = [nn.Conv2d(3, 64, 3, 1, 1), nn.PReLU(num_parameters=64)]
        for _ in range(num_conv):
            body.extend([nn.Conv2d(64, 64, 3, 1, 1), nn.PReLU(num_parameters=64)])
        body.append(nn.Conv2d(64, 3 * upscale * upscale, 3, 1, 1))
        self.body = nn.ModuleList(body)
        self.upsampler = nn.PixelShuffle(upscale)
        self.upscale = upscale

    def forward(self, x):
        out = x
        for layer in self.body:
            out = layer(out)
        return self.upsampler(out) + F.interpolate(x, scale_factor=self.upscale, mode="nearest")


class ImageModel(nn.Module):
    def __init__(self, model):
        super().__init__()
        self.model = model

    def forward(self, x):
        return torch.clamp(self.model(x), 0, 1) * 255.0


def load_weights(model, path):
    checkpoint = torch.load(path, map_location="cpu", weights_only=True)
    state = checkpoint.get("params_ema", checkpoint.get("params", checkpoint))
    model.load_state_dict(state, strict=True)
    model.eval()


def convert(kind, weights, output, tile):
    model = RRDBNetX2() if kind == "x2plus" else SRVGGNetCompact()
    load_weights(model, weights)
    wrapped = ImageModel(model).eval()
    trace_tile = min(tile, 128) if kind == "x2plus" else tile
    example = torch.rand(1, 3, trace_tile, trace_tile)
    traced = torch.jit.trace(wrapped, example)
    scale = 2 if kind == "x2plus" else 4
    coreml = ct.convert(
        traced,
        convert_to="mlprogram",
        minimum_deployment_target=ct.target.macOS13,
        compute_precision=ct.precision.FLOAT16,
        inputs=[ct.ImageType(name="input", shape=(1, 3, tile, tile),
                             scale=1 / 255.0, color_layout=ct.colorlayout.RGB)],
        outputs=[ct.ImageType(name="output", color_layout=ct.colorlayout.RGB)],
    )
    coreml.author = "WebCool / Real-ESRGAN"
    coreml.short_description = f"{kind} fixed {tile}x{tile}, {scale}x output"
    coreml.user_defined_metadata["webcool.scale"] = str(scale)
    coreml.user_defined_metadata["webcool.tile"] = str(tile)
    coreml.save(output)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--kind", choices=("x2plus", "general-x4v3"), required=True)
    parser.add_argument("--weights", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--tile", type=int, default=512)
    args = parser.parse_args()
    Path(args.output).parent.mkdir(parents=True, exist_ok=True)
    convert(args.kind, args.weights, args.output, args.tile)


if __name__ == "__main__":
    main()
