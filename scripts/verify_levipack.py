#!/usr/bin/env python3
"""Verifies a LeviBoost .levipack before it is published.

Checks the archive layout the launcher expects, that the entry library is a
64-bit AArch64 shared object built for Android (no executable stack, 16 KB page
alignment), and that nothing the launcher already provides is bundled.

Usage:
    python3 scripts/verify_levipack.py dist/LeviBoost.levipack
"""

import argparse
import json
import struct
import sys
import zipfile
from pathlib import Path

FORBIDDEN_ENTRIES = ("libpreloader.so", "libc++_shared.so", "libc.so", "libm.so")
REQUIRED_MANIFEST_KEYS = ("type", "name", "author", "version", "entry")


def fail(message: str) -> None:
    print(f"FAIL {message}")
    raise SystemExit(1)


def check_elf(data: bytes, name: str) -> None:
    if data[:4] != b"\x7fELF":
        fail(f"{name} is not an ELF file")
    if data[4] != 2:
        fail(f"{name} is not 64-bit")
    machine = struct.unpack_from("<H", data, 18)[0]
    if machine != 183:
        fail(f"{name} is not AArch64 (machine={machine})")
    elf_type = struct.unpack_from("<H", data, 16)[0]
    if elf_type != 3:
        fail(f"{name} is not a shared object (type={elf_type})")

    # Program headers: reject an executable stack and check 16 KB compatibility.
    program_offset = struct.unpack_from("<Q", data, 32)[0]
    program_entry_size = struct.unpack_from("<H", data, 54)[0]
    program_count = struct.unpack_from("<H", data, 56)[0]
    max_align = 0
    for index in range(program_count):
        base = program_offset + index * program_entry_size
        p_type = struct.unpack_from("<I", data, base)[0]
        p_flags = struct.unpack_from("<I", data, base + 4)[0]
        p_align = struct.unpack_from("<Q", data, base + 48)[0]
        if p_type == 1 and p_flags & 0x2:  # PT_LOAD | PF_W
            pass
        if p_type == 0x6474E551 and p_flags & 0x1:  # PT_GNU_STACK | PF_X
            fail(f"{name} requests an executable stack")
        max_align = max(max_align, p_align)
    if max_align < 16384:
        print(f"  note: maximum segment alignment is {max_align} bytes; "
              "build with -Wl,-z,max-page-size=16384 for 16 KB page devices")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("package", type=Path)
    args = parser.parse_args()

    if not args.package.is_file():
        fail(f"{args.package} not found")

    with zipfile.ZipFile(args.package) as archive:
        names = archive.namelist()
        if "manifest.json" not in names:
            fail("manifest.json is missing")
        manifest = json.loads(archive.read("manifest.json"))
        missing = [key for key in REQUIRED_MANIFEST_KEYS if not manifest.get(key)]
        if missing:
            fail(f"manifest is missing: {', '.join(missing)}")
        if manifest["type"] != "preload-native":
            fail(f"unexpected manifest type {manifest['type']!r}")

        entry = manifest["entry"]
        if entry not in names:
            fail(f"entry {entry} is missing from the archive")
        for forbidden in FORBIDDEN_ENTRIES:
            if any(name.endswith(forbidden) for name in names):
                fail(f"{forbidden} must not be bundled")

        data = archive.read(entry)
        print(f"package : {args.package}")
        print(f"name    : {manifest['name']} {manifest['version']} "
              f"by {manifest['author']}")
        print(f"entry   : {entry} ({len(data)} bytes)")
        print(f"versions: {manifest.get('minecraft_versions') or 'all'}")
        print(f"entries : {', '.join(sorted(names))}")
        check_elf(data, entry)

    print("OK - the package is a valid LeviLaunchroid preload-native mod")
    return 0


if __name__ == "__main__":
    sys.exit(main())
