#!/usr/bin/env python3
"""Carve Hybris into files, the way Battle Squadron is already shipped.

The cracked disk is an AmigaDOS volume whose filesystem holds only the boot
executable; the game's 16 data files live on the raw tracks behind it, which
is why the disk looked opaque.  They are stored contiguously and track
aligned, so once each one's start is known the disk becomes a directory:

    original/hybris/hybris.exe   the loader, a normal AmigaDOS hunk file
    original/hybris/data/03..18  the game files, byte-identical to the disk
    original/hybris/disk-map.json

Starts are located with an oracle -- the WHDLoad install's data directory,
whose files are the same bytes -- so the map is derived, never guessed.  Any
file whose disk copy differs from the oracle copy is reported with the exact
byte count, since a silent near-match is how a wrong carve hides.
"""
import argparse
import json
import os
import struct
import sys

SECTOR = 512
SECTORS_PER_TRACK = 11
TRACK = SECTOR * SECTORS_PER_TRACK
ADF_SIZE = 160 * TRACK

T_SHORT, ST_FILE = 2, -3


def read_block(image, block):
    return image[block * SECTOR:(block + 1) * SECTOR]


def ofs_root(image):
    """The root block of a standard 880K OFS volume."""
    return 880


def ofs_walk(image, block, path=""):
    """Yield (name, header_block, size) for every file under a directory."""
    header = read_block(image, block)
    hash_table = struct.unpack(">72I", header[24:24 + 72 * 4])
    for entry in hash_table:
        chain = entry
        while chain:
            node = read_block(image, chain)
            secondary = struct.unpack(">i", node[-4:])[0]
            name_length = node[-80]
            name = node[-79:-79 + name_length].decode("latin-1")
            full = f"{path}/{name}" if path else name
            if secondary == ST_FILE:
                size = struct.unpack(">I", node[-188:-184])[0]
                yield full, chain, size
            else:
                yield from ofs_walk(image, chain, full)
            chain = struct.unpack(">I", node[-16:-12])[0]


def ofs_read_file(image, header_block, size):
    """OFS keeps 24 bytes of header in every data block."""
    out = bytearray()
    header = read_block(image, header_block)
    data_blocks = list(struct.unpack(">72I", header[24:24 + 72 * 4]))
    extension = struct.unpack(">I", header[-8:-4])[0]
    while True:
        for block in reversed(data_blocks):
            if not block:
                continue
            chunk = read_block(image, block)
            used = struct.unpack(">I", chunk[12:16])[0]
            out += chunk[24:24 + used]
        if not extension:
            break
        header = read_block(image, extension)
        data_blocks = list(struct.unpack(">72I", header[24:24 + 72 * 4]))
        extension = struct.unpack(">I", header[-8:-4])[0]
    return bytes(out[:size])


