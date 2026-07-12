#!/usr/bin/env python3
"""Create an 8-bit weight-quantized Core ML comparison model."""

import argparse
import coremltools as ct
from coremltools.models.neural_network import quantization_utils


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    model = ct.models.MLModel(args.input)
    quantized = quantization_utils.quantize_weights(model, nbits=8,
                                                    quantization_mode="linear")
    quantized.user_defined_metadata["webcool.scale"] = "4"
    quantized.user_defined_metadata["webcool.tile"] = "512"
    quantized.short_description = "Real-ESRGAN x4plus, 8-bit linear weight quantization"
    quantized.save(args.output)


if __name__ == "__main__":
    main()
