#!/usr/bin/env python3
"""Generate CodeFormer's ignored BasicSR version module without building it."""

from pathlib import Path
import re


def main():
    script_dir = Path(__file__).resolve().parent
    codeformer_root = script_dir / "CodeFormer"
    if not (codeformer_root / "inference_codeformer.py").is_file():
        codeformer_root = script_dir / "codeformer" / "CodeFormer"

    basicsr_root = codeformer_root / "basicsr"
    version_source = basicsr_root / "VERSION"
    if not version_source.is_file():
        raise SystemExit(f"BasicSR VERSION file not found: {version_source}")
    version = version_source.read_text(encoding="utf-8").strip()
    if not re.fullmatch(r"[0-9]+(?:\.[0-9]+)*", version):
        raise SystemExit(f"invalid BasicSR version: {version!r}")

    version_info = tuple(int(part) for part in version.split("."))
    destination = basicsr_root / "version.py"
    destination.write_text(
        "# Generated from basicsr/VERSION. Kept in the WebCool source payload because\n"
        "# CodeFormer imports it before inference starts.\n"
        f"__version__ = {version!r}\n"
        f"__gitsha__ = {version!r}\n"
        f"version_info = {version_info!r}\n",
        encoding="utf-8",
    )
    print(f"BasicSR version module ready: {destination}")


if __name__ == "__main__":
    main()
