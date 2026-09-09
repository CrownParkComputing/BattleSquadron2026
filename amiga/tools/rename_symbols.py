#!/usr/bin/env python3
"""Apply verified names to IRA output without changing emitted bytes."""

from __future__ import annotations

import argparse
import re
from pathlib import Path


COMMON = {
    "EXT_DFF080": "COP1LC",
    "EXT_DFF096": "DMACON",
    "EXT_DFF09A": "INTENA",
    "EXT_DFF09C": "INTREQ",
    "EXT_BFE001": "CIAA_PRA",
}

PER_FILE = {
    "loader.asm": {
        "SECSTRT_0": "LoaderEntry",
        "LAB_150": "TakeOverSystem",
        "LAB_1E0": "EnterSupervisor",
        "LAB_298": "CopyLongs",
        "LAB_400": "GameBootstrap",
        "LAB_1BA8": "LoadModule",
        "LAB_1C9E": "Clear32Words",
        "LAB_AB46": "BondDecompress",
        "LAB_EAD0": "LoadFileByName",
        "EXT_246F0": "GameplayOverlayEntry",
        "EXT_3D800": "CommonOverlayEntry",
    },
    "lodgam.asm": {
        "SECSTRT_0": "GameplayJumpTable",
        "LAB_247A6": "InitAudioChannels",
        "LAB_247C8": "InitAudioChannel",
        "LAB_24C6E": "AudioSystemInit",
        "LAB_24CBE": "AudioSystemShutdown",
        "LAB_24D16": "ToggleAudioChannel",
        "LAB_24D6E": "PlaySoundEffect",
        "LAB_24DDE": "SelectMusic",
        "LAB_24E22": "AudioChannelTable",
    },
    "lodcom.asm": {
        "SECSTRT_0": "CommonJumpTable",
        "LAB_3D812": "InitAudioChannels",
        "LAB_3D834": "InitAudioChannel",
        "LAB_3D8B4": "UpdateAudioChannel",
        "LAB_3DC64": "AudioSystemInit",
        "LAB_3DCB6": "AudioSystemShutdown",
        "LAB_3DCD8": "RequestMusicStop",
        "LAB_3DD74": "AudioInterruptHandler",
    },
}


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("assembly", type=Path)
    args = parser.parse_args()
    names = {**COMMON, **PER_FILE.get(args.assembly.name, {})}
    source = args.assembly.read_text()
    for old, new in names.items():
        source = re.sub(rf"\b{old}\b", new, source)
    args.assembly.write_text(source)
    print(f"{args.assembly.name}: applied {len(names)} verified names")


if __name__ == "__main__":
    main()
