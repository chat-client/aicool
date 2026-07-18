#!/usr/bin/env python3
"""Download and verify the official cross-platform Real-ESRGAN NCNN models."""

import argparse
import hashlib
from pathlib import Path
import shutil
import time
import urllib.request
import zipfile


ARCHIVE_NAME = "realesrgan-ncnn-vulkan-20220424-ubuntu.zip"
ARCHIVE_URL = (
    "https://github.com/xinntao/Real-ESRGAN/releases/download/v0.2.5.0/"
    + ARCHIVE_NAME
)

# filename, exact byte size, SHA-256
MODELS = (
    ("realesrgan-x4plus.param", 116029,
     "35330ececcea33b6c397a72548e788d5d53becee4734c50b7fada36e89f10a86"),
    ("realesrgan-x4plus.bin", 33424520,
     "713ee713b0353afaa27976f0563a64a5043bd70b9bd8936c2e26e25ebcdbcddf"),
    ("realesrgan-x4plus-anime.param", 30290,
     "2b8fb6e0ae4d2d85704ca08c119a2f5ea40add4f2ecd512eb7f4cd44b6127ed4"),
    ("realesrgan-x4plus-anime.bin", 8943500,
     "fe01c269cfd10cdef8e018ab66ebe750cf79c7af4d1f9c16c737e1295229bacc"),
    ("realesr-animevideov3-x2.param", 3173,
     "b88ff4f00ebf019a7fdac17fdd45a7fd3665d37509efc5baf2e4da2e24420a04"),
    ("realesr-animevideov3-x2.bin", 1247368,
     "548a36f9c3f4ab8da56cd3b13badf23968bee207b396dad14d04b830e5f2ab2d"),
    ("realesr-animevideov3-x3.param", 3173,
     "d1a5755008791d09b57e3425fc9dd0bd26b00fdf79c606210bc0e693f8230881"),
    ("realesr-animevideov3-x3.bin", 1247368,
     "548a36f9c3f4ab8da56cd3b13badf23968bee207b396dad14d04b830e5f2ab2d"),
    ("realesr-animevideov3-x4.param", 3077,
     "850a248e7c14c27e5bd8cf7265113a9441036a7db63963bb8aa5169d788a435e"),
    ("realesr-animevideov3-x4.bin", 1247368,
     "548a36f9c3f4ab8da56cd3b13badf23968bee207b396dad14d04b830e5f2ab2d"),
)


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def valid(path, size, expected_hash):
    return (path.is_file() and path.stat().st_size == size
            and sha256(path) == expected_hash)


def download(url, destination, attempts=4):
    destination.parent.mkdir(parents=True, exist_ok=True)
    temporary = destination.with_suffix(destination.suffix + ".part")
    for attempt in range(attempts):
        try:
            request = urllib.request.Request(
                url, headers={"User-Agent": "WebCool-RealESRGAN/1.0"})
            with urllib.request.urlopen(request, timeout=120) as response, \
                    temporary.open("wb") as output:
                total = int(response.headers.get("Content-Length", "0"))
                completed = 0
                while True:
                    block = response.read(1024 * 1024)
                    if not block:
                        break
                    output.write(block)
                    completed += len(block)
                    if total:
                        print(f"archive: {completed * 100 // total}%", flush=True)
            temporary.replace(destination)
            return
        except Exception as error:
            temporary.unlink(missing_ok=True)
            if attempt + 1 == attempts:
                raise SystemExit(f"failed to download {url}: {error}") from error
            time.sleep(min(2 ** attempt, 8))


def extract_models(archive, output_root):
    try:
        with zipfile.ZipFile(archive) as bundle:
            members = {}
            for info in bundle.infolist():
                name = Path(info.filename).name
                if name in {item[0] for item in MODELS}:
                    members[name] = info
            for name, size, expected_hash in MODELS:
                destination = output_root / name
                if valid(destination, size, expected_hash):
                    print(f"verified {name}", flush=True)
                    continue
                info = members.get(name)
                if info is None:
                    raise SystemExit(f"model is missing from official archive: {name}")
                destination.parent.mkdir(parents=True, exist_ok=True)
                temporary = destination.with_suffix(destination.suffix + ".download")
                with bundle.open(info) as source, temporary.open("wb") as output:
                    shutil.copyfileobj(source, output, 1024 * 1024)
                if not valid(temporary, size, expected_hash):
                    temporary.unlink(missing_ok=True)
                    raise SystemExit(f"checksum verification failed: {name}")
                temporary.replace(destination)
                print(f"verified {name}", flush=True)
    except zipfile.BadZipFile as error:
        raise SystemExit(f"invalid Real-ESRGAN archive: {archive}") from error


def main():
    project_root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser()
    parser.add_argument(
        "--output-root", type=Path,
        default=project_root / "models" / "realesrgan" / "ncnn")
    parser.add_argument("--archive", type=Path,
                        help="use an existing official ZIP instead of downloading it")
    args = parser.parse_args()
    output_root = args.output_root.resolve()

    if all(valid(output_root / name, size, digest)
           for name, size, digest in MODELS):
        for name, _, _ in MODELS:
            print(f"verified {name}", flush=True)
        print(f"Real-ESRGAN NCNN models ready: {output_root}")
        return

    archive = (args.archive.resolve() if args.archive else
               output_root.parent / ".downloads" / ARCHIVE_NAME)
    if not archive.is_file():
        if args.archive:
            raise SystemExit(f"archive not found: {archive}")
        print(f"downloading official Real-ESRGAN archive: {ARCHIVE_URL}")
        download(ARCHIVE_URL, archive)
    extract_models(archive, output_root)
    if not args.archive:
        archive.unlink(missing_ok=True)
        if archive.parent.exists() and not any(archive.parent.iterdir()):
            archive.parent.rmdir()
    print(f"Real-ESRGAN NCNN models ready: {output_root}")


if __name__ == "__main__":
    main()
