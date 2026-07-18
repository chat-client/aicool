#!/usr/bin/env python3
"""Run an externally installed official CodeFormer tree for one whole image."""

import argparse
import os
from pathlib import Path
import shutil
import signal
import subprocess
import sys


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--repo", required=True, type=Path)
    parser.add_argument("--input", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--fidelity", required=True, type=float)
    parser.add_argument("--detector", default="retinaface_resnet50")
    parser.add_argument("--only-center-face", action="store_true")
    parser.add_argument("--aligned", action="store_true",
                        help="Treat the input as one cropped/aligned face and restore it directly")
    parser.add_argument("--inpaint", action="store_true",
                        help="Use the official masked-face inpainting model")
    return parser.parse_args()


def main():
    args = parse_args()
    repo = args.repo.resolve()
    source = args.input.resolve()
    destination = args.output.resolve()
    inference = repo / ("inference_inpainting.py" if args.inpaint else "inference_codeformer.py")
    if not inference.is_file():
        raise SystemExit(f"CodeFormer inference script not found: {inference}")
    if not source.is_file():
        raise SystemExit(f"input image not found: {source}")
    if not 0.0 <= args.fidelity <= 1.0:
        raise SystemExit("fidelity must be between 0 and 1")

    required_weights = [
        repo / "weights" / "CodeFormer" / ("codeformer_inpainting.pth" if args.inpaint else "codeformer.pth"),
    ]
    if not args.inpaint:
        required_weights.extend([
            repo / "weights" / "facelib" / "detection_Resnet50_Final.pth",
            repo / "weights" / "facelib" / "parsing_parsenet.pth",
        ])
    missing = [str(path) for path in required_weights if not path.is_file()]
    if missing:
        raise SystemExit("CodeFormer weights are not installed: " + ", ".join(missing))

    result_root = destination.parent / ".codeformer-results"
    if result_root.exists():
        shutil.rmtree(result_root)
    result_root.mkdir(parents=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    inference_source = source
    if args.inpaint:
        import cv2
        image = cv2.imread(str(source), cv2.IMREAD_COLOR)
        if image is None:
            raise SystemExit("inpainting input image cannot be decoded")
        inference_source = result_root / "inpaint-input.png"
        image = cv2.resize(image, (512, 512), interpolation=cv2.INTER_AREA)
        if not cv2.imwrite(str(inference_source), image):
            raise SystemExit("cannot prepare 512x512 inpainting input")
        command = [sys.executable, str(inference), "--input_path", str(inference_source),
                   "--output_path", str(result_root)]
    else:
        command = [
            sys.executable,
            str(inference),
            "--input_path", str(source),
            "--output_path", str(result_root),
            "--fidelity_weight", f"{args.fidelity:.2f}",
            "--upscale", "1",
            "--detection_model", args.detector,
        ]
        if args.only_center_face:
            command.append("--only_center_face")
        if args.aligned:
            command.append("--has_aligned")
    env = os.environ.copy()
    env["PYTHONPATH"] = str(repo) + os.pathsep + env.get("PYTHONPATH", "")
    print("progress=5", flush=True)
    process = subprocess.Popen(
        command,
        cwd=repo,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        bufsize=1,
        start_new_session=True,
    )

    def terminate_child(signum, _frame):
        try:
            os.killpg(process.pid, signum)
        except ProcessLookupError:
            pass

    signal.signal(signal.SIGTERM, terminate_child)
    signal.signal(signal.SIGINT, terminate_child)
    assert process.stdout is not None
    for line in process.stdout:
        print(line, end="", flush=True)
    return_code = process.wait()
    if return_code != 0:
        raise SystemExit(return_code)

    expected = (result_root / "inpaint-input.png" if args.inpaint else
                (result_root / "restored_faces" / f"{source.stem}.png"
                 if args.aligned else result_root / "final_results" / f"{source.stem}.png"))
    if expected.is_file():
        if args.inpaint:
            shutil.copy2(expected, destination)
        elif args.aligned:
            # Aligned restoration intentionally preserves CodeFormer's native
            # 512x512 face output. This is the mode used for heavily occluded
            # or incomplete face reconstruction.
            shutil.copy2(expected, destination)
        else:
            # Whole-image restoration internally raises short faces to a 512 px
            # canvas. Restore the source dimensions before any optional external
            # super-resolution stage.
            import cv2
            source_image = cv2.imread(str(source), cv2.IMREAD_COLOR)
            restored_image = cv2.imread(str(expected), cv2.IMREAD_COLOR)
            if source_image is None or restored_image is None:
                raise SystemExit("CodeFormer output image cannot be decoded")
            source_height, source_width = source_image.shape[:2]
            if restored_image.shape[:2] != (source_height, source_width):
                restored_image = cv2.resize(
                    restored_image, (source_width, source_height), interpolation=cv2.INTER_AREA)
                if not cv2.imwrite(str(destination), restored_image):
                    raise SystemExit("cannot save size-normalized CodeFormer output")
            else:
                shutil.copy2(expected, destination)
    else:
        # No detected face is a successful no-op and must not break the rest of
        # the enhancement pipeline.
        shutil.copy2(source, destination)
        print("faces=0", flush=True)
    shutil.rmtree(result_root, ignore_errors=True)
    print("frame=1/1", flush=True)


if __name__ == "__main__":
    main()
