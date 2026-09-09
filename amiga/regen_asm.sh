#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"

archive=/home/jon/Downloads/BattleSquadron_v1.8_0941.lha
whdload=original/whdload
data="$whdload/BattleSquadron/data"

if [ ! -f "$data/LOADER" ]; then
    mkdir -p "$whdload"
    7z x -y -o"$whdload" "$archive" >/dev/null
fi

tools/extract_slave.py \
    "$whdload/BattleSquadron/BattleSquadron.slave" original/slave.bin
tools/extract_modules.py "$data" original/modules docs/module-map.json

tools/discover_code.py "$data/LOADER" --base 0x100 \
    --entry 0x100 --entry 0x150 --entry 0x1e0 --entry 0x206 \
    --entry 0x21a --entry 0x228 --entry 0x232 --entry 0x23c \
    --entry 0x246 --entry 0x250 --entry 0x25a --entry 0x284 \
    --entry 0x2a0 --entry 0x400 --entry 0x1ba8 --entry 0x1c2c \
    --entry 0x1c7a --entry 0x1c9e --entry 0x1cae --entry 0xab46 \
    --entry 0xead0 --output original/loader.cnf
tools/discover_code.py original/modules/LODGAM.bin --base 0x246f0 \
    --entry 0x246f0 --entry 0x246f6 --entry 0x246fc \
    --entry 0x24702 --entry 0x24708 --entry 0x2470e \
    --entry 0x24714 --entry 0x2471a --entry 0x24720 \
    --entry 0x24726 --entry 0x2472c --entry 0x24732 \
    --entry 0x24738 --entry 0x2473e --entry 0x24744 \
    --entry 0x2474a --entry 0x24750 --entry 0x247a6 \
    --entry 0x24f34 --output original/lodgam.cnf
tools/discover_code.py original/modules/LODCOM.bin --base 0x3d800 \
    --entry 0x3d800 --entry 0x3d806 --entry 0x3d80c \
    --entry 0x3d812 --entry 0x3dd74 --output original/lodcom.cnf

# IRA derives its config filename from the binary filename.
cp original/loader.cnf "$data/LOADER.cnf"
cp original/lodgam.cnf original/modules/LODGAM.cnf
cp original/lodcom.cnf original/modules/LODCOM.cnf

mkdir -p asm
(cd asm && ira -M68000 -BINARY -OFFSET=0x100 -CONFIG -A -LABEL=1 \
    ../"$data"/LOADER loader.asm >/dev/null 2>&1)
(cd asm && ira -M68000 -BINARY -OFFSET=0x246f0 -CONFIG -A -LABEL=1 \
    ../original/modules/LODGAM.bin lodgam.asm >/dev/null 2>&1)
(cd asm && ira -M68000 -BINARY -OFFSET=0x3d800 -CONFIG -A -LABEL=1 \
    ../original/modules/LODCOM.bin lodcom.asm >/dev/null 2>&1)

tools/rename_symbols.py asm/loader.asm
tools/rename_symbols.py asm/lodgam.asm
tools/rename_symbols.py asm/lodcom.asm
./verify.sh
