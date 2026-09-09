#!/usr/bin/env python3
"""Build canonical runtime overlays from the WHDLoad data directory."""

from __future__ import annotations

import argparse
import json
import struct
from pathlib import Path

from depack_bond import depack


TABLE_FILE_OFFSET = 0x1880
DESCRIPTOR_SIZE = 24
LAST_MODULE = "LODSAV"


def s32(value: int) -> int:
    return value - 0x100000000 if value & 0x80000000 else value


def descriptors(loader: bytes):
    offset = TABLE_FILE_OFFSET
    while offset + DESCRIPTOR_SIZE <= len(loader):
        load_address, packed_size, staging_address, raw_mode = struct.unpack_from(
            ">IIII", loader, offset
        )
        name = loader[offset + 16 : offset + 24].rstrip(b"\0").decode("ascii")
        yield name, load_address, packed_size, staging_address, s32(raw_mode)
        offset += DESCRIPTOR_SIZE
        if name == LAST_MODULE:
            return
    raise ValueError(f"module table did not end with {LAST_MODULE}")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("data_dir", type=Path)
    parser.add_argument("output_dir", type=Path)
    parser.add_argument("manifest", type=Path)
    args = parser.parse_args()

    loader = (args.data_dir / "LOADER").read_bytes()
    args.output_dir.mkdir(parents=True, exist_ok=True)
    manifest = []
    for name, address, packed_size, staging, mode in descriptors(loader):
        source = args.data_dir / name
        item = {
            "name": name,
            "load_address": address,
            "packed_size": packed_size,
            "staging_address": staging,
            "mode": mode,
        }
        if not source.exists():
            item["status"] = "missing"
            manifest.append(item)
            print(f"{name}: missing (optional/unused in this install)")
            continue
        packed = source.read_bytes()
        if len(packed) != packed_size:
            raise ValueError(
                f"{name}: table size {packed_size} != file size {len(packed)}"
            )
        runtime = depack(packed) if mode < 0 else packed
        destination = args.output_dir / f"{name}.bin"
        destination.write_bytes(runtime)
        item.update(
            runtime_size=len(runtime),
            status="depacked" if mode < 0 else "raw",
            output=destination.name,
        )
        manifest.append(item)
        print(
            f"{name}: ${address:06x}, {len(packed):6d} -> "
            f"{len(runtime):6d} bytes ({item['status']})"
        )

    args.manifest.parent.mkdir(parents=True, exist_ok=True)
    args.manifest.write_text(json.dumps(manifest, indent=2) + "\n")
    print(f"wrote {args.manifest}")


if __name__ == "__main__":
    main()
