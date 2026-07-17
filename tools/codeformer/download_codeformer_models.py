#!/usr/bin/env python3
"""Download and verify the official CodeFormer inference weights."""

import argparse
from concurrent.futures import ThreadPoolExecutor, as_completed
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import time
import urllib.request


RELEASE = "https://github.com/sczhou/CodeFormer/releases/download/v0.1.0"
MODELS = (
    ("CodeFormer/codeformer.pth", 376637898,
     "1009e537e0c2a07d4cabce6355f53cb66767cd4b4297ec7a4a64ca4b8a5684b7"),
    ("facelib/detection_Resnet50_Final.pth", 109497761,
     "6d1de9c2944f2ccddca5f5e010ea5ae64a39845a86311af6fdf30841b0a5a16d"),
    ("facelib/parsing_parsenet.pth", 85331193,
     "3d558d8d0e42c20224f13cf5a29c79eba2d59913419f945545d8cf7b72920de2"),
    ("CodeFormer/codeformer_inpainting.pth", 370786611,
     "md5:253cbf53da0873ddd7f3b665ad6feb23"),
)


def ensure_codeformer_weights_link(weights_root):
    """Point the development CodeFormer checkout at the shared model data."""
    codeformer_root = Path(__file__).resolve().parent / "CodeFormer"
    link = codeformer_root / "weights"
    weights_root = weights_root.resolve()
    if not (codeformer_root / "inference_codeformer.py").is_file():
        raise SystemExit(f"CodeFormer source not found: {codeformer_root}")

    # Path.resolve() follows Unix symlinks and Windows directory junctions.
    if link.exists() or link.is_symlink():
        if link.resolve() == weights_root:
            print(f"weights link ready: {link} -> {weights_root}", flush=True)
            return
        raise SystemExit(
            f"refusing to replace existing CodeFormer weights path: {link}"
        )

    link.parent.mkdir(parents=True, exist_ok=True)
    if sys.platform == "win32":
        result = subprocess.run(
            ["cmd.exe", "/d", "/c", "mklink", "/J", str(link), str(weights_root)],
            capture_output=True,
            text=True,
        )
        if result.returncode != 0:
            message = result.stderr.strip() or result.stdout.strip()
            raise SystemExit(f"failed to create weights junction: {message}")
    else:
        relative_target = os.path.relpath(weights_root, link.parent)
        link.symlink_to(relative_target, target_is_directory=True)
    print(f"weights link ready: {link} -> {weights_root}", flush=True)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def checksum_matches(path, expected):
    if expected.startswith("md5:"):
        digest = hashlib.md5()
        with path.open("rb") as stream:
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        return digest.hexdigest() == expected[4:]
    return sha256(path) == expected


def fetch_part(url, path, start, end, attempts=8):
    expected = end - start + 1
    if path.is_file() and path.stat().st_size == expected:
        return expected
    for attempt in range(attempts):
        try:
            request = urllib.request.Request(
                url,
                headers={"Range": f"bytes={start}-{end}", "User-Agent": "WebCool-CodeFormer/1.0"},
            )
            with urllib.request.urlopen(request, timeout=120) as response:
                data = response.read()
            if len(data) != expected:
                raise RuntimeError(f"range returned {len(data)} bytes, expected {expected}")
            temporary = path.with_suffix(path.suffix + ".tmp")
            temporary.write_bytes(data)
            temporary.replace(path)
            return expected
        except Exception:
            if attempt + 1 == attempts:
                raise
            time.sleep(min(2 ** attempt, 15))


def download_model(root, relative, size, expected_hash, workers, chunk_size):
    destination = root / relative
    if destination.is_file() and destination.stat().st_size == size:
        if checksum_matches(destination, expected_hash):
            print(f"verified {relative}", flush=True)
            return
    parts_root = root / ".parts" / Path(relative).name
    parts_root.mkdir(parents=True, exist_ok=True)
    url = f"{RELEASE}/{Path(relative).name}"
    jobs = []
    index = 0
    for start in range(0, size, chunk_size):
        end = min(size - 1, start + chunk_size - 1)
        jobs.append((url, parts_root / f"{index:05d}.part", start, end))
        index += 1
    completed = 0
    with ThreadPoolExecutor(max_workers=workers) as executor:
        futures = [executor.submit(fetch_part, *job) for job in jobs]
        for future in as_completed(futures):
            completed += future.result()
            print(f"{relative}: {completed * 100 // size}%", flush=True)
    destination.parent.mkdir(parents=True, exist_ok=True)
    assembled = destination.with_suffix(destination.suffix + ".download")
    with assembled.open("wb") as output:
        for _, part, _, _ in jobs:
            with part.open("rb") as source:
                shutil.copyfileobj(source, output, 1024 * 1024)
    if assembled.stat().st_size != size or not checksum_matches(assembled, expected_hash):
        assembled.unlink(missing_ok=True)
        raise SystemExit(f"checksum verification failed: {relative}")
    assembled.replace(destination)
    shutil.rmtree(parts_root)
    print(f"verified {relative}", flush=True)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output-root", type=Path,
                        default=Path(__file__).resolve().parents[2] / "models" / "codeformer" / "weights")
    parser.add_argument("--workers", type=int, default=12)
    parser.add_argument("--chunk-mib", type=int, default=4)
    args = parser.parse_args()
    if not 1 <= args.workers <= 32 or not 1 <= args.chunk_mib <= 32:
        raise SystemExit("workers must be 1..32 and chunk-mib must be 1..32")
    output_root = args.output_root.resolve()
    for relative, size, expected_hash in MODELS:
        download_model(output_root, relative, size, expected_hash,
                       args.workers, args.chunk_mib * 1024 * 1024)
    parts = output_root / ".parts"
    if parts.exists() and not any(parts.iterdir()):
        parts.rmdir()
    ensure_codeformer_weights_link(output_root)


if __name__ == "__main__":
    main()
