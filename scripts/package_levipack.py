#!/usr/bin/env python3
"""Packages LeviBoost as a LeviLaunchroid .levipack.

A .levipack is a zip archive that contains the mod manifest, the native
library named by the manifest's "entry" field, and an optional icon.  The
launcher reads manifest.json directly, so the copy in the repository root is
the single source of truth for the mod's metadata.

Usage:
    python3 scripts/package_levipack.py --library build/arm64-v8a/libleviboost.so \
        --output dist/LeviBoost.levipack [--icon assets/icon.png]
"""

import argparse
import hashlib
import json
import struct
import sys
import zipfile
from pathlib import Path

REPO_ROOT = Path(__file__).resolve().parent.parent
DEFAULT_MANIFEST = REPO_ROOT / "manifest.json"
REQUIRED_KEYS = ("type", "name", "author", "version", "entry")


def load_manifest(path: Path) -> dict:
    manifest = json.loads(path.read_text(encoding="utf-8"))
    missing = [key for key in REQUIRED_KEYS if not manifest.get(key)]
    if missing:
        raise SystemExit(f"manifest {path} is missing: {', '.join(missing)}")
    if manifest["type"] != "preload-native":
        raise SystemExit(
            f"manifest type must be 'preload-native', found {manifest['type']!r}"
        )
    return manifest


def check_library(path: Path) -> None:
    """The entry point must be an AArch64 shared object."""
    if not path.is_file():
        raise SystemExit(f"library not found: {path}")
    with path.open("rb") as fp:
        header = fp.read(64)
    if header[:4] != b"\x7fELF":
        raise SystemExit(f"{path} is not an ELF file")
    if header[4] != 2:
        raise SystemExit(f"{path} is not 64-bit")
    machine = struct.unpack_from("<H", header, 18)[0]
    if machine != 183:  # EM_AARCH64
        raise SystemExit(f"{path} is not an AArch64 library (machine={machine})")
    if struct.unpack_from("<H", header, 16)[0] != 3:  # ET_DYN
        raise SystemExit(f"{path} is not a shared object")


def build_package(library: Path, manifest_path: Path, icon: Path | None,
                  output: Path) -> None:
    manifest = load_manifest(manifest_path)
    check_library(library)

    output.parent.mkdir(parents=True, exist_ok=True)
    if output.exists():
        output.unlink()

    with zipfile.ZipFile(output, "w", compression=zipfile.ZIP_DEFLATED,
                         compresslevel=9) as archive:
        archive.writestr("manifest.json",
                         json.dumps(manifest, indent=2, ensure_ascii=False) + "\n")
        archive.write(library, manifest["entry"])
        if icon and icon.is_file():
            archive.write(icon, "icon.png")

    with zipfile.ZipFile(output, "r") as archive:
        names = set(archive.namelist())
        if "manifest.json" not in names or manifest["entry"] not in names:
            raise SystemExit(f"package is incomplete: {sorted(names)}")
        for forbidden in ("libpreloader.so", "libc++_shared.so"):
            if forbidden in names:
                raise SystemExit(
                    f"{forbidden} must not be bundled; the launcher provides it"
                )

    digest = hashlib.sha256(output.read_bytes()).hexdigest()
    print(f"wrote {output} ({output.stat().st_size} bytes)")
    print(f"  entry:  {manifest['entry']} ({library.stat().st_size} bytes)")
    print(f"  sha256: {digest}")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--library", required=True, type=Path,
                        help="path to libleviboost.so")
    parser.add_argument("--output", required=True, type=Path,
                        help="path of the .levipack to write")
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST,
                        help="manifest to embed (default: repository root)")
    parser.add_argument("--icon", type=Path, default=REPO_ROOT / "assets/icon.png",
                        help="icon.png to embed (optional)")
    args = parser.parse_args()

    build_package(args.library, args.manifest, args.icon, args.output)
    return 0


if __name__ == "__main__":
    sys.exit(main())