def locate(image, wanted, probe=64):
    """Find `wanted` on the raw disk.  Returns (offset, matching_bytes)."""
    best = (-1, 0)
    start = 0
    while True:
        at = image.find(wanted[:probe], start)
        if at < 0:
            break
        run = 0
        while run < len(wanted) and image[at + run] == wanted[run]:
            run += 1
        if run > best[1]:
            best = (at, run)
        if run == len(wanted):
            break
        start = at + 1
    return best


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("adf")
    parser.add_argument("--oracle",
                        default=os.path.expanduser(
                            "~/Downloads/whdload-pipeline/HybrisNTSC/data"),
                        help="directory of known-good copies used to locate "
                             "each file on the raw disk")
    parser.add_argument("--out", default="original/hybris")
    args = parser.parse_args()

    with open(args.adf, "rb") as handle:
        image = handle.read()
    if len(image) != ADF_SIZE:
        sys.exit(f"{args.adf}: {len(image)} bytes, expected {ADF_SIZE}")
    if image[:3] != b"DOS":
        sys.exit(f"{args.adf}: no DOS boot block")

    data_dir = os.path.join(args.out, "data")
    os.makedirs(data_dir, exist_ok=True)

    # 1. The loader, out of the filesystem.
    written = []
    for name, block, size in ofs_walk(image, ofs_root(image)):
        if "/" in name:
            continue
        content = ofs_read_file(image, block, size)
        if len(content) != size:
            sys.exit(f"{name}: read {len(content)} of {size} bytes")
        if content[:4] != b"\x00\x00\x03\xf3":
            continue
        path = os.path.join(args.out, "hybris.exe")
        with open(path, "wb") as handle:
            handle.write(content)
        written.append((path, size))
        print(f"loader   {name:12s} {size:7d} bytes -> {path}")

    if not written:
        sys.exit("no hunk executable in the filesystem")

    # 2. The game files, off the raw tracks.
    if not os.path.isdir(args.oracle):
        sys.exit(f"{args.oracle}: no oracle directory, cannot locate files")

    entries = []
    for name in sorted(os.listdir(args.oracle)):
        if not name.isdigit():
            continue
        with open(os.path.join(args.oracle, name), "rb") as handle:
            reference = handle.read()
        offset, matched = locate(image, reference)
        if offset < 0:
            print(f"file {name}: NOT FOUND on the disk")
            continue
        entries.append({
            "id": name,
            "offset": offset,
            "length": len(reference),
            "track": offset // TRACK,
            "sector": offset // SECTOR,
            "track_aligned": offset % TRACK == 0,
            "oracle_match": matched,
        })

    entries.sort(key=lambda entry: entry["offset"])
    for entry in entries:
        carved = image[entry["offset"]:entry["offset"] + entry["length"]]
        path = os.path.join(data_dir, entry["id"])
        with open(path, "wb") as handle:
            handle.write(carved)
        note = "exact" if entry["oracle_match"] == entry["length"] else \
            f"differs after {entry['oracle_match']} bytes"
        print(f"file {entry['id']}  track {entry['track']:3d} "
              f"offset ${entry['offset']:06x} {entry['length']:7d} bytes  "
              f"{note}")

    # 3. Whatever the map does not account for: the disk regions no known
    #    file covers.  These are the leads for the rest of the port.
    covered = bytearray(len(image))
    for entry in entries:
        covered[entry["offset"]:entry["offset"] + entry["length"]] = \
            b"\x01" * entry["length"]
    covered[:2 * TRACK] = b"\x01" * (2 * TRACK)      # the DOS filesystem
    unaccounted, run_start = [], None
    for track in range(160):
        block = image[track * TRACK:(track + 1) * TRACK]
        seen = covered[track * TRACK:(track + 1) * TRACK]
        # Only a track NOTHING claims is a real lead; the tail track
        # of a file is partly covered and is not a separate region.
        live = any(block) and not any(seen)
        if live and run_start is None:
            run_start = track
        elif not live and run_start is not None:
            unaccounted.append([run_start, track - 1])
            run_start = None
    if run_start is not None:
        unaccounted.append([run_start, 159])
    for first, last in unaccounted:
        print(f"unmapped tracks {first}-{last} "
              f"(${first * TRACK:06x}, {(last - first + 1) * TRACK} bytes)")

    manifest = {
        "source": os.path.abspath(args.adf),
        "loader": "hybris.exe",
        "files": entries,
        "unmapped_track_runs": unaccounted,
    }
    # A plain-text twin of the map: the C host parses this, so the runtime
    # never needs a JSON reader or the disk image.
    text_path = os.path.join(args.out, "disk-map.txt")
    with open(text_path, "w") as handle:
        handle.write("# id  first_track  tracks  length\n")
        for entry in entries:
            tracks = (entry["length"] + TRACK - 1) // TRACK
            handle.write(f"{entry['id']} {entry['track']} {tracks} "
                         f"{entry['length']}\n")
    print(f"wrote {text_path}")

    map_path = os.path.join(args.out, "disk-map.json")
    with open(map_path, "w") as handle:
        json.dump(manifest, handle, indent=2)
        handle.write("\n")
    print(f"wrote {map_path}: {len(entries)} files")


if __name__ == "__main__":
    main()
